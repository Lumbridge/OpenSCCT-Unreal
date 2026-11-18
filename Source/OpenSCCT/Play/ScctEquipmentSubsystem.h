#pragma once

#include "CoreMinimal.h"
#include "Play/ScctEquipmentModels.h"
#include "Play/ScctEquipmentView.h"
#include "Runtime/ScctEquipmentApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctEquipmentSubsystem.generated.h"

class AScctFirstPersonView;
class AScctPawnProxy;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UScctSessionSubsystem;
class UScctWeaponSet;
class USpotLightComponent;
class UStaticMeshComponent;

/**
 * Draws what the pawns of the match hold and wear, from the bridge's equipment table (Runtime/ScctEquipmentApi.h).
 * The simulation decides all of it; nothing here changes the match.
 *
 * Each frame, in the session's OnFrame (the pawn bodies are posed and the camera is final by then):
 *   - attachments: one posed mesh per pawn and slot (the weapon in a hand or at the holster, a second carried
 *     weapon, the lit goggle lenses) on the body's bone with the bridge's offset. They are part of the body: shown,
 *     hidden and shadowed with it, so the local player seen from his own eyes keeps the shadow of his gun.
 *   - the torch: a shadow-casting spot light with the bridge's cone, range and colour that scatters in the
 *     level's fog. On the merc's head bone for others; at the eye for the player holding it.
 *   - the laser: a thin self-lit beam to the point the simulation's trace hit, a dot and a little red light there.
 *   - the camo suit: body and attachments turn to bent light by the bridge's amount.
 *   - the local player's first-person view model (AScctFirstPersonView) and its muzzle flash, a flash for every
 *     shot the bridge counts.
 * Which mesh stands for a weapon class, and a replacement model with its sockets, come from the weapon table
 * (UScctWeaponSet, /Game/OpenSCCT/Weapons/DA_ScctWeapons).
 *
 * Muzzle flashes of other pawns, tracers and impacts are not drawn here: those belong to the match effects, which
 * ask FindMuzzle for where a pawn's barrel ends and DrawsLocalMuzzleFlash for whether the local flash is theirs.
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard):
 *   -ScctForceCloak=<0..1>   draw every pawn and the view model with that much of the camo suit on (the suit is
 *                            not in the default kit, so a local match never switches it on by itself)
 *   -ScctNoViewModel         do not draw the first-person view model (to compare a view with and without it)
 *
 * Console variables for tuning with the game running: scct.Torch.CandelasPerBrightness, scct.Torch.FogScattering,
 * scct.FirstPerson.Scale, and scct.Weapons.Replacements (0, the default: the original models; 1: the weapon
 * table's replacement models where their assets exist; read when a weapon is next shown).
 */
