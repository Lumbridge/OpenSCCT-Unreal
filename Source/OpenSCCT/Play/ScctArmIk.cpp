#include "Play/ScctArmIk.h"

#include "Components/PoseableMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "ReferenceSkeleton.h"
#include "TwoBoneIK.h"

bool FScctArmIk::Reach(UPoseableMeshComponent& Mesh, int32 UpperArmBone, int32 ForearmBone, int32 HandBone, const FTransform& Hand, const FVector& ElbowHint)
{
	const USkinnedAsset* Asset = Mesh.GetSkinnedAsset();
	TArray<FTransform>& Local = Mesh.BoneSpaceTransforms;
	if (Asset == nullptr || !Local.IsValidIndex(UpperArmBone) || !Local.IsValidIndex(ForearmBone) || !Local.IsValidIndex(HandBone))
	{
		return false;
	}
	const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
	if (Skeleton.GetParentIndex(HandBone) != ForearmBone || Skeleton.GetParentIndex(ForearmBone) != UpperArmBone)
	{
		return false;
	}

	// Solved in the world, where the target is. The bones' lengths are taken from where they are now, so a model
	// drawn at any scale keeps its arm.
	const int32 ShoulderBone = Skeleton.GetParentIndex(UpperArmBone);
	const FTransform Shoulder = ShoulderBone != INDEX_NONE ? Mesh.GetBoneTransform(ShoulderBone) : Mesh.GetComponentTransform();
	FTransform Upper = Mesh.GetBoneTransform(UpperArmBone);
	FTransform Fore = Mesh.GetBoneTransform(ForearmBone);
	FTransform End = Mesh.GetBoneTransform(HandBone);
	AnimationCore::SolveTwoBoneIK(Upper, Fore, End, ElbowHint, Hand.GetLocation(), false, 1.0, 1.0);
	// The solver leaves the hand's rotation alone; the hand takes the one asked for.
	End.SetRotation(Hand.GetRotation());

	Local[UpperArmBone] = Upper.GetRelativeTransform(Shoulder);
	Local[ForearmBone] = Fore.GetRelativeTransform(Upper);
	Local[HandBone] = End.GetRelativeTransform(Fore);
	Mesh.RefreshBoneTransforms();
	return true;
}
