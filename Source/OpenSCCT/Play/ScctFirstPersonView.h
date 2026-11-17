#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Play/ScctEquipmentView.h"
#include "Runtime/ScctEquipmentApi.h"
#include "ScctFirstPersonView.generated.h"

class FScctEquipmentModels;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class UScctWeaponSet;

/**
 * The local player's first-person view model: the merc's arms and gun, or the spy's arms and pistol in his eye
 * view. The bridge computes everything about it (which models, every bone, where the skeleton sits in front of
 * the camera); this actor draws it.
 *
 * It is placed from the frame's final camera, in the session's OnFrame, which runs after the camera has been
 * moved and before the engine caches it: the model is never a frame behind the view.
 *
 * Its meshes are first-person primitives (EFirstPersonPrimitiveType::FirstPerson): the renderer pulls them
 * towards the camera by the camera's first-person scale, which leaves them exactly where they were on screen
 * but nearer than any wall the player can stand against, so the gun never pokes into the level. They are lit by
 * the scene like everything else and shadow themselves (r.FirstPerson.SelfShadow); the shadow they throw on the
 * world is the hidden third-person body's. The field of view is the world's: the bridge places the model for it.
 *
 * Unattended switch (off by default; for runs with nobody at the keyboard):
 *   -ScctFlashHold=<seconds>   keep every muzzle flash at full strength that long before it fades, so a
 *                              screenshot can catch one (a real flash is over in a twentieth of a second)
 */
UCLASS(NotPlaceable, Transient)
class OPENSCCT_API AScctFirstPersonView : public AActor
{
	GENERATED_BODY()

public:
	AScctFirstPersonView();
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/**
	 * Draws the frame's view model as the bridge describes it, in front of Camera (the frame's final camera in
	 * the world). Cloak is the camo suit's amount on the arms and gun.
	 */
	void Present(const FScctEquipmentApi& Api, FScctEquipmentModels& Models, const UScctWeaponSet* Weapons,
		const FScctFirstPerson& View, const FTransform& Camera, float Cloak, float DeltaSeconds);

	/** Nothing is drawn this frame (third person, the scope, a pistol put away, a tour camera). */
	void Hide(float DeltaSeconds);

	bool IsShown() const { return bShown; }

	/** The muzzle in the world this frame, X along the barrel. False while no model with a muzzle is drawn. */
	bool FindMuzzle(FTransform& OutWorld) const;

	/** Where the laser leaves the weapon this frame: the spy pistol's laser socket, else the muzzle. */
	bool FindLaserStart(FTransform& OutWorld) const;

	/** A shot left the gun: the flash and its light, at the muzzle. */
	void PlayMuzzleFlash();

private:
	void TickFlash(float DeltaSeconds);

	FScctEquipmentView Parts[2];
	int32 PartMeshes[2] = { 0, 0 };
	int32 RateOfFireSkin = -1;
	bool bShown = false;

	bool bHasMuzzle = false;
	bool bHasLaser = false;
	FTransform Muzzle;
	FTransform Laser;
	FTransform LastCamera;
	FScctCloakLook CloakLook;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<USceneComponent> FlashRoot;

	/** The flash: a hot core, a tongue along the barrel and two thin flares across it. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FlashShapes;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlashMaterials;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> FlashLight;

	/** Seconds since the last shot; negative while no flash shows. */
	float FlashAge = -1.0f;
	int32 FlashesPlayed = 0;
	TArray<FScctBridgeBone> PoseBuffer;
};
