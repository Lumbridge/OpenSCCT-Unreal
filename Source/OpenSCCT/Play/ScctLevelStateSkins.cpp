#include "Play/ScctLevelStateSubsystem.h"

#include "Dom/JsonObject.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Online/ScctMapFolders.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctMenuApi.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const FJsonObject* Child(const FJsonObject* Obj, const FString& Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Obj && Obj->TryGetObjectField(Key, Out) ? Out->Get() : nullptr;
	}

	FString TextField(const FJsonObject* Obj, const TCHAR* Key)
	{
		FString Value;
		if (Obj) Obj->TryGetStringField(Key, Value);
		return Value;
	}

	bool Flag(const FJsonObject* Obj, const TCHAR* Key)
	{
		bool Value = false;
		return Obj && Obj->TryGetBoolField(Key, Value) && Value;
	}

	UTexture2D* SkinPicture(const FString& Folder, const FString& Relative)
	{
		if (Relative.IsEmpty() || !FPaths::IsRelative(Relative)) return nullptr;
		FString Path = FPaths::ConvertRelativePathToFull(Folder / Relative);
		if (!FPaths::CollapseRelativeDirectories(Path) || !FPaths::IsUnderDirectory(Path, Folder)) return nullptr;
		return FImageUtils::ImportFileAsTexture2D(Path);
	}
}

void UScctLevelStateSubsystem::ApplyMapMaterials(FActorState& State)
{
	UStaticMeshComponent* Component = State.Mesh.Get();
	if (!Component || MapMaterialAssets.IsEmpty()) return;
	for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Source = Component->GetMaterial(Slot);
		if (!Source || !Source->GetOutermost()->GetName().StartsWith(TEXT("/Game/Imported/Fx/SGameplayObjects/Materials/"))) continue;
		const FSoftObjectPath* Path = MapMaterialAssets.Find(Source->GetFName());
		if (UMaterialInterface* Material = Path ? Cast<UMaterialInterface>(Path->TryLoad()) : nullptr)
		{
			UMaterialInterface* Original = Component->OverrideMaterials.IsValidIndex(Slot) ? Component->OverrideMaterials[Slot].Get() : nullptr;
			State.MapMaterials.Add({Slot, Original, Material});
			Component->SetMaterial(Slot, Material);
			if (bLog)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Level state: local prop material %s on '%s' slot %d"),
					*Material->GetName(), *GetName(State.Info.NameId), Slot);
			}
		}
	}
}

void UScctLevelStateSubsystem::RestoreMapMaterials(FActorState& State)
{
	if (UStaticMeshComponent* Component = State.Mesh.Get())
	{
		for (const FMapMaterialOverride& Entry : State.MapMaterials)
		{
			if (Component->GetMaterial(Entry.Slot) == Entry.Replacement.Get())
			{
				Component->SetMaterial(Entry.Slot, Entry.Original.Get());
			}
		}
	}
	State.MapMaterials.Reset();
}

UTexture2D* UScctLevelStateSubsystem::GameplaySkinPicture(const FString& Folder, const FString& Relative) const
{
	if (!MapColorAssets.IsEmpty() && !Relative.IsEmpty() && FPaths::IsRelative(Relative))
	{
		if (const FSoftObjectPath* Path = MapColorAssets.Find(FName(*FPaths::GetBaseFilename(Relative))))
		{
			if (UTexture2D* Picture = Cast<UTexture2D>(Path->TryLoad())) return Picture;
		}
	}
	return SkinPicture(Folder, Relative);
}

void UScctLevelStateSubsystem::ApplyHackPanelDisplay(FActorState& State)
{
	// The original switch's second section is a permanent self-lit combiner, independent of
	// the switch's usable/hacking state. Cooked and loose maps both bind their actors here.
	UStaticMeshComponent* Component = State.Mesh.Get();
	if (!Component || !Component->GetStaticMesh() || !Session ||
		!GetName(State.MeshId).Equals(TEXT("SGameplayObjects.SGPO_SM.trigger_hack"), ESearchCase::IgnoreCase)) return;

	if (!bTriedHackPanelDisplay)
	{
		bTriedHackPanelDisplay = true;
		const FString Folder = ScctMapFolders::FolderOf(Session->GetSessionInfo().Map) / TEXT("render");
		FString Json;
		TSharedPtr<FJsonObject> Doc;
		if (!FFileHelper::LoadFileToString(Json, *(Folder / TEXT("materials.json"))) ||
			!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Doc)) return;
		const FJsonObject* Table = Child(Doc.Get(), TEXT("materials"));
		if (!Table) return;
		for (const auto& Pair : Table->Values)
		{
			const auto Entry = Pair.Value->AsObject();
			if (!TextField(Entry.Get(), TEXT("source")).Equals(TEXT("SGameplayObjectsTex.SGPO.triggers_hack_fx_heat"), ESearchCase::IgnoreCase)) continue;
			const FJsonObject* Combine = Child(Child(Entry.Get(), TEXT("flags")), TEXT("combine"));
			if (!Combine || Combine->GetIntegerField(TEXT("op")) != 4) continue;
			UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Materials/M_ScctHackPanel.M_ScctHackPanel"));
			if (!Parent) return;
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
			for (int32 Layer = 1; Layer <= 2; ++Layer)
			{
				const FString Suffix = FString::FromInt(Layer);
				const FJsonObject* Anim = Child(Combine, TEXT("anim") + Suffix);
				const TArray<TSharedPtr<FJsonValue>>* Direction = nullptr;
				double Rate = 0;
				if (!Anim || TextField(Anim, TEXT("type")) != TEXT("panner") ||
					!Anim->TryGetArrayField(TEXT("direction"), Direction) || Direction->Num() < 2 ||
					!Anim->TryGetNumberField(TEXT("rate"), Rate)) return;
				UTexture2D* LayerTexture = SkinPicture(Folder, TextField(Combine, *(TEXT("tex") + Suffix)));
				if (!LayerTexture) return;
				const double Yaw = (*Direction)[1]->AsNumber() * (2.0 * PI / 65536.0);
				Mid->SetTextureParameterValue(FName(*(TEXT("Layer") + Suffix)), LayerTexture);
				Mid->SetVectorParameterValue(FName(*(TEXT("Pan") + Suffix)), FLinearColor(Rate * FMath::Cos(Yaw), Rate * FMath::Sin(Yaw), 0, 0));
			}
			HackPanelDisplay = Mid;
			break;
		}
	}
	if (HackPanelDisplay)
	{
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			if (SourceSlot(Component->GetStaticMesh(), Slot) == 1) Component->SetMaterial(Slot, HackPanelDisplay);
		}
	}
}

