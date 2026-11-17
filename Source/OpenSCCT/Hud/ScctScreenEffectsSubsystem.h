#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctVisorEffects.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctScreenEffectsSubsystem.generated.h"

class FScctHudModel;
class UTextureRenderTarget2D;
struct FScctHudState;

/**
 * The flash-bang's blinding (spec/equipment/flash-bang.md rule 5, spec/hud/screen-effects.md rules 7 and 14).
 *
 * When the simulation asks for an after-image (FScctHudState::RemanenceCaptures moves on) the next frame the player
 * sees is copied once, straight after tone mapping and before the HUD, into a texture at half the viewport's size.
 * The HUD draws that frozen frame over the live picture by RemanenceBlendFactor (1 to 0 over 8 s), so the moment of
 * the flash stays burnt in and fades. The white-out on top comes from the merc page's flash time and from the
 * FlashEffect slot (FlashWhiteOut).
 *
 * The HUD subsystem calls Present once a frame after reading the snapshot.
 *
 * -ScctForceFlash[=seconds] fakes a flash-bang hit that many seconds (4 by default) after the HUD opens, so the
 * effect can be looked at unattended.
 *
 * Gas and smoke double the picture (spec/hud/screen-effects.md, "Cross-eyed double vision"): while CrossEyedFactor
 * (or a running CrossEye or Smoke slot) is above nothing, M_ScctDoubleVision on an unbound post volume of its own
 * draws the finished frame twice, half each, one copy stretched from the left by SpreadA and the other from the
 * right by SpreadB. The spreads swing with the time as the original's formula has it (DoubleVisionSpread). The volume
 * is made the first time the view doubles, so a match without gas has no extra blendable at all.
 *
 * -ScctForceCrossEye=<0..1> holds CrossEyedFactor at that value from the start, so the doubling can be shot unattended.
 *
 * The merc's visor effects (EMP shutdown, perturbation, foam, droplets, sticky shock; Hud/ScctVisorEffects) keep their
 * latches here: Present moves them on and points the model at them.
 */
UCLASS()
class OPENSCCT_API UScctScreenEffectsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Seconds the FlashEffect slot and the merc page's white-out run (ClientDamageHudEffect type 8, A_Flash 80 frames at 10 fps). */
	static constexpr float FlashSeconds = 8.0f;

	/** Factors at or under this draw no after-image (viewport post-render 0x10a0b050). */
	static constexpr float AfterImageThreshold = 0.0039f;

	/** How white the screen is for this state, 0 to 1: the merc page's white-out or a running FlashEffect slot. */
	static float FlashWhiteOut(const FScctHudState& State);

	/** How cross-eyed the view is for this state, 0 to 1: CrossEyedFactor, or a running CrossEye or Smoke slot. */
	static float CrossEyeFactor(const FScctHudState& State);

	/**
	 * The U offsets of the two copies of the picture for cross-eye factor Factor at Seconds (game 0x10966010): the
	 * first copy shows U 0 to 1 - X across the screen, the second U Y to 1. Each swings between 25/640 and 75/640 of
	 * the width at factor 1.
	 */
	static FVector2f DoubleVisionSpread(float Factor, double Seconds);

	/** Notices new after-image requests, and puts the held frame and its strength in the model for the HUD to draw. */
	void Present(FScctHudModel& Model, float DeltaSeconds);

	/** Nothing burnt in any more: the match is over. */
	void Clear();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Copies the next frame drawn into the after-image texture (made or resized to half the viewport first). */
	void RequestCapture();

	/** -ScctForceFlash: writes a flash-bang hit into the snapshot as the bridge would. */
	void ApplyForcedFlash(FScctHudModel& Model, float DeltaSeconds);

	/** Doubles the picture by Factor (0 takes the blendable out of the frame). */
	void PresentDoubleVision(float Factor);

	/** Makes the double vision's post volume and material the first time; false when the material is missing. */
	bool EnsureDoubleVision();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> AfterImage;

	UPROPERTY(Transient)
	TObjectPtr<class APostProcessVolume> DoubleVolume;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> DoubleMaterial;

	bool bTriedDoubleVision = false;
	bool bDoubled = false;
	double NextDoubleLog = 0.0;

	/** -ScctForceCrossEye: the factor held from the start (negative when not forced). */
	float ForcedCrossEye = -1.0f;

	FSlateBrush AfterImageBrush;

	/** The render side: a scene view extension that copies the tone-mapped frame when asked. */
	TSharedPtr<class FScctAfterImageCapture, ESPMode::ThreadSafe> Capture;

	int32 LastCaptures = INDEX_NONE;
	int32 CapturesSeen = 0;
	bool bHeld = false;
	bool bWhiteOut = false;

	float ForceDelay = -1.0f;
	float ForcedAt = -1.0f;

	FScctVisorState Visor;
};
