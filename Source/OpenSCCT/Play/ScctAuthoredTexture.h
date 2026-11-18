#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace ScctAuthored
{
	struct FTextureBinding
	{
		FString File;
		bool bSRGB = false;
		bool bNormal = false;
		int32 Columns = 1;
		int32 Rows = 1;
	};

	/** One decoder and mip generator for both loose and cooked authored textures. */
	UTexture2D* LoadTexture(const FTextureBinding& Binding, const FString& RenderFolder, const FString& AssetFolder,
		UObject* Outer, TMap<FString, TObjectPtr<UTexture2D>>& Cache, FString& Error);
}
