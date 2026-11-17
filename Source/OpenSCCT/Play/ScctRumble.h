#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctRumble.generated.h"

class APlayerController;
struct FScctPadRumble;

/**
 * The controller's vibration: the simulation's SPlayerController.Vibration_* notifications for the local player
 * (a shot, a gadget, a blast, a grab, a sticky shock...) played on the controller in use, as the Godot client's
 * PadRumble plays them. The bridge does the rules (Bridge/.../Controls/PadRumble.cs: each notification's priority,
 * two motor strengths and length, Vibration_CheckSiPossible, the settings' on/off and strength, the round's end);
 * this takes its rumbles each frame and starts them with APlayerController::PlayDynamicForceFeedback, the
 * low-frequency motor on the left large channel and the high-frequency one on the right small channel (XInput's
 * left and right motors). A new rumble replaces the one running, as NativeRumblePadStart does.
 *
 * The log has "ScctRumble: <name> large=<0..1> small=<0..1> <s> s priority <n> (<played or why not>)" for every
 * rumble and "ScctRumble: PlayDynamicForceFeedback ..." for each one started (no controller is needed for either).
 *
 * -ScctRumbleTest=<Name>@<seconds>[;...] raises those notifications on the local player's controller through the
 * simulation at those match seconds (as the script's Vibration_ functions raise them), e.g.
 * -ScctRumbleTest="Vibration_Blast@6;Vibration_Tir@6.1;Vibration_Tir@7". -ScctRumbleOn turns the vibration on for the
 * run (not saved), since a first run has it off as Reloaded does.
 */
UCLASS()
class OPENSCCT_API UScctRumbleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Starts Rumble on Player's controller (replacing the one running). Why goes in the log line. */
	static void PlayOn(APlayerController* Player, const FScctPadRumble& Rumble, const TCHAR* Why);

	/** The Options screen's TEST: the blast's rumble at the strength set, on the first local player's controller. */
	static void PlayTest(UWorld* World);

private:
	void RunChecks();

	/** The two dynamic force feedback entries a rumble uses (low motor, high motor), reused so a new one replaces the last. */
	uint64 LowHandle = 0;
	uint64 HighHandle = 0;

	float Seconds = 0.0f;
	bool bChecksRead = false;
	bool bSessionSeen = false;
	TArray<TPair<FString, float>> Checks;
	int32 RumblesSeen = 0;
	int32 RumblesPlayed = 0;
};
