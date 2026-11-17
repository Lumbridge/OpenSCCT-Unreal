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
 * Thermal vision draws marked pixels hot.
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
};
