#include "Play/ScctEffectSkins.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctEventsApi.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* const EffectSkinMaterialPath = TEXT("/Game/OpenSCCT/Effects/M_ScctFxSkin.M_ScctFxSkin");

	FString EffectSkinName(const FScctEventsApi* Events, int32 Id)
	{
		TCHAR Buffer[256];
		const int32 Length = Id != 0 && Events != nullptr ? Events->GetName(Id, Buffer, UE_ARRAY_COUNT(Buffer)) : 0;
		return Length > 0 ? FString(Buffer) : FString();
	}
}

void FScctEffectSkins::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Base);
	for (TPair<int32, FSkin>& Pair : Skins)
	{
		Collector.AddReferencedObject(Pair.Value.Material);
		Collector.AddReferencedObject(Pair.Value.Picture);
	}
}

void FScctEffectSkins::Clear(UStaticMeshComponent* Component)
{
	if (Component != nullptr)
	{
		Component->EmptyOverrideMaterials();
		if (Component->IsForceDisableNanite())
		{
			Component->SetForceDisableNanite(false);
		}
	}
}

const FScctEffectSkinApi* FScctEffectSkins::FindApi()
{
	if (!bApiTried)
	{
		bApiTried = true;
		FString Error;
		Api = ScctFeature<FScctEffectSkinApi>(&Error);
		if (Api == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: the effect meshes keep their own materials (no mine status light), the bridge's effect skins are missing: %s"), *Error);
		}
	}
	return Api;
}

void FScctEffectSkins::RunCheck(float MatchSeconds, const FVector& Eye, const FVector& Forward)
{
	if (CheckStep < 0)
	{
		CheckStep = FParse::Param(FCommandLine::Get(), TEXT("ScctMineLight")) ? 1 : 0;
	}
	if (CheckStep == 0 || CheckStep > 2 || MatchSeconds < (CheckStep == 1 ? 2.0f : 10.0f) || FindApi() == nullptr)
	{
		return;
	}
	if (CheckStep == 1)
	{
		const int32 Queued = Api->PlaceCheckMines(Eye.X, Eye.Y, Eye.Z, Forward.X, Forward.Y, Forward.Z);
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: -ScctMineLight, two proximity mines ahead of (%.0f, %.0f, %.0f): %s"), Eye.X, Eye.Y, Eye.Z,
			Queued != 0 ? TEXT("queued") : TEXT("NOT placed (no local server)"));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: -ScctMineLight, the first mine chaffed for 30 s: %s"), Api->ChaffCheckMine(30.0f) != 0 ? TEXT("queued") : TEXT("NOT done"));
	}
	++CheckStep;
}

void FScctEffectSkins::Apply(int32 ActorId, UStaticMeshComponent* Component, FWorn& Worn, const FScctEventsApi* Events, bool bLog)
{
	if (FindApi() == nullptr || Component == nullptr)
	{
		return;
	}

	FScctEffectSkinSlot Slots[ScctEffectSkinSlots] = {};
	const int32 Count = FMath::Min(Api->GetActorSkins(ActorId, Slots, ScctEffectSkinSlots), ScctEffectSkinSlots);
	const int32 NumSlots = FMath::Min(Component->GetNumMaterials(), ScctEffectSkinSlots);
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		const int32 Wanted = Slot < Count ? Slots[Slot].SkinId : 0;
		FSkin* Skin = Wanted != 0 ? FindSkin(Wanted, Events) : nullptr;
		// A skin that cannot be drawn leaves the mesh's own material, as a slot without one.
		const int32 Drawn = Skin != nullptr && Skin->Material != nullptr ? Wanted : 0;
		if (Drawn != 0 && Skin->Frame != Slots[Slot].Frame)
		{
			// One material for every mesh that wears the skin: the cell comes from the level's clock, the same for all.
			Skin->Frame = Slots[Slot].Frame;
			const int32 Cell = FMath::Max(Skin->Frame, 0);
			Skin->Material->SetVectorParameterValue(TEXT("Cell"), FLinearColor(1.0f / Skin->Columns, 1.0f / Skin->Rows,
				static_cast<float>(Cell % Skin->Columns) / Skin->Columns, static_cast<float>(Cell / Skin->Columns) / Skin->Rows));
		}
		if (Drawn == Worn.SkinIds[Slot])
		{
			continue;
		}

		Worn.SkinIds[Slot] = Drawn;
		// Null is the mesh's own material again.
		Component->SetMaterial(Slot, Drawn != 0 ? Skin->Material.Get() : nullptr);
		if (Drawn != 0)
		{
			++NumApplied;
			// Nanite draws no added material: a mesh that wears one is drawn the ordinary way, as the level's are.
			if (!Component->IsForceDisableNanite())
			{
				Component->SetForceDisableNanite(true);
			}
		}
		if (bLog)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Effects: actor %d wears skin '%s' in slot %d"), ActorId, Drawn != 0 ? *EffectSkinName(Events, Drawn) : TEXT("none"), Slot);
		}
	}
}

FScctEffectSkins::FSkin* FScctEffectSkins::FindSkin(int32 SkinId, const FScctEventsApi* Events)
{
	if (FSkin* Known = Skins.Find(SkinId))
	{
		return Known;
	}

	FSkin& Skin = Skins.Add(SkinId);
	if (!bBaseTried)
	{
		bBaseTried = true;
		Base = LoadObject<UMaterialInterface>(nullptr, EffectSkinMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Base == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Effects: M_ScctFxSkin is missing (run Tools/Effects/Build-EffectAssets.ps1); the effect meshes keep their own materials"));
		}
	}

	FScctEffectSkin Info = {};
	if (Base == nullptr || Api->GetSkin(SkinId, &Info) == 0)
	{
		return &Skin;
	}
	const FString File = FOpenSCCTModule::FindContentRoot() / EffectSkinName(Events, Info.FileId);
	Skin.Picture = FPaths::FileExists(File) ? FImageUtils::ImportFileAsTexture2D(File) : nullptr;
	if (Skin.Picture == nullptr)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Effects: skin '%s' is not drawn, its picture was not read from %s"), *EffectSkinName(Events, SkinId), *File);
		return &Skin;
	}

	Skin.Columns = FMath::Max(Info.Columns, 1);
	Skin.Rows = FMath::Max(Info.Rows, 1);
	Skin.Material = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	Skin.Material->SetTextureParameterValue(TEXT("Texture"), Skin.Picture);
	Skin.Material->SetVectorParameterValue(TEXT("Cell"), FLinearColor(1.0f / Skin.Columns, 1.0f / Skin.Rows, 0.0f, 0.0f));
	return &Skin;
}
