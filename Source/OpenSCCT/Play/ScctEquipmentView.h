#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctEquipmentApi.h"

class UMaterialInterface;
class UMeshComponent;
class UPoseableMeshComponent;
class UPrimitiveComponent;
struct FScctEquipmentModel;
struct FScctWeaponPresentation;

/** What the simulation says a weapon is doing this frame: what moves a replacement model's parts. */
struct FScctWeaponMotion
{
	/** Counts up with every shot (FScctEquipment::ShotCount); negative when not known. */
	int32 ShotCount = -1;

	/** 0 single, 1 burst, 2 automatic; negative when not known. */
	int32 RateOfFire = -1;

	bool bFiring = false;
	bool bReloading = false;
};

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

	/**
	 * Moves the replacement's own parts for this frame (the weapon table row's moving parts): bolt handle,
	 * trigger, selector and magazine. Call it after the frame's pose is set. Nothing without a replacement.
	 */
	void Animate(const FScctWeaponMotion& Motion, float DeltaSeconds);

	/**
	 * Closes the left hand on the replacement's grip: bends the row's left arm of Holder (the mesh that carries
	 * the arms: the view model's own posed mesh in first person, the body a gun hangs on) so the hand bone sits
	 * at the row's place in the replacement's LeftHandSocket. Only while the animation has the hand on the
	 * original gun. Call it after the frame's poses of the model and of Holder are set. Nothing without a
	 * replacement, or for a row that names no arm.
	 */
	void HoldWithLeftHand(UPoseableMeshComponent& Holder);

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

	/** The Merc sight's aperture or the centre of the drawn barrel when the model has no separate mount. */
	bool FindMercLaserMount(FTransform& OutWorld) const;

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
	void CopyPoseToFollowers();

	TWeakObjectPtr<UPoseableMeshComponent> Replacement;
	const FScctEquipmentModel* Model = nullptr;
	const FScctWeaponPresentation* Row = nullptr;
	double NextProbe = 0.0;
	int32 MercLaserBone = INDEX_NONE;
	FVector MercLaserBoreOffset = FVector::ZeroVector;

	/** The original's gun and magazine bones in the posed mesh, and the replacement's moving parts; INDEX_NONE where there is none. */
	int32 GunBone = INDEX_NONE;
	int32 OriginalMagazine = INDEX_NONE;
	int32 BoltBone = INDEX_NONE;
	int32 TriggerBone = INDEX_NONE;
	int32 SelectorBone = INDEX_NONE;
	int32 MagazineBone = INDEX_NONE;

	/** The bridge posed the original this frame (so its magazine bone moves with a reload). */
	bool bPosed = false;
	bool bWasReloading = false;
	int32 LastShot = -1;

	/** Seconds since the last shot and since the reload began; negative while neither runs. */
	float BoltAge = -1.0f;
	float ReloadAge = -1.0f;

	/** The trigger's pull (0 to 1) and the selector's turn (degrees) as drawn: they ease to where the simulation has them. */
	float TriggerPulled = 0.0f;
	float SelectorTurned = 0.0f;
	bool bSelectorSet = false;

	/** The arm was not a chain of three bones (said once). */
	bool bArmReported = false;
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
