#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

namespace ScctCosmeticMaterial
{
	UMaterialInstanceDynamic* Create(UObject* Outer, UMaterialInterface* Source, UTexture2D* Colour, const FString& Pattern, const FString& Path);
}
