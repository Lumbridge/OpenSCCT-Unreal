#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctVoiceApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Voice/ScctVoiceCapture.h"
#include "Voice/SScctVoiceOverlay.h"
#include <atomic>
#include "ScctVoiceSubsystem.generated.h"

class AScctPlayerController;
class FScctVoiceTap;
class UAudioComponent;
class USoundWaveProcedural;

/** One remote talker's playback: a 48 kHz mono procedural wave played 2D on the voice chat sound class. */
USTRUCT()
struct FScctVoiceTalkerOutput
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Component;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> Wave;

	/** Times the audio thread found the wave empty (it then plays silence), counted on that thread. */
	TSharedPtr<std::atomic<int32>> Underruns;

	FString Name;
	/** Frames read from the bridge in the current second, and the sum of their squared samples. */
	int32 SecondFrames = 0;
	double SecondSquares = 0.0;
	int64 Frames = 0;
};

/**
 * Voice chat in Unreal: the microphone into the bridge's voice client, other players' voices out, and the voice keys.
 * The talking itself (Opus, the server's relay, the jitter buffer, the activation gate, the clean-up) is the bridge's
 * (Bridge/OpenScct.UnrealBridge/Voice, Runtime/ScctVoiceApi.h); this is the Godot client's VoiceAudio, VoiceInput and
 * OnlineVoiceChat on Unreal's audio. A game instance subsystem, so it lives across the lobby and the rounds as the
 * visit does.
 *
 * CAPTURE. While the voice client has somewhere to send (an online visit, or the Voice page's microphone test) and
 * voice chat is on, the input device chosen on the Voice page is open (FScctVoiceCapture); its samples go to the
 * bridge once a frame at the device's rate. Two seconds after there is nowhere to send, the device is closed again,
 * so the game never holds the microphone in a local match; turning voice chat off closes it at once. The Voice page's
 * level meter (SetMeterWanted) also opens it, whether voice chat is on or not, as Godot's page does. Self mute and the activation gate are the bridge's.
 *
 * PLAYBACK. One UAudioComponent per remote talker, made when his first frames are waiting and dropped when he is no
 * longer a talker (he left, or the visit moved on). Each plays a USoundWaveProcedural at 48 kHz mono on the Voice
 * sound class of UScctAudioMix, so "Other players' voice volume" (Voice.OutputVolume) is its submix's gain; the player's volume for each
 * talker is already in the samples ReadFrame hands out. Once a frame the game thread tops each wave up to three 20 ms
 * frames: the audio output's pace decides how fast the jitter buffer is read, as Godot's generators do. Without an
 * audio device (-nosound) the frames are read by the clock, 50 a second, so the voice still flows and is logged.
 *
 * ECHO REFERENCE. With echo cancellation on, the main submix (what the speakers play) is tapped, mixed to mono at
 * 48 kHz on the audio thread and handed to the bridge's SubmitPlayback on the game thread.
 *
 * KEYS. The local player's interface commands (AScctPlayerController::OnLocalCommand): PushToTalk and TeamPushToTalk
 * are held keys (press and release), ToggleVoiceMute flips Voice.MicMuted and keeps it. With push to talk off, the
 * bridge's activation gate transmits when the microphone is loud enough; holding a talk key also forces it.
 *
 * OPTIONS on the settings page "Voice" (Voice/ScctVoiceSettings.h), applied at once: a new input device reopens
 * the capture, "Restrict Mic to Team" goes to the server of the visit (and to every server joined later), and
 * Voice.Test runs Godot's "Test my microphone" (three seconds recorded through the bridge's relay, then played back
 * as another player hears it) and switches itself off when it is over. The device list is looked at every five
 * seconds; when it changes the device choice is registered again. "Other players' voice volume" is UScctAudioMix's.
 *
 * Unattended switches:
 *   -ScctVoiceTone=Hz          a sine at 0.3 instead of the microphone (the device is never opened; the clean-up
 *                              is off for it, since noise suppression takes a steady tone away, as in Godot)
 *   -ScctVoiceTalk             the all channel's talk key is held for the whole run
 *   -ScctVoiceEcho[=Seconds]   starts the microphone test when not online: Seconds recorded (default 3), then played
 *                              back through the talker playback; 0 for live loopback
 *   -ScctVoicePushToTalk=0|1   forces push to talk off or on over the saved option
 *   -ScctVoiceMeter            the Voice page's level meter is on for the whole run
 *   -ScctVoiceSet="K=V;S:K=V"  sets options (any registered key, "default" for its default), each at S seconds
 *                              after the voice table came up (0 when no "S:"), as the Options screen would (so
 *                              they are kept in the user settings); "S:options[=Page]" logs a page at that time
 *   -ScctVoiceOptions[=Page]   logs every option of the page (default Voice) with its default and value
 *   -ScctVoiceOverlay          the speaker overlay everywhere (a local match too), with made-up talkers and the
 *                              muted microphone line beside any real ones
 *   -ScctVoiceOverlayInput="S:Line:click|up|down;.."  a real mouse event through Slate on the overlay's line
 *                              (0 the top) S seconds after the voice table came up (with -ScctMatchMenu to free the
 *                              cursor), logged with what Slate made of it
 *   -ScctVoiceAutoHack=0|1     the controls' "auto-hack enemy communications" on or off for the run (applied, not
 *                              saved), so hacked comms are heard without the key
 *   -ScctVoiceMute=Name[;Name] those players at volume 0 by name (still received, played as silence), saved
 *   -ScctVoiceVolume=Name=V[;..] those players at volume V (0 to 2) by name, saved
 *
 * OVERLAY. In a round of an online visit, SScctVoiceOverlay on the game viewport (Voice/ScctVoicePlayers.cpp). Each
 * heard enemy is marked by the link the server let him through (the talker row's Link): hacked comms, the scope's
 * voice trace, downed chat or grab talk; the own line says when the microphone talks to a downed or held enemy.
 *
 * SPECIAL CASES. The server routes them from its copy of the controllers (OpenSCCT SessionVoiceLinks): the Hack Comm
 * binding is the simulation's bSpecialsChat button and auto-hack the controls' AutoHackComms option, both sent to the
 * server by the round's own controller (ReplicateSpecialsChatOnServer, ServerSetAutoHackComms); the voice trace of the
 * scope and binoculars runs in the simulation too. The bridge adds one thing: answering the HUD's Speak prompt with the
 * Hack Comm key talks on the all channel even with push to talk on (downed chat and grab talk only carry that channel).
 * Console: scct.Voice.Set <Key> <Value|default>, scct.Voice.Options [Page].
 * Log lines start with "ScctVoice:": the capture (device, rate), once a second while the voice client is connected
 * the frames captured and sent and the talkers, and for every talker heard that second "ScctVoice: heard talker=..
 * rms=..", plus the voice submix's output level.
 */