UCLASS()
class OPENSCCT_API UScctEquipmentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/** What a pawn holds and wears this frame as the bridge described it, or null. */
	const FScctEquipment* FindEquipment(int32 ActorId) const;

	/**
	 * Where a pawn's barrel ends in the world this frame, X along the barrel: the view model's muzzle for the
	 * local player while it is drawn, else the muzzle of the gun on the body. False when the pawn shows no gun.
	 */
	bool FindMuzzle(int32 ActorId, FTransform& OutWorld) const;

	/**
	 * Where a pawn's gun throws its spent casing this frame, with the port's turn: a replacement model's
	 * EjectionPort socket, else the original's bone FX_DOUILLE; on the view model for the local player while it is
	 * drawn, else on the gun on the body. False when the gun has neither (the spy's pistol) or no gun is drawn.
	 */
	bool FindCasingPort(int32 ActorId, FTransform& OutWorld) const;

	/**
	 * The local player's own muzzle flash is this subsystem's while it draws a view model: the flash sits on that
	 * model's muzzle. The match effects draw the local first-person flash only while this is false (no view model:
	 * the equipment table is missing, -ScctNoViewModel, a tour camera).
	 */
	bool DrawsLocalMuzzleFlash() const;

	/** The first-person view model of this frame as the bridge described it; Kind is None when none is drawn. */
	const FScctFirstPerson& GetFirstPerson() const { return FirstPerson; }

	/** The actor drawing the view model, or null before a match has shown one. */
	AScctFirstPersonView* GetFirstPersonView() const { return FirstPersonView; }

	/** For the named checks (Test/Features): holds the camo suit's amount as -ScctForceCloak does; negative gives the simulation's back. */
	void ForceCloakForCheck(float Amount) { ForcedCloak = Amount; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FAttachmentView
	{
		FScctEquipmentView View;
		int32 Mesh = 0;
		int32 Bone = 0;
		int32 Skin = -1;
		bool bAnimated = false;
		bool bSeen = false;
	};

	/** Everything drawn for one pawn. The components live on the pawn's proxy and go with it. */
	struct FPawnGear
	{
		TWeakObjectPtr<AScctPawnProxy> Proxy;
		FAttachmentView Slots[3];
		TWeakObjectPtr<USpotLightComponent> Torch;
		TWeakObjectPtr<UStaticMeshComponent> Corona;
		TWeakObjectPtr<UStaticMeshComponent> Beam;
		TWeakObjectPtr<UStaticMeshComponent> Dot;
		TWeakObjectPtr<UPointLightComponent> DotLight;
		int32 Flags = 0;
		int32 ShotCount = -1;
		int32 WeaponClass = 0;
		int32 WeaponSlot = 0;
		bool bSeen = false;
	};

	void OnMatchStarted();
	void OnMatchStopped();
	void OnSessionChanged(int32 OldSessionId, int32 NewSessionId);
	void OnFrame(float DeltaSeconds);

	void UpdateAttachment(const FScctEquipmentApi& Api, FPawnGear& Gear, AScctPawnProxy& Proxy, const FScctAttachment& Attachment);
	void UpdateTorch(FPawnGear& Gear, AScctPawnProxy& Proxy, const FScctEquipment& Equipment, bool bFromEye);
	void UpdateLaser(FPawnGear& Gear, AScctPawnProxy& Proxy, const FScctEquipment& Equipment, bool bFromViewModel);
	void UpdateFirstPerson(const FScctEquipmentApi& Api, float DeltaSeconds);
	void LogChanges(const FScctEquipmentApi& Api, FPawnGear& Gear, const FScctEquipment& Equipment);
	void DropGear(FPawnGear& Gear);
	void DropEverything();

	/** A self-lit shape (beam, dot, corona) on a proxy, placed in the world each frame. */
	UStaticMeshComponent* MakeGlowShape(AScctPawnProxy& Proxy, const TCHAR* MeshPath, const FLinearColor& Color, float Intensity, float Sharpness);

	/** The camera of this frame in the world. */
	FTransform GetCamera() const;

	/** What a pawn's weapon is doing this frame, for the moving parts of a model drawn in its place. */
	static FScctWeaponMotion MotionOf(const FScctEquipment& Pawn);

	/** The frame's length, for what UpdateAttachment animates. */
	float FrameSeconds = 0.0f;

	TWeakObjectPtr<UScctSessionSubsystem> Session;
	FDelegateHandle StartedHandle;
	FDelegateHandle StoppedHandle;
	FDelegateHandle ChangedHandle;
	FDelegateHandle FrameHandle;

	FScctEquipmentModels Models;
	FScctCloakLook CloakLook;
	TMap<int32, FPawnGear> Gear;

	TArray<FScctEquipment> Equipment;
	int32 EquipmentCount = 0;
	TArray<FScctAttachment> Attachments;
	TArray<FScctBridgeBone> PoseBuffer;
	FScctFirstPerson FirstPerson = {};
	int32 FirstPersonShots = -1;
	int32 FirstPersonActor = 0;

	/** -ScctForceCloak; negative when not given. */
	float ForcedCloak = -1.0f;
	bool bNoViewModel = false;

	/** The missing equipment table has been reported (once, not every frame). */
	bool bReportedNoTable = false;

	UPROPERTY(Transient)
	TObjectPtr<AScctFirstPersonView> FirstPersonView;

	UPROPERTY(Transient)
	TObjectPtr<const UScctWeaponSet> Weapons;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GlowMaterial;
};
