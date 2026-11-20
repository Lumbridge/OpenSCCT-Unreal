#include "Play/ScctCosmeticMaterial.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"

namespace
{
	const FName BaseColor(TEXT("BaseColor"));
	const FName GltfBaseColor(TEXT("BaseColorTexture"));

	bool UsesRifleGltf(UMaterialInterface* Source)
	{
		if (!Source || !Source->GetMaterial()) return false;
		const FName Name = Source->GetMaterial()->GetFName();
		return Name == TEXT("M_RifleGltf") || Name == TEXT("M_ScctRifleFinish");
	}
}

UTexture* ScctCosmeticMaterial::ColourAtlas(UMaterialInterface* Source)
{
	UTexture* Atlas = nullptr;
	return Source && Source->GetTextureParameterValue(UsesRifleGltf(Source) ? GltfBaseColor : BaseColor, Atlas) ? Atlas : nullptr;
}

UMaterialInstanceDynamic* ScctCosmeticMaterial::Create(UObject* Outer, UMaterialInterface* Source, UTexture2D* Colour,
	const FString& Pattern, const FString& Path)
{
	const bool bGold = Pattern == TEXT("golden") || Pattern == TEXT("mastercraft");
	const bool bRifle = UsesRifleGltf(Source);
	const FString MaskPath = FPaths::ChangeExtension(Path, TEXT("mask.png"));
	UMaterialInterface* Finish = nullptr;
	if ((bGold || Pattern == TEXT("pyre")) && (!bRifle || FPaths::FileExists(MaskPath)))
	{
		Finish = bRifle ? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Materials/M_ScctRifleFinish.M_ScctRifleFinish")) :
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Materials/M_ScctCosmeticFinish.M_ScctCosmeticFinish"));
	}

	// Runtime-loaded meshes already carry dynamic instances. Unreal only accepts
	// a base material or a constant instance as the parent of another instance.
	const auto* RuntimeSource = Cast<UMaterialInstanceDynamic>(Source);
	UMaterialInterface* Parent = RuntimeSource ? RuntimeSource->Parent.Get() : Source;
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Finish ? Finish : Parent, Outer);
	if (RuntimeSource && !Finish) Instance->CopyMaterialUniformParameters(Source);
	if (Finish)
	{
		Instance->CopyMaterialUniformParameters(Source);
		Instance->SetScalarParameterValue(TEXT("GoldMirror"), bGold ? 1.f : 0.f);
		if (bRifle)
		{
			Instance->SetScalarParameterValue(TEXT("Coated"), 1.f);
			UTexture* Packed = nullptr;
			const bool bHasSourceMap = Source->GetTextureParameterValue(TEXT("MetallicRoughnessTexture"), Packed) && Packed &&
				Packed->GetPathName().Contains(TEXT("/Chars/OpenScctRifles/"));
			Instance->SetScalarParameterValue(TEXT("HasSourceORM"), bHasSourceMap ? 1.f : 0.f);
		}
		if (FPaths::FileExists(MaskPath))
		{
			static TMap<FString, TWeakObjectPtr<UTexture2D>> Masks;
			TWeakObjectPtr<UTexture2D>& Cached = Masks.FindOrAdd(MaskPath);
			if (!Cached.IsValid())
			{
				if (UTexture2D* Mask = FImageUtils::ImportFileAsTexture2D(MaskPath))
				{
					Mask->SRGB = false;
					Mask->UpdateResource();
					Cached = Mask;
				}
			}
			if (Cached.IsValid())
			{
				Instance->SetTextureParameterValue(TEXT("FinishMask"), Cached.Get());
			}
		}
	}
	Instance->SetTextureParameterValue(bRifle ? GltfBaseColor : BaseColor, Colour);
	return Instance;
}