UCLASS()
class OPENSCCT_API UScctVoiceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UScctVoiceSubsystem();
	virtual ~UScctVoiceSubsystem() override;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Puts the Voice page's options into the settings registry (harmless when they are already in). */
	static void RegisterSettings();

	/** The voice client in one read (zeros when the bridge has no voice table). */
	FScctVoiceStatus GetStatus() const { return Status; }

	/** The talkers as of this frame. */
	const TArray<FScctVoiceTalker>& GetTalkers() const { return Talkers; }

	/**
	 * The Voice page's microphone level bar (Godot's 0 to 0.25 bar): while the page shows, the menus call
	 * SetMeterWanted(true) and read GetInputLevel each frame. Outside a visit and the test this opens the microphone
	 * for the meter alone; nothing is sent. Online and during the test the level is the one being sent.
	 */
	void SetMeterWanted(bool bWanted);
	/** RMS of the last microphone frame after the clean-up and the gain, 0 to 1 (what the activation gate sees). */
	float GetInputLevel() const { return Status.InputLevel; }
	/** The label beside the level bar: "muted", "push to talk", "transmitting" or "silent". */
	FText GetMicState() const;

	/** The microphone test (Voice.Test) as Godot's page words it: "Recording - speak now", "Playing it back"... */
	FText GetTestState() const { return TestState; }
	bool IsTestRunning() const { return bTestRunning; }

	// ---- other players (Voice/ScctVoicePlayers.cpp) -----------------------------------------------------------
	//
	// The local player's choices for each other player, by name so they hold from map to map: a volume from 0 to 2
	// (10% steps from the overlay, kept across runs in Saved/VoiceVolumes.json as Godot's PlayerVoiceVolumeStore does)
	// and a mute (the server stops sending him; kept for the rest of the run). The lobby screen reads a player's row
	// with GetPlayerVoice and acts with SetPlayerVolume, SetPlayerMuted and, for the host, SetVoiceBanned;
	// SScctVoiceLobbyControls is a ready-made row of those controls.

	/** One player's speaking, level, volume and mute (zeros and volume 1 when the bridge has no voice table). */
	FScctVoicePlayer GetPlayerVoice(const FString& Name) const;
	/** The local player's volume for Name, 0 to 2: at once on the current map, and saved. */
	void SetPlayerVolume(const FString& Name, float Volume);
	void SetPlayerMuted(const FString& Name, bool bMuted);
	/** The host's (or an admin's) Voice Ban of a lobby player; the server refuses anyone else. False with no visit. */
	bool SetVoiceBanned(int32 PlayerId, bool bBanned);
	/** Godot's lobby line about the microphone: "Mic transmitting, push to talk", "Your microphone is muted"... */
	FText GetMicrophoneLine() const;
	/** Lobby or round, and the local player's team, as of this frame. */
	FScctVoiceContext GetContext() const { return Context; }

