// Responsive smoke drawn as volumes (Play/ScctEffectsSmokeVolume.h says what and why).

#include "Play/ScctEffectsSmokeVolume.h"

#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const TCHAR* const SmokeVolumeMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxSmokeVolume.M_ScctFxSmokeVolume");
	const TCHAR* const CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** The engine's cube is 100 cm on a side, centred on its origin. */
	constexpr float CubeEdge = 100.0f;

	// The shared cloud reaches several cells between ordinary Fast snapshots during its initial release.
	// Follow the 20 Hz field during bloom, then ease back to the selected steady-state budget.
	constexpr float BloomInterval = 1.0f / 20.0f;
	constexpr float BloomSeconds = 0.7f;
	constexpr float SettleSeconds = 0.35f;

	/** Clouds drawn at once; more than the field ever holds in one place in a match. */
	constexpr int32 MaxClouds = 8;

	/**
	 * Shot holes carved into one cloud at once. A hole is open for half a second, so this is more than six players
	 * emptying their guns into the same cloud keep open; the newest are kept.
	 */
	constexpr int32 MaxHoles = 32;

	/** How often a cloud's light is looked at again, seconds: lamps are switched and shot out. */
	constexpr float ProbeInterval = 0.25f;

	/** How far around the cloud a light still counts toward its ambient (the Godot client's IndirectRadius is 300 uu). */
	constexpr float BroadReach = 300.0f;

	/** A line to a light that meets something this close to it has met the lamp's own shade or lantern, cm. */
	constexpr float LampShade = 80.0f;

	/** How long a smoke emitter's sprites wait for its cloud to arrive before they are drawn after all, seconds. */
	constexpr float CloudArrivalWait = 1.0f;

	/** The summary line while clouds are out, every this many seconds. */
	constexpr float ReportInterval = 2.0f;

	TAutoConsoleVariable<float> CVarSmokeLight(
		TEXT("scct.SmokeLight"), 1.0f,
		TEXT("How strongly the map's lights light responsive smoke (1: as a white wall beside it would be)."));

	TAutoConsoleVariable<float> CVarSmokeAmbient(
		TEXT("scct.SmokeAmbient"), 1.0f,
		TEXT("How much of the light around a responsive smoke cloud lights it from all sides."));

	FAutoConsoleCommand SmokeQualityCommand(
		TEXT("scct.SmokeQuality"),
		TEXT("Sets the responsive smoke quality: 0 Fast, 1 Medium, 2 High (the Graphics page's option)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() > 0)
			{
				FScctSettings::SetInt(FScctSmokeVolumes::QualitySetting, FCString::Atoi(*Args[0]));
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke quality is %d"), FScctSettings::GetInt(FScctSmokeVolumes::QualitySetting));
		}));

	UVolumeTexture* MakeGridTexture(int32 Size)
	{
		UVolumeTexture* Texture = UVolumeTexture::CreateTransient(Size, Size, Size, PF_R8G8);
		if (Texture == nullptr) return nullptr;
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->AddressMode = TA_Clamp;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		return Texture;
	}

	/** Luminance of a linear colour. */
	float Luminance(const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}
}

const FName FScctSmokeVolumes::QualitySetting(TEXT("Graphics.ResponsiveSmokeQuality"));

void FScctSmokeVolumes::RegisterSettings()
{
	FScctSettingDesc Desc;
	Desc.Key = QualitySetting;
	Desc.Page = TEXT("Graphics");
	Desc.Group = NSLOCTEXT("OpenSCCT", "SmokeGroup", "Effects");
	Desc.Label = NSLOCTEXT("OpenSCCT", "SmokeQuality", "Responsive smoke quality");
	Desc.Tooltip = NSLOCTEXT("OpenSCCT", "SmokeQualityTip",
		"Higher settings sample the cloud more often and add finer shading. This uses more CPU time to update smoke and more GPU time while it is on screen. Gas and AI visibility use the same simulation at every setting.");
	Desc.Type = EScctSettingType::Choice;
	Desc.Choices = { NSLOCTEXT("OpenSCCT", "SmokeFast", "Fast"), NSLOCTEXT("OpenSCCT", "SmokeMedium", "Medium"), NSLOCTEXT("OpenSCCT", "SmokeHigh", "High") };
	Desc.Default = TEXT("0");
	Desc.Order = 60;
	FScctSettings::Register(Desc);
}

