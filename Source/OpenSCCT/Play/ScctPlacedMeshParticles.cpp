#include "Play/ScctPlacedMeshParticles.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Online/ScctMapFolders.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	UStaticMeshComponent* MakeParticle(AActor* Host, UStaticMesh* Mesh)
	{
		auto* Part = NewObject<UStaticMeshComponent>(Host, NAME_None, RF_Transient);
		Host->AddInstanceComponent(Part);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->bAffectDistanceFieldLighting = false;
		Part->bAffectDynamicIndirectLighting = false;
		Part->bReceivesDecals = false;
		Part->SetDefaultCustomPrimitiveDataFloat(0, 1.f); // mesh-local surface detail
		Part->SetVisibility(false);
		Part->SetupAttachment(Host->GetRootComponent());
		return Part;
	}

	UMaterialInstanceDynamic* ParticleMaterial(AActor* Host, const FScctMeshParticleLayer& Info,
		const FString& RenderDir, const FString& TextureName)
	{
		const TCHAR* Blend = Info.DrawStyle == 0 ? TEXT("Regular") : Info.DrawStyle == 3 ? TEXT("Add") :
			Info.DrawStyle == 2 || Info.DrawStyle == 5 ? TEXT("Modulate") :
			Info.DrawStyle == 4 || Info.DrawStyle == 6 ? TEXT("Premul") : TEXT("Alpha");
		const FString Name = FString(TEXT("M_ScctMeshParticle")) + Blend;
		auto* Base = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/OpenSCCT/Effects/%s.%s"), *Name, *Name));
		if (!Base) return nullptr;

		auto* Material = UMaterialInstanceDynamic::Create(Base, Host);
		Material->SetScalarParameterValue(TEXT("Style"), Info.DrawStyle);
		Material->SetScalarParameterValue(TEXT("AlphaRef"), Info.AlphaRef);
		if (!TextureName.IsEmpty() && !RenderDir.IsEmpty())
		{
			FString Path = FPaths::ConvertRelativePathToFull(RenderDir / TextureName);
			FPaths::CollapseRelativeDirectories(Path);
			if (FPaths::IsUnderDirectory(Path, RenderDir) && FPaths::FileExists(Path))
			{
				if (UTexture2D* Tex = FImageUtils::ImportFileAsTexture2D(Path))
				{
					Material->SetTextureParameterValue(TEXT("Texture"), Tex);
				}
			}
		}
		return Material;
	}
}

void FScctPlacedMeshParticles::Begin(UScctLevelStateSubsystem& Level)
{
	Reset();
	UWorld* World = Level.GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	Api = ScctFeature<FScctMeshParticleApi>();
	if (!Api) return;
	const int32 Total = Api->Layers(nullptr, 0);
	if (Total < 0) UE_LOG(LogOpenSCCT, Warning, TEXT("Could not read the map's MeshEmitter definitions"));
	if (Total <= 0) return;

	TArray<FScctMeshParticleLayer> Records;
	Records.SetNumUninitialized(Total);
	if (Api->Layers(Records.GetData(), Total) != Total) return;
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Actor) return;
	Host = Actor;
	auto* Root = NewObject<USceneComponent>(Actor);
	Root->SetMobility(EComponentMobility::Movable);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();

	const FString Folder = ScctMapFolders::FolderOf(Level.Session->GetSessionInfo().Map);
	const FString RenderDir = Folder.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(Folder / TEXT("render"));
	Layers.SetNum(Total);
	int32 Capacity = 0;
	for (int32 i = 0; i < Total; ++i)
	{
		const FScctMeshParticleLayer& Record = Records[i];
		if (Record.Capacity < 0 || Record.Capacity > MAX_int32 - Capacity)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Invalid MeshEmitter capacity in '%s'"), *Level.GetName(Record.OwnerId));
			Reset();
			return;
		}
		Capacity += Record.Capacity;
		const FString& Path = Level.GetName(Record.MeshId);
		UStaticMesh* Mesh = Level.FindMesh(Path);
		if (!Mesh)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("MeshEmitter '%s' has no drawable static mesh '%s'"), *Level.GetName(Record.OwnerId), *Path);
			continue;
		}
		FLayer& Layer = Layers[i];
		UStaticMeshComponent* Template = MakeParticle(Actor, Mesh);
		if (Record.DrawStyle >= 0)
		{
			if (auto* Material = ParticleMaterial(Actor, Record, RenderDir, Level.GetName(Record.TextureId)))
			{
				Layer.bParticleMaterial = true;
				Template->SetForceDisableNanite(true);
				for (int32 Slot = 0; Slot < Template->GetNumMaterials(); ++Slot) Template->SetMaterial(Slot, Material);
			}
		}
		else if (Record.Unlit)
		{
			Template->SetLightingChannels(false, false, false);
			for (int32 Slot = 0; Slot < Template->GetNumMaterials(); ++Slot)
			{
				if (auto* Surface = Template->CreateDynamicMaterialInstance(Slot))
					Surface->SetScalarParameterValue(TEXT("Emissive"), 1.f);
			}
		}
		Template->RegisterComponent();
		Layer.Pool.Add(Template);
		++LayersByOwner.FindOrAdd(Record.OwnerId);
	}
	Buffer.SetNumUninitialized(Capacity);
}

