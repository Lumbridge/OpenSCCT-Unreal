#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Play/ScctControlAutoplay.h"
#include "Play/ScctLookStickHold.h"
#include "Runtime/ScctBridgeApi.h"
#include "Runtime/ScctControlsApi.h"
#include "ScctPlayerController.generated.h"

class UScctSessionSubsystem;

/** An interface command a key binding raised: its name (ShowMenu, Talk, HoldScoreboard...), the number in the binding, press or release. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FScctLocalCommandEvent, FName /*Name*/, int32 /*Arg*/, bool /*bPressed*/);

/**
 * The local player's devices and camera.
 *
 * INPUT. Unreal decides nothing about what a key does. Once a frame, in the session's OnBeforeTicks (after
 * Unreal has processed the frame's input events, before the simulation's ticks), this controller hands the
 * bridge's controls the devices as they are: every key and button of EScctKey by its Unreal key, the mouse
 * counts, the wheel, the sticks and triggers, and the frame's length (FScctControlsApi::Frame). The bridge
 * applies the bindings, the hold, tap and toggle rules and the sensitivity settings and writes the simulation's
 * input itself. The core table's SetInput and Command are not used: mixing the two would overwrite the mapper.
 *
 * RAW MOUSE. The mouse counts are UPlayerInput::GetRawKeyValue(EKeys::MouseX / MouseY): the sum of the frame's
 * mouse events exactly as the viewport raised them, taken before UPlayerInput::MassageAxisInput applies the axis
 * sensitivity (0.07 by default), the field of view scaling and mouse smoothing. APlayerController's
 * GetInputMouseDelta and GetInputAnalogKeyState return the value after those and are not used. While the game
 * viewport has captured the mouse (the cursor is hidden) Slate runs it in high precision mode, so those events
 * are the device's own counts (Windows raw input, no pointer acceleration). Unreal's MouseY is already positive
 * for a mouse pushed away, which is what the bridge wants. The sticks and triggers are read raw as well, because
 * Unreal's default axis settings put a dead zone on the sticks and the bridge applies its own.
 *
 * CAMERA. The session asks this controller for the frame's camera (its ViewOverride): the controls' view, which
 * is the simulation's view turned by the mouse counts the simulation has not taken yet, so the picture follows
 * the mouse with no tick of delay. The field of view comes with the simulation's zoom, scope and rifle aim
 * blends and the widescreen setting already applied. A view that does not continue the last one (the bridge's
 * Cut, or a jump the bridge did not flag because several ticks ran in the frame) is told to Unreal as a camera
 * cut, so motion blur and temporal anti-aliasing do not smear across it. GetLookView has the rest of it for other features (where
 * the local player aims, the rifle aim blend).
 *
 * INTERFACE COMMANDS. Bindings that are not for the pawn (ShowMenu, Talk, HoldScoreboard, DisplayStrategicMap,
 * PushToTalk, Quit...) arrive through OnLocalCommand, on the press and again on the release. The game mode opens
 * the in-match menu on ShowMenu; the HUD takes the scoreboard, the mission status page, the chat line, the
 * teammate names and SwitchEnhancedReality (Hud/ScctHudSubsystem.h lists them). Nothing reads a fixed key for any
 * of these.
 *
 * MENUS. While a menu is open (SetMenuOpen) the bridge is told the game does not have the input: everything held
 * is released and no key or mouse movement reaches the player. The keys bound to a command can still be asked for
 * (FindKeysBoundTo), which is how the menu's own key closes it again.
 *
 * CONSOLE (the settings page's Controls and Key bindings tabs, Menu/SScctSettingsPanel.h, change the same values):
 *   ScctInvertY 0|1        look up and down the other way round
 *   ScctMouseScale <x>     a multiplier over every mouse count
 *   ScctBind spy|merc <key> "<command>"   bind a key for one side: the key by its binding name (W, Shift,
 *                          LeftMouse, Ctrl+F, Double+H), the command in the original's grammar ("Button bCrouch");
 *                          "" unbinds
 *   ScctBindReset spy|merc put that side's keys and controller back to the defaults
 *   ScctControlsSave       write the settings and the bindings to Saved/Controls
 *   ScctControlsLoad       read them again
 *
 * UNATTENDED SWITCH (off by default; for runs with nobody at the keyboard):
 *   -ScctAutoplay="<step>;<step>;..."
 * plays keys and mouse movements once the local player has a body. The steps become ordinary Unreal input events
 * on this controller (see ScctControlAutoplay.h), so they take the same path as real keys, bindings included.
 * While it is given the game counts as having the input even though its window has no focus (and so it does
 * once another feature's unattended switch has used HoldScriptedAction or TapBoundCommand). Steps:
 *   forward|back|left|right|fire|altfire|jump|crouch|use|roll|reload|weapon|walk|firstperson|freelook [seconds]
 *                            hold the key the default bindings put that action on (W S A D, left and right
 *                            mouse button, Space, C, Shift, Q, R, E, middle mouse button, X); 0.1 s when no
 *                            time is given; several joined with + are held together ("forward+left 2")
 *   key <UnrealKeyName> [seconds]   hold any key by its Unreal name ("key Tab", "key F 0.5")
 *   turn <degrees> [seconds] [actions]   move the mouse sideways, right positive, over the time (0.5 s); the
 *                                   degrees are those of the side's plain look (spy with the gun holstered, merc
 *                                   unscoped); actions named after the seconds are held meanwhile
 *                                   ("turn 90 1 forward" runs round a corner)
 *   look <degrees> [seconds] [actions]   move the mouse away (up positive)
 *   wheel <notches>                 turn the wheel, up positive
 *   wait [seconds]                  nothing (1 s)
 * Each step's start, and the end of the script, writes a LogOpenSCCT line "Autoplay: ..." with the local pawn's
 * place, yaw, height and health, the view (with the field of view Unreal really draws), the simulation's state
 * name, the merc's ammunition and fire mode, the selected gadget and the vision mode, and since the line before: the largest frame-to-frame change of the view, how far
 * the view's turn was from the mouse counts of its own frame (the look-ahead check) and the camera cuts raised.
 * Example: -ScctAutoplay="wait 2;forward 2;turn 90;crouch 1;jump;wait 1;fire 0.5"
 */