const FScctSmokeVolumes::FQuality& FScctSmokeVolumes::QualityOf(int32 Level)
{
	static const FQuality Levels[] = {
		{ 16, 1.0f / 10.0f, 32.0f, 0.0f, TEXT("Fast") },
		{ 32, 1.0f / 15.0f, 56.0f, 2.0f, TEXT("Medium") },
		{ 48, 1.0f / 20.0f, 88.0f, 4.0f, TEXT("High") },
	};
	return Levels[FMath::Clamp(Level, 0, 2)];
}

FScctSmokeVolumes::FScctSmokeVolumes()
{
	SettingHandle = FScctSettings::OnChanged().AddRaw(this, &FScctSmokeVolumes::HandleSettingChanged);
	Quality = FMath::Clamp(FScctSettings::GetInt(QualitySetting), 0, 2);
	FieldBuffer.SetNumUninitialized(MaxClouds);
	HoleBuffer.SetNumUninitialized(MaxHoles);
}

FScctSmokeVolumes::~FScctSmokeVolumes()
{
	FScctSettings::OnChanged().Remove(SettingHandle);
}

void FScctSmokeVolumes::Init(AActor* InHost)
{
	Host = InHost;
	Material.Reset(LoadObject<UMaterialInterface>(nullptr, SmokeVolumeMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
	Cube.Reset(LoadObject<UStaticMesh>(nullptr, CubePath));
	bMaterialMissing = Material == nullptr || Cube == nullptr;
}

void FScctSmokeVolumes::HandleSettingChanged(FName Key)
{
	if (Key != QualitySetting)
	{
		return;
	}
	const int32 NewQuality = FMath::Clamp(FScctSettings::GetInt(QualitySetting), 0, 2);
	if (NewQuality != Quality)
	{
		Quality = NewQuality;
		ApplyQuality(true);
	}
}

void FScctSmokeVolumes::ApplyQuality(bool bLog)
{
	const FQuality& Q = QualityOf(Quality);
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Look.IsValid())
		{
			Cloud.Look->SetScalarParameterValue(TEXT("Steps"), Q.Steps);
			Cloud.Look->SetScalarParameterValue(TEXT("ShadowSteps"), Q.ShadowSteps);
		}
		// The next upload is at the new grid size, at once.
		Cloud.UntilUpload = 0.0f;
	}
	if (bLog && bOn)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke on, grid %dx%dx%d (%s quality: %.0f updates a second, %.0f steps, %.0f shadow steps)"),
			Q.Resolution, Q.Resolution, Q.Resolution, Q.Name, 1.0f / Q.Interval, Q.Steps, Q.ShadowSteps);
	}
}

void FScctSmokeVolumes::Reset()
{
	for (FCloud& Cloud : Clouds)
	{
		EndCloud(Cloud);
	}
	Clouds.Reset();
	if (NumClouds > 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke summary: %d clouds drawn as volumes (at most %d at once), %d grids uploaded, %.1f ms a grid on average, %d shot holes carved"),
			NumClouds, PeakClouds, NumUploads, NumUploads > 0 ? SampleMilliseconds / NumUploads : 0.0, NumHolesDrawn);
	}
	NumClouds = NumUploads = PeakClouds = NumHolesDrawn = 0;
	SampleMilliseconds = 0.0;
	bOn = false;
	SinceReport = 0.0f;
}

bool FScctSmokeVolumes::CoversEmitter(int32 ActorId, bool bSmokeEmitter, float Age) const
{
	if (!bOn || bMaterialMissing)
	{
		return false;
	}
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.EmitterId == ActorId)
		{
			return true;
		}
	}
	return bSmokeEmitter && Age < CloudArrivalWait;
}

