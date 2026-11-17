#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;

/**
 * Plays a written list of key presses and mouse movements into a player controller, for runs with nobody at the
 * keyboard (-ScctAutoplay="..." on the command line, see ScctPlayerController.h for the script's grammar).
 *
 * It does not talk to the simulation or to the bridge's input mapper. Every step becomes the same Unreal input
 * events a keyboard and mouse raise (APlayerController::InputKey with key presses, releases and mouse axis
 * deltas), so what is exercised is the whole real path: Unreal's key state, the controller's reading of it, the
 * bridge's bindings and the simulation.
 */
class FScctControlAutoplay
{
public:
	/**
	 * The key the default bindings put a named action on ("fire", "forward"... the names of the script's grammar),
	 * or an invalid key for a name that is not one.
	 */
	static FKey FindActionKey(const FString& Action);

	/** Raises one key event on Controller as the keyboard or mouse would. */
	static void SendKey(APlayerController& Controller, const FKey& Key, bool bPressed);

	/** Reads a script. False with the reason when a step is not understood; nothing is kept then. */
	bool Parse(const FString& Script, FString& OutError);

	bool HasSteps() const { return Steps.Num() > 0; }

	/** Every step has been played and its keys are up again. */
	bool IsFinished() const { return Index >= Steps.Num() && !bFinalReportDue; }

	/**
	 * Advances the script by one frame: raises the frame's input events on Controller. Call it before the
	 * controller processes its input for the frame. MouseCountsPerDegree turns the degrees of "turn" and "look"
	 * steps into mouse counts.
	 */
	void Tick(float DeltaSeconds, APlayerController& Controller, float MouseCountsPerDegree);

	/**
	 * Called with a label when a step begins (before its first input) and once after the last step: the place to
	 * log where the player is. What it sees at a step's start is the result of the steps before.
	 */
	TFunction<void(const FString& Label)> OnReport;

private:
	struct FStep
	{
		FString Text;
		TArray<FKey> Keys;        // held for the step's length
		float Seconds = 0.0f;
		float TurnDegrees = 0.0f; // mouse to the right, spread evenly over the step
		float LookDegrees = 0.0f; // mouse pushed away (look up)
		int32 WheelNotches = 0;   // up positive, all at the step's start
	};

	void Begin(const FStep& Step, APlayerController& Controller);
	void MoveMouse(const FStep& Step, APlayerController& Controller, float CountsPerDegree);

	TArray<FStep> Steps;
	int32 Index = 0;
	bool bBegun = false;
	bool bFinalReportDue = false;
	float Elapsed = 0.0f;

	/** Whole mouse counts already sent in this step: a mouse reports whole counts, so fractions are carried. */
	int32 SentX = 0;
	int32 SentY = 0;
};
