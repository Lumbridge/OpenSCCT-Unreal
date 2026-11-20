#include "ScctImportLibrary.h"

#include "Components/SkyLightComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Light.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/SHMath.h"
#include "PrecomputedVolumetricLightmap.h"
#include "RenderingThread.h"
#include "SceneInterface.h"

namespace
{
	int32 IndexOf(FIntVector P, FIntVector Size)
	{
		return P.X + Size.X * (P.Y + Size.Y * P.Z);
	}

	void StoreProbe(FVolumetricLightmapBrickData& Brick, int32 Index, const FSHVectorRGB3& SH)
	{
		const FFloat3Packed Ambient(FLinearColor(FMath::Max(0.f, SH.R.V[0]), FMath::Max(0.f, SH.G.V[0]), FMath::Max(0.f, SH.B.V[0])));
		FMemory::Memcpy(Brick.AmbientVector.Data.GetData() + 4 * Index, &Ambient, 4);
		const FSHVector3* Channels[] = {&SH.R, &SH.G, &SH.B};
		// Inverse of the runtime lightmap shader's two SH denormalization vectors.
		constexpr float Basis[] = {.488603f,.488603f,.488603f,1.092548f,1.092548f,1.261568f,1.092548f,1.092548f};
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			const float* V = Channels[Channel]->V;
			for (int32 K = 0; K < 8; ++K)
			{
				const float Ratio = V[K + 1] * .282095f / (Basis[K] * FMath::Max(V[0], .00001f));
				Brick.SHCoefficients[2 * Channel + K / 4].Data[4 * Index + K % 4] =
					FMath::RoundToInt(255.f * FMath::Clamp(.5f + .5f * Ratio, 0.f, 1.f));
			}
		}
	}
}