void FScctSmokeVolumes::Update(float DeltaSeconds)
{
	if (Api == nullptr)
	{
		FString Error;
		Api = ScctFeature<FScctSmokeApi>(&Error);
		if (Api == nullptr)
		{
			if (!bReported)
			{
				bReported = true;
				UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: responsive smoke is drawn as sprites only, the bridge's smoke table is missing: %s"), *Error);
			}
			return;
		}
	}

	FScctSmokeState State = {};
	if (Api->GetState(&State) == 0)
	{
		return;
	}
	const bool bNowOn = State.Enabled != 0;
	if (bNowOn != bOn)
	{
		bOn = bNowOn;
		if (bOn && bMaterialMissing)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: responsive smoke is on but %s is missing (run Tools/Effects/Build-EffectAssets.ps1): its clouds are drawn as sprites"), SmokeVolumeMaterialPath);
		}
		else if (bOn)
		{
			ApplyQuality(true);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke off"));
		}
	}
	if (!bOn || bMaterialMissing || !Host.IsValid() || (State.FieldCount == 0 && Clouds.IsEmpty()))
	{
		return;
	}

	++Frame;
	const int32 Count = Api->GetFields(FieldBuffer.GetData(), FieldBuffer.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctSmokeField& Field = FieldBuffer[Index];
		FCloud* Cloud = Clouds.FindByPredicate([&Field](const FCloud& C) { return C.EmitterId == Field.EmitterId; });
		if (Cloud == nullptr)
		{
			Cloud = &StartCloud(Field);
		}
		Cloud->SeenFrame = Frame;
		Cloud->Last = Field;
		Cloud->SinceUpload += DeltaSeconds;
		Cloud->SinceProbe += DeltaSeconds;

		const FQuality& Q = QualityOf(Quality);
		const float Settle = FMath::Clamp((Field.Age - BloomSeconds) / SettleSeconds, 0.0f, 1.0f);
		const float Interval = FMath::Lerp(FMath::Min(Q.Interval, BloomInterval), Q.Interval, Settle);
		Cloud->UntilUpload -= DeltaSeconds;
		if (Cloud->UntilUpload <= 0.0f && Upload(*Cloud))
		{
			Cloud->SampleSpan = Cloud->Uploads > 1 ? FMath::Max(Cloud->SinceUpload, UE_SMALL_NUMBER) : Interval;
			Cloud->SinceUpload = 0.0f;
			// Keep the fraction of a frame beyond the scheduled boundary. After a hitch sample just once.
			Cloud->UntilUpload = Interval - FMath::Fmod(-Cloud->UntilUpload, Interval);
		}
		if (Cloud->SinceProbe >= ProbeInterval)
		{
			ProbeLight(*Cloud);
		}
		UpdateHoles(*Cloud);
		// Draw one interval behind the newest sample using its actual timestamps. Resetting the
		// blend to zero on every late upload holds then jumps when render and sample rates differ.
		const float Blend = 1.0f + (Cloud->SinceUpload - Interval) / FMath::Max(Cloud->SampleSpan, UE_SMALL_NUMBER);
		Cloud->Look->SetScalarParameterValue(TEXT("Blend"), FMath::Clamp(Blend, 0.0f, 1.0f));
	}
	PeakClouds = FMath::Max(PeakClouds, Count);

	for (int32 Index = Clouds.Num() - 1; Index >= 0; --Index)
	{
		if (Clouds[Index].SeenFrame != Frame)
		{
			EndCloud(Clouds[Index]);
			Clouds.RemoveAtSwap(Index);
		}
	}

	SinceReport += DeltaSeconds;
	if (Clouds.Num() > 0 && SinceReport >= ReportInterval)
	{
		SinceReport = 0.0f;
		FString Line;
		for (const FCloud& Cloud : Clouds)
		{
			Line += FString::Printf(TEXT(" #%d age %.1f s, %d holes (%d shot holes carved now, at most %d), %.0f%% of the box thick, %d uploads;"), Cloud.EmitterId, Cloud.Last.Age, Cloud.Last.Carves,
				Cloud.HoleCount, Cloud.PeakHoles, Cloud.Coverage * 100.0f, Cloud.Uploads);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke: %d clouds, %d grids uploaded (bridge total %d), last grid %.1f ms:%s"),
			Clouds.Num(), NumUploads, State.Uploads, State.LastSampleMilliseconds, *Line);
	}
}

