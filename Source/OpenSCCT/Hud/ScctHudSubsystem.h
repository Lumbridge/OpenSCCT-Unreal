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
 * and the HUD serves these, as the Godot client's HUD does:
 *   HoldScoreboard          the scoreboard while the key is down
 *   ShowConnectedPlayers    the scoreboard on or off (F10 by default)
 *   SwitchEnhancedReality   objective markers on or off (F9)
 *   DisplaySessionStatus    the mission status page: the scoreboard with the mission's objectives under it. It
 *                           stays open and holds the player's keys back until the same key, Escape or Enter (F8)
 *   Talk, TeamTalk          the chat line, to everyone (Enter or F5) or to the team (F6). Enter sends it through the
 *                           bridge (FScctPlayApi::SendChat); the server relays it back and it shows in the log
 *   ToggleTeammateNames     names over the heads of the player's own side, seen through walls, kept per side in
 *                           the user's settings (N)
 * Not served, as in the Godot client, which lists both as legacy shortcuts with no handler: DisplayHud and Quit.
 * DisplayStrategicMap is not served either: the bridge has no table for the map's plan (see the log line).
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard):
 *   -ScctHudCommands="3 SwitchHeadset 1;9 Scoreboard 1;14 Scoreboard 0"
 *        timed commands, "seconds-into-the-match name argument" separated by semicolons. A name is one of the
 *        HUD's own: Scoreboard 1/0 (hold, release), EnhancedReality (the original's F9: objective markers on
 *        or off), StatusPage 1/0 (the mission status page as DisplaySessionStatus opens it, and closed),
 *        TeammateNames 1/0 (the names over the player's side, for this run only), PreviewDamage (plays the HUD's
 *        feedback of a hit without one), PreviewEnd 0/1 (shows the end
 *        banner as if the spies or the mercs had won; -1 takes it away); or Fire 1/0 (hold the fire key, let
 *        go); or any command of the player's bindings (SwitchHeadset 1 or 2: the side's two vision modes, 3
 *        cycles; ShowMenu; SwitchLaser ...), which presses the key the current side has it on, so it takes the
 *        path a real key takes. A command no key is bound to is refused with a log line.
 *   -ScctHudChat="6 all hello there;11 team go left"
 *        timed chat lines, "seconds-into-the-match all|team text": the chat line opens with the text in it and
 *        is sent a second and a half later, so a screenshot in between shows the open line
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

	/** Places the names over the local player's teammates for this frame's camera, while he has them on. */
	void ProjectNames();

	/** The chat line: opens it (holding the player's keys back), sends what was typed, gives the keys back. */
	void OpenChat(bool bTeamOnly);
	void HandleChatSubmitted(const FString& Text, bool bTeamOnly);
	void HandleChatClosed();

	/** Opens or closes the mission status page and holds the player's keys back while it is open. */
	void SetStatusPage(bool bOpen);

	/** Hands the keys and the mouse back to the match after the chat line or the status page had them. */
	void GiveInputToGame();

	void RunTimedChat(float DeltaSeconds);

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

	struct FTimedChat
	{
		float Time = 0.0f;
		bool bTeamOnly = false;
		FString Text;
		/** 0 waiting, 1 typed into the open line, 2 sent. */
		int32 Stage = 0;
	};

	TArray<FTimedCommand> TimedCommands;
	TArray<FTimedChat> TimedChat;
	bool bScoreboardForced = false;

	/** The keys that close the open status page (those bound to DisplaySessionStatus, Escape, Enter) and whether all have been up since it opened. */
	TArray<FKey> StatusCloseKeys;
	bool bStatusKeysArmed = false;

	/** ToggleTeammateNames, per side (0 spies, 1 mercs), as the user's settings keep it. */
	bool bTeammateNames[2] = { false, false };
	bool bStrategicMapNoted = false;

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
