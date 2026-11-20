#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;
class USkinnedAsset;
class UAnimSequence;

/** Reads the converted skinned GLBs from the active Steam build or a selected map. */
namespace ScctRuntimeSkins
{
	USkeletalMesh* Load(const FString& File);
	UStaticMesh* LoadEffectMesh(const FString& ObjectPath);
	void LoadEffectMeshes(TArray<UStaticMesh*>& Out);
	FString SourceOf(const USkinnedAsset* Mesh);
	UMaterialInterface* LoadEquipmentSkin(const FString& Package, const FString& Texture);
	UAnimSequence* LoadAnimation(USkeletalMesh* Mesh, const FString& Name);
}