FScctSmokeVolumes::FCloud& FScctSmokeVolumes::StartCloud(const FScctSmokeField& Field)
{
	FCloud& Cloud = Clouds.AddDefaulted_GetRef();
	Cloud.EmitterId = Field.EmitterId;
	Cloud.SinceProbe = 1.0e6f;

	AActor* Owner = Host.Get();
	UStaticMeshComponent* Box = NewObject<UStaticMeshComponent>(Owner);
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetStaticMesh(Cube.Get());
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCastShadow(false);
	Box->bReceivesDecals = false;
	Box->SetupAttachment(Owner->GetRootComponent());
	Box->RegisterComponent();
	Cloud.Box.Reset(Box);

	UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Material.Get(), Owner);
	Box->SetMaterial(0, Look);
	Cloud.Look.Reset(Look);

	const FVector Origin(Field.OriginX, Field.OriginY, Field.OriginZ);
	const float Size = FMath::Max(Field.Size, 1.0f);
	Box->SetWorldLocationAndRotation(Origin + FVector(Size * 0.5f), FRotator::ZeroRotator);
	Box->SetWorldScale3D(FVector(Size / CubeEdge));
	Look->SetVectorParameterValue(TEXT("BoxOrigin"), FLinearColor(Origin.X, Origin.Y, Origin.Z, 0.0f));
	Look->SetScalarParameterValue(TEXT("BoxSize"), Size);
	FScctSmokeState State = {};
	Api->GetState(&State);
	Look->SetScalarParameterValue(TEXT("Extinction"), State.Extinction > 0.0f ? State.Extinction : 0.06f);
	const FQuality& Q = QualityOf(Quality);
	Look->SetScalarParameterValue(TEXT("Steps"), Q.Steps);
	Look->SetScalarParameterValue(TEXT("ShadowSteps"), Q.ShadowSteps);

	++NumClouds;
	UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke cloud #%d starts: box (%.0f, %.0f, %.0f) + %.0f cm, centre (%.0f, %.0f, %.0f), age %.2f s"),
		Field.EmitterId, Origin.X, Origin.Y, Origin.Z, Size, Field.CenterX, Field.CenterY, Field.CenterZ, Field.Age);
	return Cloud;
}

