#include "Hud/ScctVisionBackdrop.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "OpenSCCT.h"

namespace
{
	const TCHAR* const CoronaTagPrefix = TEXT("ScctCoronaOf:");
	const FName BackdropTag(TEXT("ScctBackdrop"));
	// Only the glow card material (the beam and flare cards) has this parameter.
	const FName GlowCardParameter(TEXT("EdgeFade"));
}

void FScctVisionBackdrop::IndexLevel(UWorld* World)
{
	IndexedWorld = World;
	Coronas.Reset();
	Sky.Reset();

	int32 NumCards = 0;
	int32 NumGlowCards = 0;
	int32 NumDomes = 0;
	int32 NumSkyline = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		bool bCorona = false;
		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().StartsWith(CoronaTagPrefix))
			{
				bCorona = true;
				break;
			}
		}
		const bool bSkyline = Actor->ActorHasTag(BackdropTag);

		Actor->ForEachComponent<UStaticMeshComponent>(false, [&](UStaticMeshComponent* Mesh)
		{
			if (bCorona)
			{
				Coronas.Add(Mesh);
				++NumCards;
				return;
			}
			if (bSkyline)
			{
				Sky.Add(Mesh);
				++NumSkyline;
				return;
			}
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				const UMaterialInterface* Material = Mesh->GetMaterial(Slot);
				if (Material == nullptr)
				{
					continue;
				}
				const UMaterial* Base = Material->GetMaterial();
				if (Base != nullptr && Base->bIsSky)
				{
					Sky.Add(Mesh);
					++NumDomes;
					return;
				}
				float Edge = 0.0f;
				if (Material->GetScalarParameterValue(FHashedMaterialParameterInfo(GlowCardParameter), Edge))
				{
					Coronas.Add(Mesh);
					++NumGlowCards;
					return;
				}
			}
		});
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Vision: thermal and EMF hide %d corona cards, %d beam or flare cards, %d sky domes and %d skyline meshes on this map"),
		NumCards, NumGlowCards, NumDomes, NumSkyline);
}

void FScctVisionBackdrop::Update(UWorld* World, bool bHide)
{
	if (World == nullptr)
	{
		return;
	}
	if (IndexedWorld.Get() != World)
	{
		Reset();
		if (!bHide)
		{
			return;
		}
		// Read on first use, once the level state has bound its coronas and the stand-ins are in.
		IndexLevel(World);
	}
	if (bHide == bHidden)
	{
		return;
	}
	bHidden = bHide;

	if (!bHide)
	{
		int32 NumShown = 0;
		for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : Taken)
		{
			if (UPrimitiveComponent* Primitive = Weak.Get())
			{
				Primitive->SetVisibility(true);
				++NumShown;
			}
		}
		Taken.Reset();
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: gave back %d coronas and sky meshes"), NumShown);
		return;
	}

	// Only what is visible now is taken, so what was hidden before stays hidden when it is given back.
	auto Hide = [this](const TArray<TWeakObjectPtr<UPrimitiveComponent>>& List)
	{
		int32 Count = 0;
		for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : List)
		{
			UPrimitiveComponent* Primitive = Weak.Get();
			if (Primitive != nullptr && Primitive->GetVisibleFlag())
			{
				Primitive->SetVisibility(false);
				Taken.Add(Weak);
				++Count;
			}
		}
		return Count;
	};
	const int32 NumCoronas = Hide(Coronas);
	const int32 NumSky = Hide(Sky);
	UE_LOG(LogOpenSCCT, Log, TEXT("Vision: hid %d coronas, %d sky actors"), NumCoronas, NumSky);
}

void FScctVisionBackdrop::Reset()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : Taken)
	{
		if (UPrimitiveComponent* Primitive = Weak.Get())
		{
			Primitive->SetVisibility(true);
		}
	}
	Taken.Reset();
	Coronas.Reset();
	Sky.Reset();
	IndexedWorld.Reset();
	bHidden = false;
}
