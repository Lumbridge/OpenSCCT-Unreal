#pragma once

#include "CoreMinimal.h"
#include "ScctAuthoredMapImport.generated.h"

class UMaterialInstanceConstant;
class UTexture2D;

/** One import session shares decoded texture sets between all the map's material instances. */
UCLASS()
class UScctAuthoredMapImport : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="OpenSCCT|Import")
	FString ApplyDefinition(UMaterialInstanceConstant* Instance, const FString& Json, const FString& Lightmap, const FString& RenderFolder,
		const FString& TextureFolder);

private:
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Textures;
};