UMaterialInterface* UScctLevelStateSubsystem::GameplaySkin(const FString& Name)
{
	if (GameplayMaterialFolder.IsEmpty())
	{
		FString Shared = FOpenSCCTModule::FindContentRoot() / TEXT("shared");
		if (const auto* Api = ScctFeature<FScctMenuApi>())
		{
			TCHAR Buffer[2048] = {};
			if (Api->GetText(static_cast<int32>(EScctMenuText::SharedRoot), Buffer, UE_ARRAY_COUNT(Buffer)) > 0) Shared = Buffer;
		}
		GameplayMaterialFolder = FPaths::ConvertRelativePathToFull(Shared / TEXT("render/gameplay"));
		FString Json;
		if (FFileHelper::LoadFileToString(Json, *(GameplayMaterialFolder / TEXT("materials.json"))))
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), GameplayMaterials);
	}

	const bool bGhost = Name.Equals(TEXT("SGameplayObjectsTex.Object.FX_pose_bomb_heat"), ESearchCase::IgnoreCase);
	const auto* Table = Child(GameplayMaterials.Get(), TEXT("skins"));
	FString MaterialName;
	if (Table)
	{
		for (const auto& Pair : Table->Values)
		{
			if (Pair.Key.ToView().Equals(Name, ESearchCase::IgnoreCase) && Pair.Value->TryGetString(MaterialName)) break;
		}
	}
	const auto* Flags = Child(Child(Child(GameplayMaterials.Get(), TEXT("materials")), MaterialName), TEXT("flags"));
	if (!Flags && !bGhost) return nullptr;

	const FString Folder = bGhost ? FPaths::GetPath(GameplayMaterialFolder) : GameplayMaterialFolder;
	UTexture2D* Base = nullptr;
	if (bGhost)
	{
		if (BombGhost) return BombGhost;
		BombFrames.Reset();
		for (const TCHAR* Frame : {TEXT("textures/SGameplayObjectsTex_FX_pose_bomb.png"), TEXT("textures/SGameplayObjectsTex_FX_pose_bomb_2.png")})
		{
			if (auto* Texture = SkinPicture(Folder, Frame)) BombFrames.Add(Texture);
		}
		if (BombFrames.Num()) Base = BombFrames[0];
	}
	else
	{
		Base = GameplaySkinPicture(Folder, TextField(Flags, TEXT("texture")));
	}
	if (!Base) return nullptr;
	const TCHAR* Blend = bGhost || Flag(Flags, TEXT("translucent")) ? TEXT("Translucent") : Flag(Flags, TEXT("masked")) ? TEXT("Masked") : TEXT("Opaque");
	const FString SelfLit = TextField(Flags, TEXT("self_illum"));
	const FString SelfLitMask = TextField(Flags, TEXT("self_illum_mask"));
	const bool bMaskedGlow = !SelfLit.IsEmpty() && !SelfLitMask.IsEmpty();
	const bool bPackedUnlit = Flag(Flags, TEXT("unlit"));
	const bool bUnlit = bGhost || (bPackedUnlit && (!bMaskedGlow || MaterialName.EndsWith(TEXT("__unlit"))));
	const FString Asset = FString::Printf(TEXT("MI_ScctRuntime_%s_%s%s"), Blend, bUnlit ? TEXT("Unlit") : TEXT("Lit"),
		bGhost || Flag(Flags, TEXT("two_sided")) ? TEXT("_TwoSided") : TEXT(""));
	auto* Parent = LoadObject<UMaterialInterface>(nullptr, *(TEXT("/Game/OpenSCCT/RuntimeMap/") + Asset + TEXT(".") + Asset));
	if (!Parent) return nullptr;
	auto* Material = UMaterialInstanceDynamic::Create(Parent, this);
	Material->SetTextureParameterValue(TEXT("BaseColor"), Base);
	Material->SetScalarParameterValue(TEXT("Emissive"), bUnlit ? 1.f : 0.f);
	if (UTexture2D* Glow = GameplaySkinPicture(Folder, SelfLit))
	{
		UTexture2D* Mask = bMaskedGlow ? SkinPicture(Folder, SelfLitMask) : nullptr;
		if (!bMaskedGlow || Mask)
		{
			Material->SetTextureParameterValue(TEXT("SelfIllumTexture"), Glow);
			Material->SetScalarParameterValue(TEXT("SelfIllum"), bPackedUnlit && bMaskedGlow ? 0.9f : 2.f);
			if (Mask) Material->SetTextureParameterValue(TEXT("SelfIllumMask"), Mask);
		}
	}
	if (bGhost) BombGhost = Material;
	return Material;
}
