#include "Play/ScctCosmeticMaterial.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"

UMaterialInstanceDynamic* ScctCosmeticMaterial::Create(UObject* Outer, UMaterialInterface* Source, UTexture2D* Colour,
	const FString& Pattern, const FString& Path)
{
	const bool bGold = Pattern == TEXT("golden") || Pattern == TEXT("mastercraft");
	UMaterialInterface* Finish = nullptr;
	if (bGold || Pattern == TEXT("pyre"))
	{
		Finish = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenSCCT/Materials/M_ScctCosmeticFinish.M_ScctCosmeticFinish"));
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Finish ? Finish : Source, Outer);
	if (Finish)
	{
		Instance->CopyMaterialUniformParameters(Source);
		Instance->SetScalarParameterValue(TEXT("GoldMirror"), bGold ? 1.f : 0.f);
		const FString MaskPath = FPaths::ChangeExtension(Path, TEXT("mask.png"));
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
	Instance->SetTextureParameterValue(TEXT("BaseColor"), Colour);
	return Instance;
}
