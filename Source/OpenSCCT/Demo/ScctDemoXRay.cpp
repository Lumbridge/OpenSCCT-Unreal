#include "Demo/ScctDemoXRay.h"

#include "Components/PoseableMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"

namespace
{
	const TCHAR* const ScctDemoXRayMaterialPath = TEXT("/Game/OpenSCCT/Demo/M_ScctDemoXRay.M_ScctDemoXRay");

	/** HudK.SpyColour and HudK.MercColour of the Godot client, as drawn over the finished picture. */
	const FLinearColor ScctDemoXRaySpyColour(200.0f / 255.0f, 247.0f / 255.0f, 184.0f / 255.0f, 1.0f);
	const FLinearColor ScctDemoXRayMercColour(1.0f, 182.0f / 255.0f, 68.0f / 255.0f, 1.0f);

	void ScctDemoXRayMark(UPrimitiveComponent& Primitive, bool bMark, int32 Stencil)
	{
		if (Primitive.bRenderCustomDepth != bMark)
		{
			Primitive.SetRenderCustomDepth(bMark);
		}
		if (Primitive.CustomDepthStencilValue != Stencil)
		{
			Primitive.SetCustomDepthStencilValue(Stencil);
		}
	}
}

bool FScctDemoXRay::EnsureVolume(UWorld* World)
{
	if (Volume.IsValid() && VolumeWorld.Get() == World)
	{
		return true;
	}
	if (bMaterialMissing || World == nullptr)
	{
		return false;
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ScctDemoXRayMaterialPath);
	if (Base == nullptr)
	{
		bMaterialMissing = true;
		UE_LOG(LogOpenSCCT, Warning, TEXT("Demo: %s is missing; run Tools/Materials/Build-DemoXRayMaterial.ps1. X-ray draws no outlines."), ScctDemoXRayMaterialPath);
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags |= RF_Transient;
	APostProcessVolume* Spawned = World->SpawnActor<APostProcessVolume>(Parameters);
	if (!IsValid(Spawned))
	{
		return false;
	}
	Spawned->bUnbound = true;
	Spawned->BlendWeight = 1.0f;
	Spawned->Priority = 900.0f;
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, Spawned);
	Instance->SetVectorParameterValue(TEXT("SpyColour"), ScctDemoXRaySpyColour);
	Instance->SetVectorParameterValue(TEXT("MercColour"), ScctDemoXRayMercColour);
	Instance->SetScalarParameterValue(TEXT("Amount"), 1.0f);
	// Weight 0 keeps the material out of the frame until X-ray is on.
	Spawned->Settings.WeightedBlendables.Array.Emplace(0.0f, Instance);
	Volume = Spawned;
	Material = Instance;
	VolumeWorld = World;
	bShown = false;
	return true;
}

void FScctDemoXRay::Update(UScctSessionSubsystem* Session, bool bOn)
{
	if (!bOn || Session == nullptr)
	{
		Release();
		return;
	}
	if (!EnsureVolume(Session->GetWorld()))
	{
		return;
	}

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Marked;
	int32 Spies = 0;
	for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Session->GetProxies())
	{
		const AScctPawnProxy* Proxy = Pair.Value;
		UPoseableMeshComponent* Body = IsValid(Proxy) ? Proxy->GetBodyMesh() : nullptr;
		if (Body == nullptr)
		{
			continue;
		}
		// DemoXRayView's rule: a living, drawn body; not one seen from its own eyes (drawn for its shadow only).
		const bool bOutline = Proxy->IsShown() && Proxy->GetPawn().Health > 0 && !Proxy->IsFirstPerson() && Body->IsVisible();
		if (!bOutline)
		{
			continue;
		}
		// Team 1 is the mercs' (with no team, a merc pawn): the bridge's TeamOf already falls back that way.
		const bool bMerc = Proxy->GetTeam() == 1;
		ScctDemoXRayMark(*Body, true, bMerc ? MercStencil : SpyStencil);
		Marked.Add(Body);
		Spies += bMerc ? 0 : 1;
	}

	// Bodies outlined last frame and not now (died, seen from the eyes) are given back.
	for (const TWeakObjectPtr<UPrimitiveComponent>& Was : MarkedBodies)
	{
		UPrimitiveComponent* Primitive = Was.Get();
		if (Primitive != nullptr && !Marked.Contains(Was))
		{
			ScctDemoXRayMark(*Primitive, false, 0);
		}
	}
	MarkedBodies = MoveTemp(Marked);

	if (!bShown && Volume.IsValid() && Material.IsValid())
	{
		for (FWeightedBlendable& Blendable : Volume->Settings.WeightedBlendables.Array)
		{
			Blendable.Weight = 1.0f;
		}
		bShown = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: X-ray on"));
	}
	if (MarkedBodies.Num() != LastMarked || Spies != LastSpies)
	{
		LastMarked = MarkedBodies.Num();
		LastSpies = Spies;
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: X-ray outlines %d players (%d spy team, %d merc team)"), LastMarked, Spies, LastMarked - Spies);
	}
}

void FScctDemoXRay::Release()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& Was : MarkedBodies)
	{
		if (UPrimitiveComponent* Primitive = Was.Get())
		{
			ScctDemoXRayMark(*Primitive, false, 0);
		}
	}
	MarkedBodies.Reset();
	if (bShown && Volume.IsValid())
	{
		for (FWeightedBlendable& Blendable : Volume->Settings.WeightedBlendables.Array)
		{
			Blendable.Weight = 0.0f;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: X-ray off"));
	}
	bShown = false;
	LastMarked = -1;
	LastSpies = -1;
}