void FScctSmokeVolumes::EndCloud(FCloud& Cloud)
{
	if (Cloud.Box.IsValid())
	{
		Cloud.Box->DestroyComponent();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke cloud #%d ends after %.1f s, %d grids uploaded"), Cloud.EmitterId, Cloud.Last.Age, Cloud.Uploads);
	Cloud = FCloud();
}

bool FScctSmokeVolumes::Upload(FCloud& Cloud)
{
	const FQuality& Q = QualityOf(Quality);
	const int32 N = Q.Resolution;
	Grid.SetNumUninitialized(N * N * N);
	const double Start = FPlatformTime::Seconds();
	if (Api->GetDensity(Cloud.EmitterId, N, Grid.GetData(), Grid.Num()) == 0)
	{
		return false;
	}
	SampleMilliseconds += (FPlatformTime::Seconds() - Start) * 1000.0;

	const bool bResized = Cloud.Resolution != N;
	if (bResized)
	{
		Cloud.Density.Reset(MakeGridTexture(N));
		if (!Cloud.Density.IsValid()) return false;
		Cloud.Resolution = N;
		Cloud.Look->SetTextureParameterValue(TEXT("DensityHistory"), Cloud.Density.Get());
	}
	FTextureResource* Resource = Cloud.Density->GetResource();
	if (!Resource) return false;

	// The two snapshots share filter coordinates, mip and sampler. Interleave them once per update.
	TArray<uint8> Packed;
	Packed.SetNumUninitialized(Grid.Num() * 2);
	FIntVector Min(N), Max(-1);
	int32 Thick = 0;
	const uint8* Previous = Cloud.PreviousGrid.Num() == Grid.Num() ? Cloud.PreviousGrid.GetData() : Grid.GetData();
	// A new emitter often arrives with a dense seed already present. Fade in that first visible
	// sample from clear air. A late join or a graphics change should immediately see the existing cloud.
	const bool bFirstBloom = Cloud.Uploads == 0 && Cloud.Last.Age < BloomSeconds;
	for (int32 Z = 0, I = 0; Z < N; ++Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			int32 First = N, Last = -1;
			for (int32 X = 0; X < N; ++X, ++I)
			{
				const uint8 Value = Grid[I];
				Packed[I * 2] = bFirstBloom ? 0 : Previous[I];
				Packed[I * 2 + 1] = Value;
				if (Value)
				{
					First = FMath::Min(First, X);
					Last = X;
					Thick += Value > 64 ? 1 : 0;
				}
			}
			if (Last >= 0)
			{
				Min = Min.ComponentMin(FIntVector(First, Y, Z));
				Max = Max.ComponentMax(FIntVector(Last, Y, Z));
			}
		}
	}
	Cloud.Coverage = static_cast<float>(Thick) / Grid.Num();
	const FIntVector BothMin = bResized ? Min : Min.ComponentMin(Cloud.OccupiedMin);
	const FIntVector BothMax = bResized ? Max : Max.ComponentMax(Cloud.OccupiedMax);
	Cloud.OccupiedMin = Min;
	Cloud.OccupiedMax = Max;
	// Each nonempty texel contributes one cell either side of its centre under trilinear filtering.
	const float Cell = Cloud.Last.Size / N;
	const FVector BoundMin = (FVector(BothMin) - FVector(0.5)) * Cell;
	const FVector BoundMax = (FVector(BothMax) + FVector(1.5)) * Cell;
	Cloud.Look->SetVectorParameterValue(TEXT("DensityMin"), FLinearColor(BoundMin.X, BoundMin.Y, BoundMin.Z));
	Cloud.Look->SetVectorParameterValue(TEXT("DensityMax"), FLinearColor(BoundMax.X, BoundMax.Y, BoundMax.Z));
	Swap(Grid, Cloud.PreviousGrid);

	// Resource initialization and destruction are ordered with this command on the render thread.
	ENQUEUE_RENDER_COMMAND(ScctUploadSmokeVolume)(
		[Resource, N, Bytes = MoveTemp(Packed)](FRHICommandListImmediate& RHICmdList)
		{
			if (Resource->TextureRHI.IsValid())
			{
				const FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, N, N, N);
				RHICmdList.UpdateTexture3D(Resource->TextureRHI, 0, Region, N * 2, N * N * 2, Bytes.GetData());
			}
		});
	++Cloud.Uploads;
	++NumUploads;
	return true;
}

void FScctSmokeVolumes::UpdateHoles(FCloud& Cloud)
{
	const int32 Count = Api->GetHoles != nullptr ? Api->GetHoles(Cloud.EmitterId, HoleBuffer.GetData(), HoleBuffer.Num()) : 0;
	if (Count == 0 && Cloud.HoleCount == 0)
	{
		return;
	}
	if (!Cloud.Holes.IsValid())
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(3, MaxHoles, PF_A32B32G32R32F);
		if (Texture == nullptr)
		{
			return;
		}
		Texture->SRGB = false;
		Texture->Filter = TF_Nearest;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		Cloud.Holes.Reset(Texture);
		Cloud.Look->SetTextureParameterValue(TEXT("Holes"), Texture);
	}

	// New holes are counted once, by the simulation's sequence number of the newest.
	if (Count > 0 && HoleBuffer[0].Sequence != Cloud.HoleKey)
	{
		for (int32 Index = 0; Index < Count && HoleBuffer[Index].Sequence != Cloud.HoleKey; ++Index)
		{
			++NumHolesDrawn;
		}
		Cloud.HoleKey = HoleBuffer[0].Sequence;
	}
	Cloud.HoleCount = Count;
	Cloud.PeakHoles = FMath::Max(Cloud.PeakHoles, Count);
	Cloud.Look->SetScalarParameterValue(TEXT("HoleCount"), static_cast<float>(Count));
	if (Count == 0)
	{
		return;
	}

	constexpr int32 RowFloats = 12;
	float* Data = static_cast<float*>(FMemory::Malloc(sizeof(float) * RowFloats * Count));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctSmokeHole& Hole = HoleBuffer[Index];
		float* Row = Data + Index * RowFloats;
		Row[0] = Hole.StartX; Row[1] = Hole.StartY; Row[2] = Hole.StartZ; Row[3] = Hole.Radius;
		Row[4] = Hole.EndX; Row[5] = Hole.EndY; Row[6] = Hole.EndZ; Row[7] = Hole.Strength;
		Row[8] = Hole.Feather; Row[9] = Row[10] = Row[11] = 0.0f;
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, 3, Count);
	Cloud.Holes->UpdateTextureRegions(0, 1, Region, sizeof(float) * RowFloats, sizeof(float) * 4, reinterpret_cast<uint8*>(Data),
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SrcData);
			delete Regions;
		});
}

