#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctHudModel.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctHudSubsystem.generated.h"

class UScctHudWidget;

/**
 * The HUD of a game world. It hangs off the session subsystem: when a match starts it puts the HUD widget in the
 * viewport, and every frame of the match it reads the bridge's HUD snapshot (Runtime/ScctHudApi.h) into the
 * model the widget paints from, drains the new messages, and tells the vision subsystem which goggles are on.
 * All text on the HUD is the player's own game content, handed over by the bridge.
 *
 * Keys: none of its own. The player's bindings raise interface commands (AScctPlayerController::OnLocalCommand)
 * and the HUD serves three: HoldScoreboard (the scoreboard while the key is down), ShowConnectedPlayers (the
 * scoreboard on or off; F10 by default) and SwitchEnhancedReality (objective markers on or off; F9 by default).
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard):
 *   -ScctHudCommands="3 SwitchHeadset 1;9 Scoreboard 1;14 Scoreboard 0"
 *        timed commands, "seconds-into-the-match name argument" separated by semicolons. A name is one of the
 *        HUD's own: Scoreboard 1/0 (hold, release), EnhancedReality (the original's F9: objective markers on
 *        or off), PreviewDamage (plays the HUD's feedback of a hit without one), PreviewEnd 0/1 (shows the end
 *        banner as if the spies or the mercs had won; -1 takes it away); or Fire 1/0 (hold the fire key, let
 *        go); or any command of the player's bindings (SwitchHeadset 1 or 2: the side's two vision modes, 3
 *        cycles; ShowMenu; SwitchLaser ...), which presses the key the current side has it on, so it takes the
 *        path a real key takes. A command no key is bound to is refused with a log line.
 *   -ScctHudLog=S
 *        seconds between the snapshot lines in the log (default 1; 0 turns them off)
 */
UCLASS()
class OPENSCCT_API UScctHudSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UScctHudSubsystem();

	/** What the HUD shows this frame. */
	const FScctHudModel& GetModel() const { return *Model; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleSessionChanged(int32 OldSessionId, int32 NewSessionId);
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);
	void HandleMatchStopped();
	void HandleFrame(float DeltaSeconds);

	/** Copies the bridge's snapshot of the last tick into the model and drains the new messages. */
	void ReadSnapshot();

	/** Places the objective markers on the screen for this frame's camera. */
	void ProjectMarkers();

	void ShowWidget();
	void HideWidget();
	void RunTimedCommands();
	void LogSnapshot(float DeltaSeconds);

	struct FTimedCommand
	{
		float Time = 0.0f;
		FString Name;
		int32 Argument = 0;
		bool bDone = false;
	};

	TSharedRef<FScctHudModel> Model;
	const FScctHudApi* Api = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UScctHudWidget> Widget;

	TArray<FTimedCommand> TimedCommands;
	bool bScoreboardForced = false;

	/** HoldScoreboard is down; ShowConnectedPlayers has switched the scoreboard on. */
	bool bScoreboardKeyHeld = false;
	bool bScoreboardToggled = false;

	/** The controller whose interface commands are listened to. */
	TWeakObjectPtr<class AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;
	bool bSnapshotRead = false;
	FIntPoint ViewportSize = FIntPoint::ZeroValue;
	float LogEvery = 1.0f;
	float LogTimer = 0.0f;
};
