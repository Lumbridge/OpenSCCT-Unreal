#include "Play/ScctStudioProp.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Play/ScctLevelStateSubsystem.h"
#include "Runtime/ScctBridgeApi.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctWorldApi.h"

struct FScctStudioBone
{
	int32 NameId, Channels;
	FScctBridgeBone Transform;
};
static_assert(sizeof(FScctStudioBone) == 48);
struct FScctStudioPropApi
{
	int32 Version;
	int32 (__cdecl* Read)(int32 Actor, int32 Mesh, int32 Sequence, float Frame, float Duration, FScctStudioBone* Out, int32 Capacity);
};
template<> struct TScctFeatureInfo<FScctStudioPropApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.StudioPropExports"); }
	static constexpr int32 Version = 1;
};

namespace
{
	const FName DriverTag(TEXT("ScctStudioPoseDriver"));
	UPoseableMeshComponent* Driver(const USkeletalMeshComponent& Mesh)
	{
		auto* Pose = Cast<UPoseableMeshComponent>(Mesh.LeaderPoseComponent.Get());
		return Pose && Pose->ComponentHasTag(DriverTag) ? Pose : nullptr;
	}
}

void ScctStudioProp::Restore(USkeletalMeshComponent& Mesh)
{
	if (auto* Pose = Driver(Mesh))
	{
		Mesh.SetLeaderPoseComponent(nullptr);
		Pose->DestroyComponent();
	}
}

bool ScctStudioProp::Apply(USkeletalMeshComponent& Mesh, const FScctWorldAnimated& Record, float OriginalDuration, UScctLevelStateSubsystem& Level)
{
	const auto* Api = ScctFeature<FScctStudioPropApi>();
	if (!Api || !Mesh.GetSkinnedAsset()) return false;
	TArray<FScctStudioBone, TInlineAllocator<128>> Bones;
	Bones.SetNumUninitialized(128);
	int32 Count = Api->Read(Record.NameId, Record.MeshId, Record.SequenceId, Record.Frame, OriginalDuration, Bones.GetData(), Bones.Num());
	if (Count > Bones.Num())
	{
		Bones.SetNumUninitialized(Count);
		Count = Api->Read(Record.NameId, Record.MeshId, Record.SequenceId, Record.Frame, OriginalDuration, Bones.GetData(), Bones.Num());
	}
	if (Count <= 0 || Count > Bones.Num())
	{
		Restore(Mesh);
		return false;
	}
	auto* Pose = Driver(Mesh);
	if (!Pose)
	{
		Pose = NewObject<UPoseableMeshComponent>(Mesh.GetOwner(), NAME_None, RF_Transient);
		Pose->ComponentTags.Add(DriverTag);
		Pose->SetSkinnedAssetAndUpdate(Mesh.GetSkinnedAsset());
		Pose->SetVisibility(false);
		Pose->SetCastShadow(false);
		Pose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pose->SetWorldTransform(Mesh.GetComponentTransform());
		Pose->RegisterComponent();
		Mesh.SetLeaderPoseComponent(Pose);
	}
	Pose->BoneSpaceTransforms = Mesh.GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
	for (int32 i = 0; i < Count; ++i)
	{
		const auto& Bone = Bones[i];
		const FString& Name = Level.GetName(Bone.NameId);
		int32 Index = Mesh.GetBoneIndex(FName(*Name));
		if (Index == INDEX_NONE)
		{
			// The skeletal importer converts spaces in a source bone name to dashes.
			Index = Mesh.GetBoneIndex(FName(*Name.TrimStartAndEnd().Replace(TEXT(" "), TEXT("-"))));
		}
		if (!Pose->BoneSpaceTransforms.IsValidIndex(Index)) continue;
		auto& Local = Pose->BoneSpaceTransforms[Index];
		const FTransform Sample = FScctPose::ToUnreal(Bone.Transform);
		if (Bone.Channels & 1) Local.SetLocation(Sample.GetLocation());
		if (Bone.Channels & 2) Local.SetRotation(Sample.GetRotation());
		if (Bone.Channels & 4) Local.SetScale3D(Sample.GetScale3D());
	}
	Pose->MarkRefreshTransformDirty();
	Pose->RefreshBoneTransforms();
	return true;
}
