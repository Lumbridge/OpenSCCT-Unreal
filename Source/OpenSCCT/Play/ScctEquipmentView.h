#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctEquipmentApi.h"

class UMaterialInterface;
class UMeshComponent;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
struct FScctEquipmentModel;
struct FScctWeaponPresentation;

/**
 * One equipment model as it stands in the world: the posed original mesh (with the file's other meshes following
 * it) and, when the weapon table names one that exists, the replacement model fixed to its gun bone. Used for a
 * gun on a body's bone and for each model of the first-person view alike.
 */
struct FScctEquipmentView
{
	/**
	 * Makes the components for Model on Owner (dropping those of the model shown before). Row is the weapon
	 * table's entry for the class, or null. bFirstPerson marks every primitive as a first-person primitive, drawn
	 * with the camera's first-person scale so it cannot reach into walls.
	 */
	void Show(AActor& Owner, const FScctEquipmentModel* Model, const FScctWeaponPresentation* Row, bool bFirstPerson);

	void Destroy();

	bool IsShown() const { return Body.IsValid(); }

	/** The posed original mesh: what is attached, placed and shown or hidden; everything else hangs on it. */
	UPoseableMeshComponent* GetBody() const { return Body.Get(); }

	const FScctEquipmentModel* GetModel() const { return Model; }

	/** The weapon table's row the view was made with, or null. */
	const FScctWeaponPresentation* GetRow() const { return Row; }

	/** A replacement model is drawn in place of the original gun. */
	bool HasReplacement() const { return Replacement.IsValid(); }

	/** Sets the bridge's pose of the model. */
	void ApplyPose(TConstArrayView<FScctBridgeBone> Pose);

	/** Back to the model's rest pose. */
	void ResetPose();

	/** Every mesh component of the view: the original, its followers and the replacement. */
	void ForEachMesh(TFunctionRef<void(UMeshComponent&)> Visit) const;

	/**
	 * Where a thing of the weapon is in the world this frame: the replacement's socket when a replacement is
	 * drawn and has it, else the original model's bone (named as the bridge names it). False when neither exists.
	 */
	bool FindPoint(FName ReplacementSocket, const TCHAR* OriginalBone, FTransform& OutWorld) const;

	/** The muzzle: the replacement's Muzzle socket, else the bone "FX". */
	bool FindMuzzle(FTransform& OutWorld) const;

	/**
	 * -ScctWeaponProbe (unattended, off by default): every few seconds logs every bone of the original model in
	 * the space of the weapon table row's gun bone (place, and where its X, Y and Z axes point), the hand bones
	 * of Holder (the body the weapon is attached to, if any) in the same space, and the replacement's sockets in
	 * the replacement's own space. These are the numbers a row's AttachOffset is worked out from: where the hands
	 * are around the gun bone and which way the barrel runs.
	 */
	void LogProbe(const class USkinnedMeshComponent* Holder = nullptr);

private:
	TWeakObjectPtr<UPoseableMeshComponent> Body;
	TArray<TWeakObjectPtr<UPoseableMeshComponent>> Followers;
	TWeakObjectPtr<USkeletalMeshComponent> Replacement;
	const FScctEquipmentModel* Model = nullptr;
	const FScctWeaponPresentation* Row = nullptr;
	double NextProbe = 0.0;
};

/**
 * The camo suit on mesh components: while the amount is above nothing, every material is swapped for the cloak
 * material carrying the surface's own texture, and the shadow goes as the suit comes fully on. Back at nothing
 * the components get their own materials again.
 */
class FScctCloakLook
{
public:
	void Apply(UMeshComponent& Component, float Amount);

	/** Forgets components that are gone. */
	void Trim();

	/** Keeps the materials taken off the cloaked meshes alive; call from the owner's AddReferencedObjects. */
	void AddReferencedObjects(FReferenceCollector& Collector);

private:
	struct FState
	{
		TArray<TObjectPtr<UMaterialInterface>> Originals;
		bool bCastShadow = true;
	};

	TMap<TWeakObjectPtr<UMeshComponent>, FState> Cloaked;
};