UCLASS()
class OPENSCCT_API AScctPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AScctPlayerController();

	/** Current step of the unattended input route. Lets benchmark tools time gameplay without reading a log every frame. */
	UFUNCTION(BlueprintPure, Category="OpenSCCT|Testing")
	int32 GetAutoplayProgress() const { return Autoplay.GetProgress(); }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetInputMode(const FInputModeDataBase& InData) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual bool PostProcessWorldToScreen(FVector WorldLocation, FVector2D& ScreenLocation, bool bPlayerViewportRelative) const override;

	/**
	 * The controls' view of this frame as the camera drew it; Valid is 0 while there is none. Beyond the camera:
	 * AimPitch and AimYaw (where the local spy's arm and pistol point), RifleAimBlend (the merc's sight pose, for
	 * the first-person gun), Cut and Occluded.
	 */
	const FScctLookView& GetLookView() const { return LookView; }
	/** Viewing angle relative to the merc's aim while looking around with the body camera. */
	const FRotator& GetMercFreeLookOffset() const { return MercFreeLookOffset; }
	/** This frame's player-driven aim turn in degrees (yaw, pitch), after sensitivity and pitch limits. */
	FVector2D GetMercAimTurn() const { return MercAimTurn; }

	/** The controller did something in the last frame: show its button prompts instead of the keys'. */
	bool IsPadActive() const;

	/** The game has the keys and the mouse this frame (focused, no menu open). */
	bool HasPlayInput() const;

	/**
	 * A menu over the match opened or closed. While one is open the player's keys and mouse do not reach the
	 * simulation, whoever is at the keyboard (a person, an autoplay script, another unattended switch).
	 */
	void SetMenuOpen(bool bOpen);
	bool IsMenuOpen() const { return bMenuOpen; }

	/**
	 * The Unreal keys the current side's bindings put a command on, compared by the command's name alone
	 * ("ShowMenu") or with its number ("SwitchHeadset 1"). Keys bound with a modifier (Ctrl+F) are left out.
	 */
	TArray<FKey> FindKeysBoundTo(const FString& Command) const;

	/**
	 * For the unattended switches of other features (-ScctAutoFire, -ScctAutoWalk, -ScctHudCommands): holds or lets
	 * go the key the default bindings put one of the autoplay's named actions on ("fire", "altfire", "forward"...),
	 * as an ordinary Unreal input event, so it takes the same path as a real key and never fights the bridge's
	 * input mapper. False for a name that is not an action.
	 */
	bool HoldScriptedAction(const FString& Action, bool bHeld);

	/**
	 * The same for a command of the bindings ("SwitchHeadset 1", "ShowMenu"): presses the key the current side
	 * has it on and lets it go a frame later. False (and a log line) when no plain key is bound to it.
	 */
	bool TapBoundCommand(const FString& Command);

	/** The same, held down (true) or let go (false) until called again, for a held command such as TeamPing. */
	bool HoldBoundCommand(const FString& Command, bool bHeld);

	FScctLocalCommandEvent OnLocalCommand;

	/**
	 * While bound and returning true for a frame, that frame's mouse counts and right stick (X, Y, right X, right Y as
	 * ReadDevices has them) go to it instead of the simulation: the view does not turn. The team ping wheel chooses
	 * with them while it is open.
	 */
	DECLARE_DELEGATE_RetVal_FourParams(bool, FScctLookSink, float /*MouseX*/, float /*MouseY*/, float /*RightX*/, float /*RightY*/);
	FScctLookSink LookSink;

	UFUNCTION(Exec)
	void ScctInvertY(int32 bInvert);

	UFUNCTION(Exec)
	void ScctMouseScale(float Scale);

	UFUNCTION(Exec)
	void ScctBind(const FString& Side, const FString& Key, const FString& Command);

	UFUNCTION(Exec)
	void ScctBindReset(const FString& Side);

	UFUNCTION(Exec)
	void ScctControlsSave();

	UFUNCTION(Exec)
	void ScctControlsLoad();

