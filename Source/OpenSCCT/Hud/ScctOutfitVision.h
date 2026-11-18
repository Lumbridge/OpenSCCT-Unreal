#pragma once

#include "CoreMinimal.h"

class UPoseableMeshComponent;
class USkeletalMeshComponent;
class USkinnedAsset;
class UMaterialInstanceDynamic;
class UTexture2D;

class FScctOutfitVision
{
public:
	bool Mark(UPoseableMeshComponent& Body, int32 Kind, bool bWet, bool bLocalSpy, bool bHeat, float Intensity, float Opacity);
	void EndFrame();
	void Reset();

private:
	struct FBody
	{
		TWeakObjectPtr<USkinnedAsset> Mesh;
		TWeakObjectPtr<AActor> Actor;
		TArray<TWeakObjectPtr<USkeletalMeshComponent>> Bands;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
		int32 Skin = INDEX_NONE;
		float Intensity = -1.f;
		float Opacity = -1.f;
		bool bHeat = false;
		bool bUsed = false;
	};
	TMap<TWeakObjectPtr<UPoseableMeshComponent>, FBody> Bodies;
	TMap<FString, TWeakObjectPtr<UTexture2D>> Textures;
};
