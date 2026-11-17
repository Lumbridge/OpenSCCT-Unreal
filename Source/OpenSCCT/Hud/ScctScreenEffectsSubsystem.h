#pragma once

#include "CoreMinimal.h"
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

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> AfterImage;

	FSlateBrush AfterImageBrush;

	/** The render side: a scene view extension that copies the tone-mapped frame when asked. */
	TSharedPtr<class FScctAfterImageCapture, ESPMode::ThreadSafe> Capture;

	int32 LastCaptures = INDEX_NONE;
	int32 CapturesSeen = 0;
	bool bHeld = false;
	bool bWhiteOut = false;

	float ForceDelay = -1.0f;
	float ForcedAt = -1.0f;
};