private:
	// All projected HUD anchors share the camera's constrained view rectangle for this frame.
	mutable uint64 LensProjectionFrame = MAX_uint64;
	mutable FIntRect LensProjectionRect = FIntRect(0, 0, 0, 0);
	/** The session's OnBeforeTicks: hands the frame's devices to the bridge's controls. */
	void FeedControls(float DeltaSeconds);

	void UpdateMercFreeLook(const FScctRawInput& Raw);
	FRotator MercFreeLookOffset = FRotator::ZeroRotator;
	FVector2D MercAimTurn = FVector2D::ZeroVector;

	/** The session's ViewOverride: replaces the bridge's view with the controls' look-ahead view. */
	void OverrideView(FScctBridgeView& View);

	/** Fills Raw with the keys, buttons, mouse, wheel, sticks and triggers as Unreal has them this frame. */
	void ReadDevices(FScctRawInput& Raw) const;

	/** Passes on the interface commands the bindings raised this frame. */
	void TakeLocalCommands(const FScctControlsApi& Controls);

	/** The session's OnSessionChanged: drops what belonged to the match before. */
	void ForgetView(int32 OldSessionId, int32 NewSessionId);

	/** Mouse counts that turn the current side's plain look by one degree (for the autoplay's "turn"). */
	float GetMouseCountsPerDegree() const;

	/** The autoplay's log line: where the local player is and what the view shows. */
	void ReportAutoplay(const FString& Label);

	TWeakObjectPtr<UScctSessionSubsystem> Session;
	TSet<FKey> GameplayMousePresses;
	TArray<FScctKeyEdge> KeyEdges;
	TSet<FKey> HeldModifierKeys;
	FDelegateHandle BeforeTicksHandle;
	FDelegateHandle SessionChangedHandle;

	/** The missing controls table has been reported (once, not every frame). */
	bool bReportedNoControls = false;

	FScctLookView LookView = {};

	/** The last frame's view was a cut, so the next cut frame is the same cut still showing. */
	bool bWasCut = false;
	bool bWasOccluded = false;

	/** The names behind the command ids the controls hand over, each asked for once. */
	TMap<int32, FName> CommandNames;

	FScctControlAutoplay Autoplay;

	/** Keeps the look stick from turning the view after the ping wheel closes until it recentres. */
	FScctLookStickHold LookStickHold;

	/** Another feature's unattended switch plays keys (HoldScriptedAction, TapBoundCommand): the game has the input. */
	bool bScripted = false;

	/** Keys TapBoundCommand pressed, each let go on a later frame. */
	TArray<FKey> TappedKeys;

	bool bMenuOpen = false;

	/** The local player has had a body, so the script runs (and keeps running through a death). */
	bool bAutoplayStarted = false;

	/** The largest turn and move of the camera from one frame to the next since the last autoplay line. */
	float LargestViewTurn = 0.0f;
	float LargestViewMove = 0.0f;

	/** There was a view last frame, so this frame's can be compared with it. */
	bool bHadLookView = false;

	/** The mouse counts handed to the bridge this frame, and how far the view's turn has been from them. */
	float FrameMouseX = 0.0f;
	float LargestTurnError = 0.0f;

	/** Camera cuts told to Unreal since the last autoplay line. */
	int32 CutsRaised = 0;
};
