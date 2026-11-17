#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctVisionBackdrop.h"
#include "Hud/ScctVisionData.h"
#include "Hud/ScctVisionMotion.h"
#include "Hud/ScctVisionObjects.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctVisionSubsystem.generated.h"

class APostProcessVolume;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;

/**
 * The goggles: night vision, thermal vision, motion tracking and EMF vision as post-process materials over the
 * whole picture. The HUD subsystem tells it each frame which mode the simulation says is on; the picture fades
 * into and out of a mode.
 *
 * The materials are assets under /Game/OpenSCCT/UI/Vision, written by Tools/Materials/build_vision_materials.py
 * (run Tools/Materials/Build-VisionMaterials.ps1 after changing it). Each has one parameter, Amount, 0 to 1.
 *
 * Characters are marked for the materials through the custom depth stencil while a mode is on. In thermal and EMF
 * each body follows the simulation's rule for that mode (the bridge's vision rows, FScctVisionData): thermal draws it
 * by its HeatIntensity and HeatOpacity, EMF only while it is EMF visible (EmfVision.Shows: a spy with his weapon away
 * is not drawn) and by its EMF glow and EMFOpacity. A body the mode hides is not drawn at all (neither the main pass
 * nor the custom depth, as the original discards it); a see-through one (opacity below 0.99) is drawn only into the
 * custom depth so the material lays it over the scene behind by its opacity; the stencil carries the level and the
 * opacity (PawnStencil). What hangs on the body follows it, at the glow of what it carries when that is more (the
 * spy's drawn pistol). Motion tracking marks no body: it shows the blurred scene as the level's navy to red ramp and
 * the scene sharp only in the tracker's clean rectangles round the pawns it has locked (FScctVisionMotion), and does
 * not draw a fully cloaked spy. Night vision reads no marks. Gadgets, devices and the level's hot or electric meshes are marked the same way by their own heat and EMF
 * values (FScctVisionObjects, Hud/ScctVisionObjects.h). Other things only the goggles show are marked by whoever draws them
 * (the detector beams: Play/ScctMarkersSubsystem): StencilEmfStreak is drawn as a dark grey streak in EMF, and
 * HeatStencil(Heat) as that heat in thermal.
 *
 * Other subsystems ask which mode is shown (GetVisionMode and the tests after it) to show what the original hides
 * from the naked eye: beams flagged bHideInNormalVision in thermal and EMF, effects flagged bHideInNormalVision in
 * night and thermal vision.
 *
 * Unattended switch: -ScctForceVision=<34|36|37|38> holds that mode whatever the HUD says (0 holds plain sight), so
 * a mode can be shot without a player turning the goggles on. -ScctVisionHeatOpacity=<0..1> and
 * -ScctVisionEmfOpacity=<0..1> hold every pawn's opacity in that mode (0 hides them all, 0.5 see-through), to check
 * the opacity path. Thermal and EMF log 'Vision: <mode> marks n pawns (m hidden, k see-through)' when that changes.
 */
