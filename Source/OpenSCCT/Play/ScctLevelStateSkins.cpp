#include "Play/ScctLevelStateSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
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
		Base = SkinPicture(Folder, TextField(Flags, TEXT("texture")));
	}
	if (!Base) return nullptr;
	const TCHAR* Blend = bGhost || Flag(Flags, TEXT("translucent")) ? TEXT("Translucent") : Flag(Flags, TEXT("masked")) ? TEXT("Masked") : TEXT("Opaque");
	const bool bUnlit = bGhost || Flag(Flags, TEXT("unlit"));
	const FString Asset = FString::Printf(TEXT("MI_ScctRuntime_%s_%s%s"), Blend, bUnlit ? TEXT("Unlit") : TEXT("Lit"),
		bGhost || Flag(Flags, TEXT("two_sided")) ? TEXT("_TwoSided") : TEXT(""));
	auto* Parent = LoadObject<UMaterialInterface>(nullptr, *(TEXT("/Game/OpenSCCT/RuntimeMap/") + Asset + TEXT(".") + Asset));
	if (!Parent) return nullptr;
	auto* Material = UMaterialInstanceDynamic::Create(Parent, this);
	Material->SetTextureParameterValue(TEXT("BaseColor"), Base);
	Material->SetScalarParameterValue(TEXT("Emissive"), bUnlit ? 1.f : 0.f);
	if (UTexture2D* Glow = SkinPicture(Folder, TextField(Flags, TEXT("self_illum"))))
	{
		Material->SetTextureParameterValue(TEXT("SelfIllumTexture"), Glow);
		Material->SetScalarParameterValue(TEXT("SelfIllum"), 2.f);
		if (auto* Mask = SkinPicture(Folder, TextField(Flags, TEXT("self_illum_mask"))))
			Material->SetTextureParameterValue(TEXT("SelfIllumMask"), Mask);
	}
	if (bGhost) BombGhost = Material;
	return Material;
}
