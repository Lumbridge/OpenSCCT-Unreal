#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UTexture2D;

namespace ScctAuthored
{
	using FTextureCache = TMap<FString, TObjectPtr<UTexture2D>>;

	UMaterialInstanceDynamic* MaterialFor(UObject* Outer, const TSharedPtr<FJsonObject>& Flags, const FString& Lightmap,
		const FString& RenderFolder, FTextureCache& Cache, const FString& Name, UMaterialInstanceDynamic* Legacy);

#if WITH_EDITOR
	OPENSCCT_API FString BindImported(UMaterialInstanceConstant* Instance, const FString& Json, const FString& Lightmap, const FString& RenderFolder,
		const FString& TextureFolder, FTextureCache& Cache);
#endif
}