int32 UScctImportLibrary::BuildMissingLightProbes(UWorld* World)
{
	if (!World || !World->PersistentLevel || !World->PersistentLevel->MapBuildData)
	{
		return -1;
	}
	ULevel* Level = World->PersistentLevel;
	UMapBuildDataRegistry* Registry = Level->MapBuildData;
	auto* Existing = Registry->GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId);
	if (Existing && Existing->BrickDataDimensions.GetMin() > 0 && Existing->BrickData.AmbientVector.DataSize > 0)
	{
		return 0;
	}
	if (!World->Scene || IsRunningCommandlet())
	{
		return -1;
	}
	FBox Bounds(ForceInit);
	for (TActorIterator<ALightmassImportanceVolume> It(World); It; ++It)
	{
		Bounds += It->GetComponentsBoundingBox(true);
	}
	if (!Bounds.IsValid || Bounds.GetSize().GetMin() <= 0) { return -1; }

	// Sample the outgoing light from static surfaces in six directions. Live lamps must
	// not leave a permanent contribution after a player switches them off.
	TArray<ULightComponent*> HiddenLights;
	for (TActorIterator<ALight> It(World); It; ++It)
	{
		ULightComponent* Light = It->GetLightComponent();
		if (Light && Light->Mobility == EComponentMobility::Movable && Light->IsVisible())
		{
			HiddenLights.Add(Light);
			Light->SetVisibility(false);
		}
	}
	USkyLightComponent* Capture = NewObject<USkyLightComponent>(World, NAME_None, RF_Transient);
	Capture->SetMobility(EComponentMobility::Movable);
	Capture->SetVisibility(false);
	Capture->CubemapResolution = 16;
	Capture->SkyDistanceThreshold = 5.f;
	Capture->bLowerHemisphereIsBlack = false;
	Capture->RegisterComponentWithWorld(World);
	const FVector Size = Bounds.GetSize();
	const double Spacing = FMath::Max(200., FMath::Pow(Size.X * Size.Y * Size.Z / 2400., 1. / 3.));
	const FIntVector Bricks(FMath::CeilToInt(Size.X / (4 * Spacing)), FMath::CeilToInt(Size.Y / (4 * Spacing)), FMath::CeilToInt(Size.Z / (4 * Spacing)));
	if (Bricks.GetMax() > 255)
	{
		Capture->DestroyComponent();
		for (auto* Light : HiddenLights) { Light->SetVisibility(true); }
		return -1;
	}
	const FIntVector Cells = Bricks * 4;
	const FIntVector Samples = Cells + FIntVector(1);
	int32 LitProbes = 0;
	TArray<FSHVectorRGB3> Probes;
	Probes.SetNumZeroed(Samples.X * Samples.Y * Samples.Z);
	for (int32 Z = 0; Z < Samples.Z; ++Z)
	{
		for (int32 Y = 0; Y < Samples.Y; ++Y)
		{
			for (int32 X = 0; X < Samples.X; ++X)
			{
				const FIntVector P(X, Y, Z);
				Capture->SetWorldLocation(Bounds.Min + Size * FVector(P) / FVector(Cells));
				float Brightness = 0;
				World->Scene->UpdateSkyCaptureContents(Capture, false, nullptr, nullptr, Brightness, Probes[IndexOf(P, Samples)], nullptr, nullptr);
				FlushRenderingCommands();
				const auto& Sample = Probes[IndexOf(P, Samples)];
				if (Sample.R.V[0] + Sample.G.V[0] + Sample.B.V[0] > .0001f) { ++LitProbes; }
			}
		}
		UE_LOG(LogTemp, Display, TEXT("SCCT-LIGHTING captured probe layer %d/%d"), Z+1, Samples.Z);
	}
	Capture->DestroyComponent();
	for (auto* Light : HiddenLights) { Light->SetVisibility(true); }

	UE_LOG(LogTemp, Display, TEXT("SCCT-LIGHTING %d of %d probes received light"), LitProbes, Probes.Num());
	if (!LitProbes) { return -1; }
	Level->ReleaseRenderingResources();
	FlushRenderingCommands();
	FPrecomputedVolumetricLightmapData& Data = Existing ? *Existing : Registry->AllocateLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId);
	Data.InitializeOnImport(Bounds, 4);
	Data.IndirectionTextureDimensions = Bricks;
	Data.IndirectionTexture.Format = PF_R8G8B8A8_UINT;
	Data.IndirectionTexture.Resize(4 * Bricks.X * Bricks.Y * Bricks.Z);
	Data.BrickDataDimensions = Bricks * 5;
	const int32 Voxels = Data.BrickDataDimensions.X * Data.BrickDataDimensions.Y * Data.BrickDataDimensions.Z;
	auto& Brick = Data.BrickData;
	Brick.AmbientVector.Format = PF_FloatR11G11B10;
	Brick.AmbientVector.Resize(4 * Voxels);
	for (auto& Coefficients : Brick.SHCoefficients) { Coefficients.Format = PF_R8G8B8A8; Coefficients.Resize(4 * Voxels); }
	Brick.SkyBentNormal.Format = PF_R8G8B8A8;
	Brick.DirectionalLightShadowing.Format = PF_G8;
	Brick.DirectionalLightShadowing.Resize(Voxels);
	FMemory::Memset(Brick.DirectionalLightShadowing.Data.GetData(), 255, Voxels);
	for (int32 Z = 0; Z < Bricks.Z; ++Z)
	for (int32 Y = 0; Y < Bricks.Y; ++Y)
	for (int32 X = 0; X < Bricks.X; ++X)
	{
		const FIntVector Tile(X, Y, Z);
		uint8* Entry = &Data.IndirectionTexture.Data[4 * IndexOf(Tile, Bricks)];
		Entry[0] = X; Entry[1] = Y; Entry[2] = Z; Entry[3] = 1;
		for (int32 K = 0; K < 125; ++K)
		{
			const FIntVector Local(K % 5, (K / 5) % 5, K / 25);
			StoreProbe(Brick, IndexOf(Tile * 5 + Local, Data.BrickDataDimensions), Probes[IndexOf(Tile * 4 + Local, Samples)]);
		}
	}
	Data.FinalizeImport();
	Registry->MarkPackageDirty();
	Level->InitializeRenderingResources();
	return Probes.Num();
}
