#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctMouseAimTest.generated.h"

class AScctPlayerController;

/**
 * -ScctLookTest=<side>,<field>,<value>: checks in a running match that a mouse and aim setting changed through the
 * Options screen's own path turns the view differently. Once the local player has a view (and a few seconds more,
 * so the spawn has settled), it moves the mouse by a fixed number of counts and logs how far the view turned; then it
 * sets the field with ScctMouseAim::SetField and applies and saves it as APPLY and SAVE would, logs the value the
 * bridge reports back and the side's block of controls.json, moves the mouse by the same counts again and logs the
 * turn and the ratio of the two. Last it puts the field back as it was (applied and saved), so the run leaves the
 * player's file as it found it.
 *
 * The mouse goes in as Unreal mouse axis events on the player controller, the same path as a real mouse. A run with
 * nobody at the keyboard needs -ScctAutoplay (any script, "wait 60" will do) so the controller takes the input with
 * its window in the background. Example (the merc's plain look is the one the counts turn):
 *   -ScctLookTest=merc,Look,2.0 -ScctAutoplay="wait 60" with ?Side=merc
 * Only made when the switch is given.
 */
UCLASS()
class OPENSCCT_API UScctMouseAimTest : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	enum class EStage : uint8 { WaitView, Settle, Turn, Measure, Change, Done };

	/** Starts a turn of the fixed counts and remembers the yaw it starts from. */
	void BeginTurn(const AScctPlayerController& Controller);

	/** The yaw turned since BeginTurn, degrees, the short way round. */
	float TurnedSince(const AScctPlayerController& Controller) const;

	/** Sets the field through the section's model, applies, saves and logs what the bridge and the file now hold. */
	void ChangeField(const FString& Value, const TCHAR* Why);

	EStage Stage = EStage::WaitView;
	float Clock = 0.0f;
	int32 FramesSent = 0;
	float StartYaw = 0.0f;
	bool bAfter = false;
	float TurnBefore = 0.0f;
	FString Original;
};
