#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctPawnSkins.generated.h"

class AScctPawnProxy;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UPoseableMeshComponent;
class UScctSessionSubsystem;

/**
 * The state skins over a pawn's body, so everyone can see what is done to him:
 *
 *  - Sticky shocked (SBasePawn.bStickySkinned, BeginStickySkin): the Stickess skin, the electric texture panning up
 *    the body and head over the lit suit, and the blinking blue light of the first shock spark (LT_Blink, hue 145,
 *    saturation 174, brightness 150, radius 10, at B R ForeArm) lighting him and what is round him, on and off every
 *    0.2 s as the Godot client's ShockFxView blinks it.
 *  - A merc's shock protection (the bridge's ShockImmune row): OpenSCCT's depth-tested amber edge over the whole
 *    body, pulsing over 1.5 s, lit from the feet up to the share of protection left (all of him while a state
 *    protects him), seen by every player as the Godot client's MercShockImmunityView draws it. Not on his own body
 *    seen from his own eyes (the body is not drawn there).
 *  - Wet (SBasePawn.bWet, BeginWetSkin): the wet LitSpecular skins' sheen, the wet env map by the camera-space
 *    reflection through each slot's specularity mask. Night vision amplifies the picture and shows it; thermal shows
 *    the wet pawn by the HeatIntensity the rain cools on the server (the original's wet heat modifiers use the dry
 *    heat textures), so the sheen is left out in thermal and EMF. Not drawn over a cloaked spy, and the Stickess skin
 *    takes its place while shocked (RestoreSkins puts it back), as SimPawnView does; the protection edge is kept
 *    over it.
 *
 * The skins are overlay materials (Tools/Materials/build_pawn_skin_materials.py, under
 * /Game/OpenSCCT/Materials/PawnSkins): a second draw of the same body, so the body keeps its own materials and the camo
 * suit's swap is never undone. The Stickess and wet skins are set per material slot (their textures follow each
 * slot's UVs: spy body and head, merc body; not the merc's face), the edge over the whole body. The states come from
 * the vision subsystem's rows of this frame (FScctVisionData), read on the session's OnFrame after the HUD refreshed
 * them.
 *
 * Unattended switches: -ScctForceSkin=sticky|immune|wet (several with commas) applies those skins to every pawn
 * whatever the simulation says; -ScctSkinCam[=spy|merc] puts the camera 2 m in front of the nearest other pawn (of
 * that side), looking at him, so a run can shoot the skins. The log says 'PawnSkins: sticky n, immune m, wet k' when
 * the counts change, and each pawn's skin going on or off with whether the simulation or the switch asked for it.
 */
UCLASS()
class OPENSCCT_API UScctPawnSkinsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** What each material slot of a character model takes. */
	struct FSlotSkins
	{
		bool bSticky = false;
		int32 Wet = INDEX_NONE; // index into WetMaterials
	};

	/** The skins shown on one pawn's body. */
	struct FPawnSkin
	{
		TWeakObjectPtr<AScctPawnProxy> Proxy;
		TWeakObjectPtr<UPoseableMeshComponent> Body;
		TWeakObjectPtr<UPointLightComponent> Light;
		TWeakObjectPtr<UMaterialInstanceDynamic> Edge;
		/** The overlay set on each slot now (null: none, the whole body's shows). */
		TArray<TWeakObjectPtr<UMaterialInterface>> SlotOverlays;
		bool bSticky = false;
		bool bImmune = false;
		bool bWet = false;
		bool bEdgeShown = false;
		/** When the shock began, world seconds: the light's blink runs from it. */
		double StickySince = 0.0;
		float PulsePhase = 0.0f;
	};

	void OnFrame(float DeltaSeconds);
	void OnMatchStopped();

	/** Loads the overlay materials once; false when they are missing (said once). */
	bool EnsureMaterials();

	/** What each slot of this body's model takes, worked out once per model from its material names. */
	const TArray<FSlotSkins>& SlotsOf(const UPoseableMeshComponent& Body);

	void Apply(AScctPawnProxy& Proxy, FPawnSkin& Skin, bool bSticky, bool bImmune, bool bWet, float Coverage, float Cloak, float DeltaSeconds);
	void UpdateLight(AScctPawnProxy& Proxy, FPawnSkin& Skin, bool bOn);
	void UpdateEdge(AScctPawnProxy& Proxy, UPoseableMeshComponent& Body, FPawnSkin& Skin, bool bShown, float Coverage, float DeltaSeconds);

	/** Takes every skin and light off (the match is over). */
	void RemoveAll();
	void Remove(FPawnSkin& Skin);

	/** -ScctSkinCam: the camera in front of a pawn. */
	void PlaceSkinCamera();

	TWeakObjectPtr<UScctSessionSubsystem> Session;
	FDelegateHandle FrameHandle;
	FDelegateHandle StoppedHandle;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> StickyMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> EdgeMaterial;

	/** The wet skins: spy body, spy head, merc body. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> WetMaterials;

	bool bTriedMaterials = false;

	/** The inverse of the exposure of the last frame: the edge and the sheen are brightnesses in the picture. */
	float PictureScale = 1.0f;

	TMap<int32, FPawnSkin> Skins;
	TMap<TWeakObjectPtr<const UObject>, TArray<FSlotSkins>> SlotsByModel;

	/** -ScctForceSkin. */
	bool bForceSticky = false;
	bool bForceImmune = false;
	bool bForceWet = false;

	/** -ScctSkinEdge (fill, rim, strength) and -ScctSkinWet (strength): levels held over the materials', negative when not. */
	float EdgeTune[3] = { -1.0f, -1.0f, -1.0f };
	float WetTune = -1.0f;

	/** -ScctSkinCam: 0 off, 1 any pawn, 2 spies only, 3 mercs only; and the pawn it follows. */
	int32 SkinCamera = 0;
	int32 SkinCameraTarget = 0;

	/** The last counts logged. */
	int32 LastSummary = -1;
	double LastSummaryLog = 0.0;
	double LastCameraLog = 0.0;
	double LastBlinkLog = 0.0;
};
