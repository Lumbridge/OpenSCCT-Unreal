#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctMeshParticleApi.h"

class AActor;
class UStaticMeshComponent;
class UScctLevelStateSubsystem;

/** Mesh children of the converted map's placed Emitters. Each component keeps the real mesh's section slots;
 * pooling ordinary components also works with cooked map materials that have no instancing permutation. */
class FScctPlacedMeshParticles
{
public:
	void Begin(UScctLevelStateSubsystem& Level);
	void Reset();
	void Draw();
	void SetPlace(int32 OwnerId, const FTransform& Frame) const;
	int32 Trigger(int32 OwnerId) const;
	int32 Count(int32 OwnerId) const { return LayersByOwner.FindRef(OwnerId); }

private:
	struct FLayer
	{
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Pool;
		int32 Visible = 0;
		int32 Next = 0;
		bool bParticleMaterial = false;
	};

	const FScctMeshParticleApi* Api = nullptr;
	TWeakObjectPtr<AActor> Host;
	TMap<int32, int32> LayersByOwner;
	TArray<FLayer> Layers;
	TArray<FScctMeshParticle> Buffer;
};
