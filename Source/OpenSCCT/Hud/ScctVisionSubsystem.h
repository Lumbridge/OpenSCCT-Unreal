#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctVisionSubsystem.generated.h"

class APostProcessVolume;
class UMaterialInstanceDynamic;

/**
 * The goggles: night vision, thermal vision, motion tracking and EMF vision as post-process materials over the
 * whole picture. The HUD subsystem tells it each frame which mode the simulation says is on; the picture fades
 * into and out of a mode.
 *
 * The materials are assets under /Game/OpenSCCT/UI/Vision, written by Tools/Materials/build_vision_materials.py
 * (run Tools/Materials/Build-VisionMaterials.ps1 after changing it). Each has one parameter, Amount, 0 to 1.
 *
 * Characters are marked for the materials through the custom depth stencil while a mode is on: 1 a spy, 2 a merc.
 * Thermal vision draws marked pixels hot. Other things only the goggles show are marked by whoever draws them
 * (the detector beams: Play/ScctMarkersSubsystem): StencilEmfStreak is drawn as a dark grey streak in EMF, and
 * HeatStencil(Heat) as that heat in thermal.
 *
 * Other subsystems ask which mode is shown (GetVisionMode and the tests after it) to show what the original hides
 * from the naked eye: beams flagged bHideInNormalVision in thermal and EMF, effects flagged bHideInNormalVision in
 * night and thermal vision.
 *
 * Unattended switch: -ScctForceVision=<34|36|37|38> holds that mode whatever the HUD says (0 holds plain sight), so
 * a mode can be shot without a player turning the goggles on.
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

	/** The goggles of a world, or null. */
	static const UScctVisionSubsystem* Find(const UWorld* World);

	/** The custom depth stencil of something EMF vision draws as a dark grey streak. */
	static constexpr int32 StencilEmfStreak = 3;

	/** The custom depth stencil of something thermal vision draws at Heat (0 to 1): 128 to 255. */
	static int32 HeatStencil(float Heat) { return 128 + FMath::RoundToInt(FMath::Clamp(Heat, 0.0f, 1.0f) * 127.0f); }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static constexpr int32 ModeCount = 4;

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
	bool bTriedMaterials = false;
	bool bCharactersMarked = false;
	int32 LastMode = 0;

	/** The mode shown, after the forced one is applied. */
	int32 ShownMode = 0;

	/** -ScctForceVision: the mode held whatever the HUD says, INDEX_NONE when not forced. */
	int32 ForcedMode = INDEX_NONE;
};