UCLASS()
class OPENSCCT_API UScctVisionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Shows the simulation's vision mode (FScctHudState::VisionMode: 34 night, 36 thermal, 37 motion, 38 EMF; anything else is plain sight). */
	void Present(int32 VisionMode, float DeltaSeconds);

	/** Plain sight at once: the match is over. */
	void Clear();

	/** The mode the goggles show now (34 night, 36 thermal, 37 motion, 38 EMF, 0 plain sight), forced or the HUD's. */
	int32 GetVisionMode() const { return ShownMode; }

	/** Thermal or EMF: the modes that show detector beams flagged bHideInNormalVision. */
	bool IsHeatOrEmf() const { return ShownMode == 36 || ShownMode == 38; }

	/** Night or thermal: the modes that show effects flagged bHideInNormalVision (the original's EffectViews rule). */
	bool ShowsVisionOnlyEffects() const { return ShownMode == 34 || ShownMode == 36; }

	/**
	 * Thermal or EMF is in the picture: coronas (the level's, the torches' and the glow cards) and the sky are not drawn,
	 * so the frame keeps the mode's clear colour (FScctVisionBackdrop). The equipment asks for the torch coronas.
	 */
	bool ShouldHideCoronasAndSky() const { return Backdrop.IsHidden(); }

	/** The simulation's per-actor vision values of this frame (heat, EMF, motion, shock protection), refreshed by Present. */
	const FScctVisionData& GetData() const { return Data; }
	FScctVisionData& GetData() { return Data; }

	/** The goggles of a world, or null. */
	static const UScctVisionSubsystem* Find(const UWorld* World);

	/** The custom depth stencil of something EMF vision draws as a dark grey streak. */
	static constexpr int32 StencilEmfStreak = 3;

	/** The custom depth stencil of something thermal vision draws at Heat (0 to 1): 128 to 255. */
	static int32 HeatStencil(float Heat) { return 128 + FMath::RoundToInt(FMath::Clamp(Heat, 0.0f, 1.0f) * 127.0f); }

	/** The first custom depth stencil of a pawn body in thermal or EMF (64 to 127). */
	static constexpr int32 StencilPawnFirst = 64;

	/**
	 * The custom depth stencil of a pawn body in thermal or EMF: 64 + level x 4 + opacity step, the level the mode's
	 * intensity (HeatIntensity or EMF glow, 0 to 1) in 16 steps, the opacity step 0 to 2 for 1/4 to 3/4 see-through
	 * and 3 for opaque (0.99 and up). Build_vision_materials.py decodes it.
	 */
	static int32 PawnStencil(float Intensity, float Opacity);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static constexpr int32 ModeCount = 4;

	/** Motion tracking's place in the modes and materials (mode 37). */
	static constexpr int32 MotionIndex = 2;

	/** Spawns the unbound volume that carries the materials; false when the material assets are missing. */
	bool EnsureVolume();

	/** Marks or unmarks the pawn bodies in the custom depth stencil. */
	void MarkCharacters(bool bMark);

	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> Volume;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	/** How far the picture is into each mode, 0 to 1. */
	float Amounts[ModeCount] = {};

	/**
	 * The night vision tube's gain (the material's Gain). A real tube turns itself down in a lit room; this one
	 * follows the brightness of the whole picture as the engine's exposure measured it (FScctExposure::PictureLevel),
	 * within bounds and eased over about half a second, so a lamp that comes into view does not make the picture
	 * pump. 0 until the goggles are first on.
	 */
	float NightGain = 0.0f;
	bool bTriedMaterials = false;
	bool bCharactersMarked = false;
	int32 LastMode = 0;

	FScctVisionData Data;

	/** The gadgets, devices and level meshes thermal and EMF draw by their own values. */
	FScctVisionObjects Objects;

	/** The coronas and the sky, hidden in thermal and EMF. */
	FScctVisionBackdrop Backdrop;

	/** Motion tracking's level ramp and the tracker's clean rectangles. */
	FScctVisionMotion Motion;

	/** The mode shown, after the forced one is applied. */
	int32 ShownMode = 0;

	/** -ScctForceVision: the mode held whatever the HUD says, INDEX_NONE when not forced. */
	int32 ForcedMode = INDEX_NONE;

	/** -ScctVisionHeatOpacity / -ScctVisionEmfOpacity: every pawn's opacity in that mode, negative when not held. */
	float HeatOpacityOverride = -1.0f;
	float EmfOpacityOverride = -1.0f;

	/** The pawn primitives whose shadow a mode took away while it hid them, to be given back. */
	TSet<TWeakObjectPtr<UPrimitiveComponent>> ShadowsTaken;

	/** The last pawn marking summary and when it was logged. */
	int32 LastMarkSummary = -1;
	double LastMarkLog = 0.0;
};