private:
	void InitPlayers();
	void StartPlayers();
	void UpdateOverlay();
	void ShowOverlay(bool bShow);
	TArray<FScctVoiceOverlayLine> BuildOverlayLines() const;
	void ToggleOverlayMute(const FString& Player);
	void AdjustOverlayVolume(const FString& Player, int32 Steps);
	void RunOverlayInputs();

	bool Tick(float DeltaTime);
	void BindCommands();
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);
	void HandleSettingChanged(FName Key);
	void ApplySettings();
	void SendKeys();
	void RefreshDevices();
	void ParseScheduledSettings(const FString& Text);
	void RunScheduledSettings();
	void SendMeter();
	void SyncTeamOnly();
	void HandleTestSwitch();
	void FollowTest();

	void UpdateCapture(float DeltaTime);
	void FeedTone(float DeltaTime);
	void PumpPlayback(float DeltaTime);
	void PumpEchoReference();
	FScctVoiceTalkerOutput* CreateOutput(const FScctVoiceTalker& Row);
	void DestroyOutput(FScctVoiceTalkerOutput& Output);
	void ClearOutputs();
	void EnsureTaps();
	void RemoveTaps();
	void LogSecond();
	void ReportError();

	const FScctVoiceApi* Api = nullptr;
	float ApiRetry = 0.0f;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle SettingsHandle;

	FScctVoiceStatus Status = {};
	TArray<FScctVoiceTalker> Talkers;

	UPROPERTY(Transient)
	TMap<int32, FScctVoiceTalkerOutput> Outputs;

	TUniquePtr<FScctVoiceCapture> Capture;
	TArray<float> CaptureBlock;
	int32 SentCaptureRate = 0;
	/** Seconds the voice client has had nowhere to send while the device is open. */
	float IdleSeconds = 0.0f;
	/** The device to retry after a failure, and when. */
	float CaptureRetry = 0.0f;
	FString OpenedDevice;

	/** Frames owed to the clock when there is no audio device. */
	double ClockFrames = 0.0;

	/** The main submix tap (echo reference) and the voice submix tap (the output level), on the main audio device. */
	TSharedPtr<FScctVoiceTap, ESPMode::ThreadSafe> ReferenceTap;
	TSharedPtr<FScctVoiceTap, ESPMode::ThreadSafe> OutputTap;
	uint32 TapDevice = 0;
	TArray<float> ReferenceBlock;
	/** Echo cancellation is on: the main submix goes back to the bridge. */
	bool bEchoReference = false;

	TWeakObjectPtr<AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;
	bool bTalkHeld = false;
	bool bTeamHeld = false;
	bool bSentTalk = false;
	bool bSentTeam = false;
	bool bKeysSent = false;

	/** -ScctVoiceTone (0: the microphone), -ScctVoiceTalk, -ScctVoiceEcho (seconds, -1 none), -ScctVoicePushToTalk (-1 none). */
	float ToneHz = 0.0f;
	double TonePhase = 0.0;
	double ToneOwed = 0.0;
	bool bForceTalk = false;
	/** -ScctVoiceAutoHack=0|1: the controls' auto-hack option forced for the run (-1: left alone); not saved. */
	int32 ForceAutoHack = -1;
	bool bAutoHackForced = false;
	int32 EchoSeconds = -1;
	bool bEchoStarted = false;
	int32 ForcedPushToTalk = -1;

	/** The input devices last registered as the device choice ("Default" not among them). */
	TArray<FString> KnownDevices;
	bool bDevicesKnown = false;
	float DevicePoll = 0.0f;
	/** The device choice is being registered again: its index moves to the same device, not a new pick. */
	bool bSyncingDevices = false;

	/** The capture was wanted last frame, and for what (the log says when either changes). */
	bool bCaptureWanted = false;
	const TCHAR* CaptureReason = nullptr;

	/** The level meter: asked for by the menus or -ScctVoiceMeter, and what the bridge was last told. */
	bool bMeterWanted = false;
	bool bForceMeter = false;
	bool bMeterSent = false;

	/** "Restrict Mic to Team" as the server last had it (-1 no visit), and the option following it. */
	int32 LastServerTeamOnly = -1;
	bool bSyncingTeamOnly = false;

	/** Voice.Test's microphone test is running, and its words for the page. */
	bool bTestRunning = false;
	FText TestState;

	/** -ScctVoiceSet: option changes at seconds after the voice table came up; -ScctVoiceOptions: a page to log. */
	struct FScheduledSetting
	{
		float Seconds = 0.0f;
		FString Assignment;
	};
	TArray<FScheduledSetting> ScheduledSettings;
	float ReadyTime = 0.0f;
	FString OptionsToLog;

	/** The speaker overlay on the game viewport (null while it is not shown) and the viewport it is on. */
	TSharedPtr<SScctVoiceOverlay> Overlay;
	TWeakObjectPtr<class UGameViewportClient> OverlayViewport;
	/** -ScctVoiceOverlay: shown everywhere, with made-up talkers beside any real ones. */
	bool bForceOverlay = false;
	struct FFakeTalker
	{
		FString Name;
		int32 Team = 0;
		/** EScctVoiceLink. */
		int32 Link = 0;
		float Volume = 1.0f;
		bool bMuted = false;
	};
	TArray<FFakeTalker> FakeTalkers;
	/** -ScctVoiceOverlayInput: mouse events sent through Slate onto a line, at seconds after the voice table came up. */
	struct FOverlayInput
	{
		float Seconds = 0.0f;
		int32 Line = 0;
		/** "click", "up" or "down" (the wheel). */
		FString Action;
	};
	TArray<FOverlayInput> OverlayInputs;
	FString LastOverlayText;
	FScctVoiceContext Context = { 0, 0, -1, 0, 0, 0, 0 };
	/** Where the per-player volumes are kept, and -ScctVoiceMute / -ScctVoiceVolume's names and volumes. */
	FString VolumeFile;
	TArray<TPair<FString, float>> SwitchVolumes;
	bool bPlayersStarted = false;

	float LogTimer = 0.0f;
	int64 LastFramesSent = 0;
	int64 LastFramesCaptured = 0;
	int32 LastEchoPhase = 0;
	int32 LastConnected = 0;
	FString LastError;
};
