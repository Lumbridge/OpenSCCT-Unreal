#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Runtime/ScctBridgeApi.h"
#include "ScctPawnProxy.generated.h"

class UPoseableMeshComponent;
class UStaticMeshComponent;

/**
 * The Unreal body of one simulated pawn. The simulation owns where it is and what it does, and the bridge
 * computes its bone pose from the simulation's animation channels; this actor only draws it. Without the
 * imported character models it falls back to a shape the size of the pawn's collision cylinder.
 */
UCLASS()
class OPENSCCT_API AScctPawnProxy : public AActor
{
	GENERATED_BODY()

public:
	AScctPawnProxy();

	/**
	 * Places and poses the body for this frame. Skeleton is the bridge's bone list for the pawn's kind and Pose
	 * its bones this frame (empty before the pawn has one). bHideBody is set for the local player seen through
	 * their own eyes.
	 */
	void Present(const FScctBridgePawn& Pawn, TConstArrayView<FScctBridgeBoneInfo> Skeleton, TConstArrayView<FScctBridgeBone> Pose, bool bHideBody);

private:
	void SetKind(int32 NewKind, TConstArrayView<FScctBridgeBoneInfo> Skeleton);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPoseableMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> StandIn;

	/** For each bone of the bridge's skeleton, the bone of the Unreal mesh, or INDEX_NONE. */
	TArray<int32> BoneMap;
	int32 Kind = INDEX_NONE;
	bool bHasModel = false;
};
