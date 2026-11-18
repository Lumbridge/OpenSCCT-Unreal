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
 *
 * The session subsystem calls Present once a frame, before it raises OnFrame, so everything a feature reads
 * here in OnFrame is this frame's: the pawn record, the pose on the body mesh, the bone lookups.
 */
UCLASS()
class OPENSCCT_API AScctPawnProxy : public AActor
{
	GENERATED_BODY()

public:
	AScctPawnProxy();

	/**
	 * Places and poses the body for this frame. Skeleton is the bridge's bone list for the pawn's kind and Pose
	 * its bones this frame (empty before the pawn has one).
	 */
	void Present(const FScctBridgePawn& Pawn, TConstArrayView<FScctBridgeBoneInfo> Skeleton, TConstArrayView<FScctBridgeBone> Pose);

	/** The pawn as the bridge described it in the last Present: id, kind, team, health, flags, place, aim. */
	const FScctBridgePawn& GetPawn() const { return Pawn; }

	int32 GetActorId() const { return Pawn.ActorId; }

	/** 0 spy, 1 merc. */
	int32 GetKind() const { return Pawn.Kind; }

	/** The pawn's replicated team: 0 spies, 1 mercs. Right for every pawn, other players' included. */
	int32 GetTeam() const { return Pawn.Team; }

	/** The player at this machine. */
	bool IsLocal() const { return (Pawn.Flags & EScctPawnFlags::Local) != 0; }

	/**
	 * Seen from its own eyes this frame: the body is out of the main view and only its shadow is drawn. What is
	 * attached to the body for other players to see (a holstered gun, a backpack) is hidden with it and keeps its
	 * shadow the same way; a first-person view model is a separate thing and is not attached here.
	 */
	bool IsFirstPerson() const { return (Pawn.Flags & EScctPawnFlags::FirstPerson) != 0; }

	/** The simulation draws this pawn at all (not a hidden or dead-and-gone one). */
	bool IsShown() const { return (Pawn.Flags & EScctPawnFlags::Visible) != 0; }

	/** The posed character mesh. It has no model (GetSkinnedAsset is null) until the imported characters exist. */
	UPoseableMeshComponent* GetBodyMesh() const { return Body; }

	/** The imported character model was found and the bridge's skeleton was matched to it. */
	bool HasModel() const { return bHasModel; }

	/**
	 * The bone of the body mesh for each bone of the bridge's skeleton, in the bridge's bone order (the order of
	 * GetSkeleton and GetPawnPose); INDEX_NONE where the mesh has no such bone. Empty without a model.
	 */
	TConstArrayView<int32> GetBoneMap() const { return BoneMap; }

	/** The same by the bridge's bone names (exactly as GetSkeleton spells them), unmatched bones left out. */
	const TMap<FName, int32>& GetBonesByBridgeName() const { return BonesByBridgeName; }

	/**
	 * The body mesh's bone index for a bone named as the bridge names it, or INDEX_NONE. Spelling differences
	 * that importers introduce (spaces, case, punctuation) do not matter.
	 */
	int32 FindMeshBone(const FString& BridgeBoneName) const;

	/** The body mesh's own name for that bone (usable as a socket name), or NAME_None. */
	FName FindMeshBoneName(const FString& BridgeBoneName) const;

	/**
	 * Attaches Component to the body at the bone the bridge calls BridgeBoneName, at RelativeTransform in that
	 * bone's space as the Unreal mesh has it (centimetres; FScctPose::ToUnreal turns a transform of the character
	 * file into it). The component then follows the pose every frame and is hidden and shown with the body.
	 * Returns false, attaching nothing, when the body has no such bone.
	 */
	bool AttachToBone(USceneComponent* Component, const FString& BridgeBoneName, const FTransform& RelativeTransform = FTransform::Identity);

	/**
	 * Thins the body, and what is attached to it, out of the main view close to the camera: whole at Distance cm
	 * from the camera and beyond, gone at half of it, in between a screen door the anti-aliasing blends into an
	 * even transparency (the surface material's NearFade, custom primitive data slot NearFadeSlot;
	 * Tools/Import/scct_materials.py). 0 switches it off. The shadow stays whole. Call it every frame it is on,
	 * because attachments come and go.
	 *
	 * The head (bone "B Head" and what hangs on it, the goggle lenses) goes whole, at once, while its bone is
	 * nearer Eye than HeadGoneFactor times Distance: thinned by depth alone it hung beside the gun as a ghost of
	 * loose shards when the camera was held against a wall. Its shadow goes with it. Never in the first-person
	 * view, where the body is only a shadow and that shadow keeps its head.
	 */
	void SetNearFade(float Distance, const FVector& Eye);
	float GetNearFade() const { return NearFade; }
	bool IsHeadHidden() const { return bHeadHidden; }

	static constexpr float HeadGoneFactor = 1.25f;
	/** How much further than that the head bone must be before the head comes back, cm (no flicker at the edge). */
	static constexpr float HeadBackMargin = 6.0f;

	/** The custom primitive data slot of the near fade (NEAR_FADE_SLOT in Tools/Import/scct_materials.py). */
	static constexpr int32 NearFadeSlot = 2;

private:
	void SetKind(int32 NewKind, TConstArrayView<FScctBridgeBoneInfo> Skeleton);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPoseableMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> StandIn;

	FScctBridgePawn Pawn = {};
	TArray<int32> BoneMap;
	TMap<FName, int32> BonesByBridgeName;

	/** Mesh bone index by the letters and digits of a bone name, for FindMeshBone. */
	TMap<FString, int32> MeshBonesByKey;
	int32 Kind = INDEX_NONE;
	bool bHasModel = false;

	/** A pose has been written to the body (for the play inspector's -ScctTestFreezePose, which keeps that first one). */
	bool bPosedOnce = false;
	TArray<FTransform> FrozenBones;
	float NearFade = 0.0f;
	bool bHeadHidden = false;
};