void FScctSmokeVolumes::ProbeLight(FCloud& Cloud)
{
	Cloud.SinceProbe = 0.0f;
	AActor* Owner = Host.Get();
	UWorld* World = Owner != nullptr ? Owner->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	// The light at the cloud's middle: each of the map's lights that reaches it, as the scene's own shading has it
	// (the light's brightness in the engine's units, its falloff and its cone), direct when nothing of the level stands
	// between, and every light near enough as a share of soft light from all sides, in the place of the light the
	// level bounces about (the Godot client's ResponsiveSmokeLighting indirect term).
	const FVector P(Cloud.Last.CenterX, Cloud.Last.CenterY, Cloud.Last.CenterZ);
	FLinearColor Direct(0, 0, 0, 0);
	FLinearColor Broad(0, 0, 0, 0);
	FVector Moment = FVector::ZeroVector;
	FVector BroadMoment = FVector::ZeroVector;
	float Energy = 0.0f;
	int32 Reaching = 0;
	int32 Blocked = 0;
	FString FirstBlocker;
	FCollisionQueryParams Params(TEXT("ScctSmokeLight"), false, Owner);
	for (TObjectIterator<ULocalLightComponent> It; It; ++It)
	{
		ULocalLightComponent* Light = *It;
		if (Light == nullptr || Light->GetWorld() != World || !Light->IsRegistered() || !Light->IsVisible() || Light->Intensity <= 0.0f
			|| !Light->LightingChannels.bChannel0)
		{
			continue;
		}
		const FVector At = Light->GetComponentLocation();
		const float Radius = Light->AttenuationRadius;
		const float Distance = FVector::Dist(At, P);
		if (Radius <= 0.0f || Distance >= Radius + BroadReach)
		{
			continue;
		}
		// The light reaching a point at the edge of the broad reach toward the light stands for the light about.
		const float Near = FMath::Max(Distance - (Distance >= Radius ? BroadReach : 0.0f), 1.0f);
		float Falloff = 0.0f;
		const UPointLightComponent* Point = Cast<UPointLightComponent>(Light);
		if (Point != nullptr && Point->bUseInverseSquaredFalloff)
		{
			const float Window = FMath::Square(FMath::Clamp(1.0f - FMath::Pow(Near / Radius, 4.0f), 0.0f, 1.0f));
			Falloff = Window / (Near * Near + 1.0f);
		}
		else
		{
			const float Exponent = Point != nullptr ? Point->LightFalloffExponent : 8.0f;
			Falloff = FMath::Pow(FMath::Clamp(1.0f - FMath::Square(Near / Radius), 0.0f, 1.0f), Exponent);
		}
		const FVector ToLight = (At - P).GetSafeNormal();
		if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
		{
			const float Outer = FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle));
			const float Inner = FMath::Cos(FMath::DegreesToRadians(FMath::Min(Spot->InnerConeAngle, Spot->OuterConeAngle - 0.5f)));
			const float Cosine = FVector::DotProduct(-ToLight, Light->GetForwardVector());
			const float T = FMath::Clamp((Cosine - Outer) / FMath::Max(Inner - Outer, 1.0e-4f), 0.0f, 1.0f);
			Falloff *= T * T * (3.0f - 2.0f * T);
		}
		if (Falloff <= 0.0f)
		{
			continue;
		}
		const FLinearColor E = Light->GetLightColor() * (Light->ComputeLightBrightness() * Falloff);
		Broad += E;
		BroadMoment += ToLight * Luminance(E);
		// A wall between holds it back; what the line meets close to the light is the lamp's own shade or lantern. The
		// cloud is tall, so the line is tried from its middle and from higher up in it (its middle can sit in a low
		// crate or a floor's lip); a light that reaches any of them lights it. A light whose radius ends short of the
		// middle but reaches the cloud's near side lights that side, at half the strength it has there.
		bool bReaches = false;
		FHitResult Hit;
		for (const float Up : { 0.0f, 90.0f, 180.0f })
		{
			FHitResult Try;
			const FVector From = P + FVector(0.0f, 0.0f, Up);
			if (!World->LineTraceSingleByChannel(Try, From, At, ECC_Visibility, Params) || FVector::Dist(Try.ImpactPoint, At) <= LampShade)
			{
				bReaches = true;
				break;
			}
			if (!Try.bStartPenetrating && !Hit.bBlockingHit)
			{
				Hit = Try;
			}
		}
		if (!bReaches)
		{
			if (Blocked++ == 0 && Hit.bBlockingHit)
			{
				FirstBlocker = FString::Printf(TEXT("%s %.0f cm short of %s"), *GetNameSafe(Hit.GetActor()), FVector::Dist(Hit.ImpactPoint, At), *GetNameSafe(Light->GetOwner()));
			}
			continue;
		}
		++Reaching;
		const FLinearColor Reached = Distance >= Radius ? E * 0.5f : E;
		Direct += Reached;
		const float Weight = Luminance(Reached);
		Moment += ToLight * Weight;
		Energy += Weight;
	}

	// A diffuse surface lit by E is E/pi bright for each unit of its albedo; the cloud's albedo is the material's.
	const float Light = FMath::Max(CVarSmokeLight.GetValueOnGameThread(), 0.0f) / UE_PI;
	const FLinearColor Indirect = (Broad - Direct) * 0.06f + Direct * 0.08f;
	const FLinearColor Ambient = (Indirect * FMath::Max(CVarSmokeAmbient.GetValueOnGameThread(), 0.0f) + FLinearColor(0.004f, 0.004f, 0.0045f)) * Light;
	// With no light in plain sight the light about still comes more from where the lamps are: a little of it is given
	// a direction, so the cloud has a lit and a shaded side rather than one flat tone.
	if (Energy <= 1.0e-5f && !BroadMoment.IsNearlyZero())
	{
		Direct = (Broad - Direct) * 0.09f;
		Moment = BroadMoment;
		Energy = 1.0f;
	}
	const FVector Incident = Energy > 1.0e-5f ? Moment.GetSafeNormal() : FVector::UpVector;
	Cloud.Look->SetVectorParameterValue(TEXT("Ambient"), Ambient);
	Cloud.Look->SetVectorParameterValue(TEXT("LightColor"), Direct * Light);
	Cloud.Look->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(Incident.X, Incident.Y, Incident.Z, 0.0f));
	if (Cloud.Uploads <= 1)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: responsive smoke cloud #%d light: direct (%.3f, %.3f, %.3f) from (%.2f, %.2f, %.2f) by %d lights (%d behind walls%s%s), ambient (%.4f, %.4f, %.4f)"),
			Cloud.EmitterId, Direct.R * Light, Direct.G * Light, Direct.B * Light, Incident.X, Incident.Y, Incident.Z, Reaching, Blocked, FirstBlocker.IsEmpty() ? TEXT("") : TEXT(": first "), *FirstBlocker, Ambient.R, Ambient.G, Ambient.B);
	}
}
