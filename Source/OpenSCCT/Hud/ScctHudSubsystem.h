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
 * Keys read here: Tab holds the scoreboard.
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard):
 *   -ScctHudCommands="3 SwitchHeadset 1;9 Scoreboard 1;14 Scoreboard 0"
 *        timed commands, "seconds-into-the-match name argument" separated by semicolons. A name is one of the
 *        simulation's exec commands (SwitchHeadset 1 or 2: the side's two vision modes, 3 cycles; NextGadget;
 *        SwitchROF ...), or one of the HUD's own: Scoreboard 1/0 (hold, release), EnhancedReality (the
 *        original's F9: objective markers on or off), Fire 1/0 (hold the trigger, let go; while held it
 *        replaces the player's own input), PreviewDamage (plays the HUD's feedback of a hit without one),
 *        PreviewEnd 0/1 (shows the end banner as if the spies or the mercs had won; -1 takes it away).
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
	void HandleBeforeTicks(float DeltaSeconds);
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
	bool bFireForced = false;
	bool bSnapshotRead = false;
	FIntPoint ViewportSize = FIntPoint::ZeroValue;
	float LogEvery = 1.0f;
	float LogTimer = 0.0f;
};
