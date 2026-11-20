#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;
class UTexture2D;

namespace ScctCosmeticMaterial
{
	/** The original albedo atlas used as the key for loose painted textures in the content pack. */
	UTexture* ColourAtlas(UMaterialInterface* Source);
	UMaterialInstanceDynamic* Create(UObject* Outer, UMaterialInterface* Source, UTexture2D* Colour, const FString& Pattern, const FString& Path);
}
