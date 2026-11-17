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
 *   ToggleTeammateNames     names over the heads of the player's own side, seen through walls, kept per side in
 *                           the Hud page's options (N), with health and a nametag opacity there too
 * PushToTalk, TeamPushToTalk and ToggleVoiceMute are the voice chat's, not the HUD's.
 * Talk and TeamTalk are the chat's (Hud/ScctChatSubsystem.h), which draws its composer and transcript over the HUD.
 * Not served, as in the Godot client, which lists both as legacy shortcuts with no handler: DisplayHud and Quit.
 * DisplayStrategicMap is the strategic map page's (Menu/ScctMatchCommands.h).
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
 *        PreviewCamAlert 1/0 shows or hides the camera network page's Alert box without a spy hacking.
 *        PreviewCoop N shows coop icon N (1 offer a ladder, 2 climb on the mate, 3 offer a hoist, 4 be boosted up;
 *        0 takes it away) as if the game offered it. Die kills the local pawn on the local match's server (lives
 *        left as they are), e.g. to see the Merc Deathmatch respawn weapon pick.
 *        Heart 1 selects the local spy's heart beat sensor; Weapon 1/0 holds or lets go of the weapon key (a spy
 *        draws the Shocker with it, which starts the sensor's scan). RadarRotate 0/1 and RadarScale P (percent) set
 *        the player's radar options for the run without saving them.
 *        GiveBackpack 1/2 gives the local merc (2: every merc of his side) the backpack in his last gadget slot on the
 *        local match's server, as a gear change would; Backpack 1 selects it; Refill S holds the mate carrying the
 *        player's share in front of him for S seconds with one of his charges spent and the refill entry focused, as
 *        when the player opens the interaction menu next to him.
 *        Summon 0/1 puts the nearest pawn of the other side (0) or of the player's side (1) a few metres in front of
 *        the local pawn on the local match's server (the bots start across the map), so the merc's motion tracking has
 *        a moving target to lock; AimMate S keeps the player's view and aim on his nearest teammate for S seconds (the
 *        laser gadget's target, whose bracket gets the friend marks); Laser 1/0 switches the merc's laser gadget on
 *        or off by the simulation's GadLaserSwitch (its default key also presses the roll button, so a key press would do more).
 *        PingKey 1/0 holds or lets go of the key bound to TeamPing (UScctTeamPingSubsystem: a tap marks the aimed spot,
 *        a hold opens the wheel); PingWheel D points the wheel's choice as the mouse would (1 up, 2 right, 3 down,
 *        4 left, 0 the middle).
 *        Tutorial 1/0 raises the -ScctHudTutorial text as the tutorial message again, or clears it, through the
 *        game's own SHud.AddTutoMapMessage (offline only, as the zone tips).
 *   -ScctHudTutorial=<text>[|At]
 *        at At seconds into the match (default 4) raises that text as the tutorial message, through the game's own
 *        SHud.AddTutoMapMessage (the zone tips' path), so the tutorial box and the briefing queue's wait can be checked
 *        on any map ("ScctHud: tutorial ..." lines in the log).
 *   -ScctHudTexture=Package.Name[,Package.Name...]
 *        loads those textures of the content as the session starts and logs their size ("ScctHudTextures: ...").
 *   -ScctHudLog=S
 *        seconds between the snapshot lines in the log (default 1; 0 turns them off)
 *   -ScctGadgetMenu=sticky|mine[,At[,Hold[,Down;Down...]]]
 *        opens the spy's sticky cam menu or the merc's mine menu: at At seconds (default 4) the Alt-fire key goes
 *        down, the gadget is selected and the controller put in the menu (nothing in the original's scripts enters
 *        the sticky cam menu, so a check cannot reach it by keys alone), the down key steps the selection at each
 *        Down time, and Alt-fire comes up after Hold seconds (default 8), which runs the selected line as a player's
 *        release would. The menu's rows go to the log as they change ("ScctHud: menu ...").
 *   -ScctGadgetView=sticky|camnet[,At[,Stay]]
 *        looks through a gadget's camera by the player's own keys: at At seconds (default 4) the sticky cam or the
 *        camera network gadget is selected; a spy then draws the Shocker (the weapon key held) and fires the cam with
 *        Alt-fire, a merc uses the network with Alt-fire; Stay seconds later (default 10) Alt-fire leaves the view.
 *        The view's changes go to the log ("ScctHud: gadget view ...", "ScctHud: CamNetworkAlert ...").
 *   -ScctBinoculars=At[,Zoom+Zoom...[,Lower[,Use+Use...]]]   and   -ScctBinocularsAim
 *        raises the spy's binoculars by the player's own keys: at At seconds (default 4) the key bound to
 *        SwitchBinocularVision is pressed, at each Zoom time one notch of the mouse wheel up (one zoom step in), at
 *        each Use time the Use key (locks the target in the lock panel, or unlocks it), and at Lower the binocular key
 *        again (an empty field skips it). With -ScctBinocularsAim the view is turned onto the nearest objective target every half
 *        second while the binoculars are up, so the lock panel shows. The page's changes go to the log
 *        ("ScctHud: binoculars ...": mode, zoom step, the target count and the lock candidate).
 */
UCLASS()
class OPENSCCT_API UScctHudSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UScctHudSubsystem();

	/** What the HUD shows this frame. */
	const FScctHudModel& GetModel() const { return *Model; }

	/** For the HUD's other subsystems that fill a part of the model themselves (the team ping's badges and wheel). */
	FScctHudModel& GetMutableModel() { return *Model; }

	/**
	 * Every message drained from the bridge, as it is drained (once each, oldest first), before the model files it.
	 * The chat takes the Chat and TeamChat lines from here; the model leaves those to it.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FMessageEvent, const FScctHudMessage& /*Message*/);
	FMessageEvent OnMessage;

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

	// The teammate names (ScctHudNames.cpp): the Hud page's options (Hud.ShowTeammateNamesSpy/Merc, Hud.TeammateHealth,
	// Hud.TeammateNamesOpacity), the N key flipping the played side's option, and the labels placed for this frame's
	// camera from the bridge's teammate table (Runtime/ScctTeammateNamesApi.h).
	void InitNames();
	void ToggleTeammateNames();
	void ProjectNames();
	bool AreTeammateNamesOn(int32 Side) const;

	/** Opens or closes the mission status page and holds the player's keys back while it is open. */
	void SetStatusPage(bool bOpen);

	/** Hands the keys and the mouse back to the match after the status page had them. */
	void GiveInputToGame();

	void ShowWidget();
	void HideWidget();
	void RunTimedCommands();
	void LogSnapshot(float DeltaSeconds);

	// The gadget views (ScctHudGadgetViews.cpp): the sticky cam and mine menus, the sticky cam and camera network
	// pages' camera and key help, and the unattended -ScctGadgetMenu check.
	void InitGadgetViews();
	void ReadGadgetViews();
	void RunGadgetChecks(float DeltaSeconds);

	struct FGadgetMenuCheck
	{
		int32 Menu = 0;          // EScctHudGadgetMenu, 0 when off
		float At = 4.0f;         // seconds into the match when Alt-fire goes down and the menu is forced
		float Hold = 8.0f;       // seconds Alt-fire stays down (the menu stays open)
		TArray<float> DownSteps; // seconds into the match when the down key steps the selection
		int32 Stage = 0;         // 0 waiting, 1 in the menu with Alt-fire down, 2 done
		int32 NextStep = 0;
	} GadgetMenuCheck;

	struct FGadgetViewCheck
	{
		int32 Kind = 0;          // EScctHudGadgetKind: StickyCam or CamNetwork, 0 when off
		float At = 4.0f;         // seconds into the match when the gadget is selected
		float Stay = 10.0f;      // seconds in the view before Alt-fire leaves it
		int32 Stage = 0;
		float StageAt = 0.0f;    // when the current stage began
	} GadgetViewCheck;

	// The HUD's pictures (ScctHudIcons.cpp): the coop icon's clip and the respawn pick's Use key into the model, the
	// -ScctHudTexture check, and the timed commands PreviewCoop N and Die.
	void InitIcons();
	void ReadIcons(float DeltaSeconds);
	bool RunIconCommand(const FString& Name, int32 Argument);

	TArray<FString> TextureChecks; // -ScctHudTexture=Package.Name[,Package.Name...]
	bool bTextureChecksDone = false;
	int32 LastCoopIcon = 0;
	FIntPoint LastRespawnTile = FIntPoint(-1, -1);
	float RespawnKeyAge = -100.0f;

	// The radar (ScctHudRadar.cpp): the player's radar options and the heart sensor into the model, the radar's log
	// line, and the timed commands Heart and Weapon. ReadSnapshot reads the blips themselves.
	void InitRadar();
	void ReadRadar();
	bool RunRadarCommand(const FString& Name, int32 Argument);
	void LogRadar();
	int32 LastHeartCone = 0;
	bool bRadarTextureMissingLogged = false;

	// The marker and message details (ScctHudDetails.cpp): the Hud.MarkerFadeWhileMoving option, the tutorial message's
	// table (a tutorial map; the -ScctHudTutorial message raised through the game's own SHud.AddTutoMapMessage), a log
	// line when the tutorial message or the markers' fade changes, and the timed command Tutorial 1/0.
	void InitDetails();
	void ReadDetails();
	bool RunDetailsCommand(const FString& Name, int32 Argument);
	void LogDetails();
	FString TutorialSwitchText;      // -ScctHudTutorial=<text>
	float TutorialSwitchAt = 4.0f;   // -ScctHudTutorial's seconds into the match (4 by default)
	bool bTutorialSwitchDone = false;
	int32 LastLoggedTutorialId = 0;
	int32 LastLoggedFadeStep = -1;
	bool bLastLoggedHeldBriefings = false;

	// The merc backpack panels (ScctHudBackpack.cpp): the bridge's backpack table into the model, a log line when the
	// panels change and with each snapshot line, and the timed commands GiveBackpack, Backpack and Refill.
	void InitBackpack();
	void ReadBackpack();
	bool RunBackpackCommand(const FString& Name, int32 Argument);
	void LogBackpack();
	uint32 LastBackpackSignature = 0;
	bool bBackpackTableMissingLogged = false;

	// The spy binoculars (ScctHudBinoculars.cpp): the page's clocks, key names and layers into the model, the targets
	// placed on the screen, a log line when the page opens, closes or its zoom step or targets change, and the
	// unattended -ScctBinoculars check.
	void InitBinoculars();
	void ReadBinoculars(float DeltaSeconds);
	void ProjectBinocularTargets();
	void RunBinocularCheck();
	void LogBinoculars(bool bChange);

	struct FBinocularCheck
	{
		bool bOn = false;
		float At = 4.0f;         // seconds into the match when the binocular key is pressed
		TArray<float> ZoomAt;    // seconds into the match when the zoom-in key (the wheel's up notch) is pressed
		bool bAim = false;       // keep the view on the nearest target while the binoculars are up
		float Lower = -1.0f;     // seconds into the match when the binocular key is pressed again, below 0 never
		TArray<float> UseAt;     // seconds into the match when the Use key (lock, unlock) is pressed
		int32 Stage = 0;         // 0 waiting, 1 raised, 2 lowered
		int32 NextZoom = 0;
		int32 NextUse = 0;
		bool bUseHeld = false;
		float UseReleaseAt = 0.0f;
		float AimClock = 0.0f;
	} BinocularCheck;

	float BinocularAge = 0.0f;   // seconds since the binoculars came up
	float SpeakAge = -1.0f;      // seconds since the speak box appeared, below 0 while it is not shown
	float BinocularKeysAge = -100.0f;
	FString BinocularZoomKeys, BinocularExitKey;
	int32 LastBinocularMode = 0;
	int32 LastZoomStep = 0;
	int32 LastBinocularTargetCount = -1;
	FString LastLockCandidate;
	bool bLastLockMarked = false;
	bool bBinocularPageMissingLogged = false;

	// The merc's motion tracking (ScctHudMotion.cpp): the view handed to the bridge's motion table each frame and its
	// rows into the model, a log line when the locks change and with each snapshot line, and the timed command Summon.
	void BuildMotion();
	bool RunMotionCommand(const FString& Name, int32 Argument);
	void LogMotion(bool bChange);
	uint32 LastMotionSignature = 0;
	float AimMateUntil = -1.0f;  // AimMate: the clock until which the view is kept on the nearest teammate
	bool bMotionTableMissingLogged = false;

	float GadgetKeysAge = 0.0f;
	int32 LastGadgetView = 0;
	bool bLastCamAlert = false;
	uint32 LastMenuSignature = 0;

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

	/** The keys that close the open status page (those bound to DisplaySessionStatus, Escape, Enter) and whether all have been up since it opened. */
	TArray<FKey> StatusCloseKeys;
	bool bStatusKeysArmed = false;

	/** The timed command TeammateNames 1/0 per side (0 spies, 1 mercs) for this run, -1 where the user's option holds. */
	int32 TeammateNamesForced[2] = { -1, -1 };
	int32 LastNameCount = -1;    // the labels' count and marks as last logged
	bool bNamesTableMissingLogged = false;

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
