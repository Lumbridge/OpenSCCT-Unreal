#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctVoiceApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Voice/ScctVoiceCapture.h"
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
 * so the game never holds the microphone in a local match. Self mute and the activation gate are the bridge's.
 *
 * PLAYBACK. One UAudioComponent per remote talker, made when his first frames are waiting and dropped when he is no
 * longer a talker (he left, or the visit moved on). Each plays a USoundWaveProcedural at 48 kHz mono on the Voice
 * sound class of UScctAudioMix, so the "Voice chat volume" slider is its submix's gain; the player's volume for each
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
 * OPTIONS on the settings page "Voice" (see RegisterSettings), applied at once. "Voice chat volume" is on the Audio
 * page with the other volumes.
 *
 * Unattended switches:
 *   -ScctVoiceTone=Hz          a sine at 0.3 instead of the microphone (the device is never opened; the clean-up
 *                              is off for it, since noise suppression takes a steady tone away, as in Godot)
 *   -ScctVoiceTalk             the all channel's talk key is held for the whole run
 *   -ScctVoiceEcho[=Seconds]   starts the microphone test when not online: Seconds recorded (default 3), then played
 *                              back through the talker playback; 0 for live loopback
 *   -ScctVoicePushToTalk=0|1   forces push to talk off or on over the saved option
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

private:
	bool Tick(float DeltaTime);
	void BindCommands();
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);
	void HandleSettingChanged(FName Key);
	void ApplySettings();
	void SendKeys();

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
	int32 EchoSeconds = -1;
	bool bEchoStarted = false;
	int32 ForcedPushToTalk = -1;

	float LogTimer = 0.0f;
	int64 LastFramesSent = 0;
	int64 LastFramesCaptured = 0;
	int32 LastEchoPhase = 0;
	int32 LastConnected = 0;
	FString LastError;
};
