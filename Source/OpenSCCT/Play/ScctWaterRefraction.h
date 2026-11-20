#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class ULevel;
namespace GLTF { struct FAsset; }

namespace ScctWaterRefraction
{
	bool IsGuide(const FString& Name);
	bool NeedsPreparation(const ULevel& Level);
	int32 Build(ULevel& Level, const FString& RenderFolder, const FJsonObject& Gltf, const FJsonObject& Lighting, const GLTF::FAsset& Geometry);
}
