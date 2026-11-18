#include "ScctAuthoredMapImport.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Play/ScctAuthoredMaterial.h"

FString UScctAuthoredMapImport::ApplyDefinition(UMaterialInstanceConstant* Instance, const FString& Json,
	const FString& Lightmap, const FString& RenderFolder, const FString& TextureFolder)
{
	return ScctAuthored::BindImported(Instance, Json, Lightmap, RenderFolder, TextureFolder, Textures);
}