void FScctPlacedMeshParticles::Draw()
{
	if (!Host.IsValid() || LayersByOwner.IsEmpty()) return;
	const int32 Count = Api->Frame(Buffer.GetData(), Buffer.Num());
	if (Count < 0 || Count > Buffer.Num())
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Invalid MeshEmitter frame (%d placements, capacity %d)"), Count, Buffer.Num());
		Reset();
		return;
	}
	for (FLayer& Layer : Layers) Layer.Next = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const FScctMeshParticle& P = Buffer[i];
		if (!Layers.IsValidIndex(P.Layer)) continue;
		FLayer& Layer = Layers[P.Layer];
		if (Layer.Pool.IsEmpty()) continue;
		if (Layer.Next == Layer.Pool.Num())
		{
			UStaticMeshComponent* First = Layer.Pool[0].Get();
			auto* Part = MakeParticle(Host.Get(), First->GetStaticMesh());
			Part->SetLightingChannels(First->LightingChannels.bChannel0, First->LightingChannels.bChannel1, First->LightingChannels.bChannel2);
			Part->SetForceDisableNanite(First->IsForceDisableNanite());
			for (int32 Slot = 0; Slot < First->GetNumMaterials(); ++Slot) Part->SetMaterial(Slot, First->GetMaterial(Slot));
			Part->RegisterComponent();
			Layer.Pool.Add(Part);
		}
		auto* Part = Layer.Pool[Layer.Next++].Get();
		Part->SetWorldTransform(FTransform(FRotator(P.Pitch, P.Yaw, P.Roll), FVector(P.X, P.Y, P.Z), FVector(P.ScaleX, P.ScaleY, P.ScaleZ)));
		if (Layer.bParticleMaterial)
			Part->SetCustomPrimitiveDataVector4(0, FVector4(P.R, P.G, P.B, P.A) / 255.f);
		if (!Part->IsVisible()) Part->SetVisibility(true);
	}
	for (FLayer& Layer : Layers)
	{
		for (int32 i = Layer.Next; i < Layer.Visible; ++i) Layer.Pool[i]->SetVisibility(false);
		Layer.Visible = Layer.Next;
	}
}

void FScctPlacedMeshParticles::SetPlace(int32 OwnerId, const FTransform& Frame) const
{
	if (!LayersByOwner.Contains(OwnerId)) return;
	const FVector P = Frame.GetTranslation();
	const FRotator R = Frame.Rotator();
	Api->Place(OwnerId, P.X, P.Y, P.Z, R.Pitch, R.Yaw, R.Roll);
}

int32 FScctPlacedMeshParticles::Trigger(int32 OwnerId) const
{
	const int32 Reached = Count(OwnerId);
	if (Reached) Api->Trigger(OwnerId);
	return Reached;
}

void FScctPlacedMeshParticles::Reset()
{
	if (Host.IsValid()) Host->Destroy();
	Host.Reset();
	Layers.Reset();
	LayersByOwner.Reset();
	Buffer.Reset();
	Api = nullptr;
}
