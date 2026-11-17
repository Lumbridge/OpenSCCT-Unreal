#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Play/ScctControlAutoplay.h"
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
 * PushToTalk, Quit...) arrive through OnLocalCommand, on the press and again on the release.
 *
 * CONSOLE (until there is a settings menu):
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
 * While it is given the game counts as having the input even though its window has no focus. Steps:
 *   forward|back|left|right|fire|altfire|jump|crouch|use|roll|reload|weapon|walk|firstperson [seconds]
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

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * The controls' view of this frame as the camera drew it; Valid is 0 while there is none. Beyond the camera:
	 * AimPitch and AimYaw (where the local spy's arm and pistol point), RifleAimBlend (the merc's aim zoom, for
	 * the first-person gun), Cut and Occluded.
	 */
	const FScctLookView& GetLookView() const { return LookView; }

	/** The controller did something in the last frame: show its button prompts instead of the keys'. */
	bool IsPadActive() const;

	/** The game has the keys and the mouse this frame (focused, no menu with a cursor up). */
	bool HasPlayInput() const;

	FScctLocalCommandEvent OnLocalCommand;

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
	/** The session's OnBeforeTicks: hands the frame's devices to the bridge's controls. */
	void FeedControls(float DeltaSeconds);

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
