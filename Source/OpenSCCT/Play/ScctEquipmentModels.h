#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctEquipmentApi.h"
#include "UObject/StrongObjectPtr.h"

class UMaterialInterface;
class UPoseableMeshComponent;
class USkeletalMesh;

/**
 * One equipment model as the game draws it: the imported skeletal mesh and how the bridge's skeleton of the same
 * file maps onto it. The two are matched by bone name, because they need not agree in count or order: the
 * bridge lists the nodes a clip moves outside the skin as extra bones after the joints (so a parent can come
 * after its child), and Unreal's importer makes bones of its own choosing out of the same file.
 */
struct FScctEquipmentModel
{
	/** The asset as the simulation names it ("SWeaponFirst.M160"). */
	FString Name;

	/** The package folder of the file under shared/chars ("SWeaponFirst"); skins are looked up beside it. */
	FString Package;

	TStrongObjectPtr<USkeletalMesh> Mesh;

	/**
	 * The other skeletal meshes the importer made of the same file, on the same skeleton (OpenSCCT's first-person
	 * pistols are arms, gun and dropped magazine as separate meshes). They are drawn with Mesh and follow its pose.
	 */
	TArray<TStrongObjectPtr<USkeletalMesh>> ExtraMeshes;

	/** The bridge's bone list, in the order of every pose of this model. */
	TArray<FScctBridgeBoneInfo> Skeleton;

	/** The mesh's bone for each bridge bone, INDEX_NONE where the mesh has none. */
	TArray<int32> BoneMap;

	/** The bridge's bone for each mesh bone, INDEX_NONE where the bridge has none. */
	TArray<int32> BridgeBoneOfMeshBone;

	/**
	 * Every bridge bone has a mesh bone and the parents agree, so a pose is copied bone for bone. Otherwise the
	 * pose is rebuilt through component space (ApplyPose).
	 */
	bool bDirect = false;

	/** Sets the bridge's pose (bones relative to their parents, in Skeleton's order) on a component showing Mesh. */
	void ApplyPose(UPoseableMeshComponent& Component, TConstArrayView<FScctBridgeBone> Pose) const;

	/** The mesh's bone for a bridge bone index, or INDEX_NONE. */
	int32 MeshBone(int32 BridgeBone) const { return BoneMap.IsValidIndex(BridgeBone) ? BoneMap[BridgeBone] : INDEX_NONE; }

	/** The mesh's bone for a bone named as the bridge names it, or INDEX_NONE. */
	int32 MeshBoneByName(const TCHAR* BridgeName) const;
};

/**
 * The equipment models and names of a run: each mesh is loaded and matched on first use and kept, and each of the
 * bridge's name ids is asked for once (an id keeps its text for the life of the process).
 */
class FScctEquipmentModels
{
public:
	/** The text of a name id; empty for id 0. */
	const FString& Name(const FScctEquipmentApi& Api, int32 Id);

	/**
	 * The model of a mesh the bridge names (its asset name id and file path id), or null when the file was never
	 * imported (said once in the log). Override, when set, is drawn in place of the imported file's mesh; it must
	 * have the same skeleton.
	 */
	const FScctEquipmentModel* Find(const FScctEquipmentApi& Api, int32 MeshId, int32 MeshPathId, const FSoftObjectPath* Override = nullptr);

	/** Make a role-specific material from its texture in the locally built equipment package. */
	UMaterialInterface* FindSkin(const FString& Package, const FString& SkinName);

	/** Loads every mesh of the bridge's catalog now, so none hitches a frame later, and logs what is missing. */
	void LoadCatalog(const FScctEquipmentApi& Api);

private:
	TMap<int32, FString> Names;
	TMap<FString, TUniquePtr<FScctEquipmentModel>> Models;
	TMap<FString, TStrongObjectPtr<UMaterialInterface>> Skins;
	bool bCatalogLoaded = false;
};
