#pragma once

#include "CoreMinimal.h"

class UPoseableMeshComponent;

/**
 * Two-bone IK on a posed mesh: bends an arm the bridge has already posed so its hand lands somewhere else. The
 * bridge's poses are those of the original animations; a model drawn in place of the original gun has its grip
 * in another place, and this is what closes the hand on it.
 */
namespace FScctArmIk
{
	/**
	 * Moves the hand bone of Mesh to Hand (in the world: place and rotation; its scale is kept) by turning the
	 * upper arm and forearm, the elbow bending towards ElbowHint (in the world). The forearm must be the upper
	 * arm's child and the hand the forearm's. Call it after the frame's pose is on the mesh; it refreshes the
	 * mesh's bones itself. False, changing nothing, when the three bones are not such a chain.
	 */
	OPENSCCT_API bool Reach(UPoseableMeshComponent& Mesh, int32 UpperArmBone, int32 ForearmBone, int32 HandBone, const FTransform& Hand, const FVector& ElbowHint);
}
