#include "Voice/ScctVoiceSubsystem.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "ISubmixBufferListener.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctAudioMix.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundWaveProcedural.h"
#include "Voice/ScctVoiceCapture.h"

#define LOCTEXT_NAMESPACE "ScctVoice"

namespace
{
	const FName EnabledKey(TEXT("Voice.Enabled"));
	const FName PushToTalkKey(TEXT("Voice.PushToTalk"));
	const FName MicMutedKey(TEXT("Voice.MicMuted"));
	const FName InputDeviceKey(TEXT("Voice.InputDevice"));
	const FName ThresholdKey(TEXT("Voice.ActivationThreshold"));
	const FName HoldKey(TEXT("Voice.ActivationHoldMs"));
	const FName InputGainKey(TEXT("Voice.InputGain"));
	const FName AutoGainKey(TEXT("Voice.AutoGain"));
	const FName NoiseKey(TEXT("Voice.NoiseSuppression"));
	const FName NoiseLevelKey(TEXT("Voice.NoiseLevel"));
	const FName EchoKey(TEXT("Voice.EchoCancellation"));
	const FName JitterKey(TEXT("Voice.JitterFrames"));
	/** UScctAudioMix's "Voice chat volume", which the voice client keeps as its output volume. */
	const FName OutputVolumeKey(TEXT("Audio.Voice"));

	/** Godot's GameSettings.NoiseLevelDecibels: Low, Standard, High, Extreme. */
	constexpr int32 NoiseDecibels[] = { 15, 30, 45, 60 };

	/** Decoded frames kept queued in a talker's wave (60 ms): the jitter buffer banks, the wave bridges game frames. */
	constexpr int32 QueuedFrames = 3;
	constexpr int32 MaxTalkerRows = 32;
	constexpr float ToneAmplitude = 0.3f;
	/** Seconds the device stays open after the voice client has nowhere to send (the visit travels, the test ends). */
	constexpr float CloseAfterSeconds = 2.0f;

	bool IsVoiceKey(FName Key)
	{
		static const FName Keys[] = { EnabledKey, PushToTalkKey, MicMutedKey, ThresholdKey, HoldKey, InputGainKey, AutoGainKey,
			NoiseKey, NoiseLevelKey, EchoKey, JitterKey, OutputVolumeKey };
		for (const FName& Known : Keys)
		{
			if (Known == Key)
			{
				return true;
			}
		}
		return false;
	}

	void AtomicAdd(std::atomic<float>& Value, float Add)
	{
		float Known = Value.load(std::memory_order_relaxed);
		while (!Value.compare_exchange_weak(Known, Known + Add, std::memory_order_relaxed))
		{
		}
	}

	FString TalkerName(const FScctVoiceTalker& Row)
	{
		TCHAR Name[FScctVoiceTalker::NameCapacity];
		FMemory::Memcpy(Name, Row.Name, sizeof(Name));
		Name[FScctVoiceTalker::NameCapacity - 1] = TEXT('\0');
		return FString(Name);
	}

	const TCHAR* EchoPhaseName(int32 Phase)
	{
		static const TCHAR* const Names[] = { TEXT("off"), TEXT("recording"), TEXT("playing"), TEXT("done"), TEXT("nothing captured"), TEXT("live") };
		return Phase >= 0 && Phase < UE_ARRAY_COUNT(Names) ? Names[Phase] : TEXT("?");
	}
}

/**
 * A tap on a submix, on the audio render thread. As the echo reference it mixes the main submix to mono, takes it to
 * 48 kHz (linear interpolation, enough for a canceller's reference) and pushes it into a ring the game thread drains;
 * as the output meter it adds up the voice submix's squared samples for the once-a-second log.
 */
class FScctVoiceTap : public ISubmixBufferListener
{
public:
	FScctVoiceTap(FString InName, bool bInReference) : Name(MoveTemp(InName)), bReference(bInReference)
	{
		if (bReference)
		{
			Ring.SetCapacity(ScctVoiceSampleRate / 2);
		}
	}

	/** Echo reference only: whether to collect at all (echo cancellation on). */
	std::atomic<bool> bActive{ false };

	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override
	{
		if (NumChannels <= 0 || NumSamples <= 0)
		{
			return;
		}
		const int32 NumFrames = NumSamples / NumChannels;
		if (!bReference)
		{
			float Sum = 0.0f;
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				Sum += AudioData[Index] * AudioData[Index];
			}
			AtomicAdd(Squares, Sum);
			Samples.fetch_add(NumSamples, std::memory_order_relaxed);
			return;
		}
		if (!bActive.load(std::memory_order_relaxed) || SampleRate <= 0)
		{
			Position = -1.0;
			return;
		}

		Mono.SetNumUninitialized(NumFrames, EAllowShrinking::No);
		const float Scale = 1.0f / NumChannels;
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			float Sum = 0.0f;
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				Sum += AudioData[Frame * NumChannels + Channel];
			}
			Mono[Frame] = Sum * Scale;
		}
		if (SampleRate == ScctVoiceSampleRate)
		{
			Ring.Push(Mono.GetData(), NumFrames);
			return;
		}

		// Position runs in input samples, -1 being the last sample of the previous block.
		const double Step = static_cast<double>(SampleRate) / ScctVoiceSampleRate;
		Resampled.Reset();
		while (Position < NumFrames - 1)
		{
			const int32 Index = FMath::FloorToInt32(Position);
			const float Fraction = static_cast<float>(Position - Index);
			const float A = Index < 0 ? Previous : Mono[Index];
			const float B = Mono[Index + 1];
			Resampled.Add(A + (B - A) * Fraction);
			Position += Step;
		}
		Position -= NumFrames;
		Previous = Mono[NumFrames - 1];
		Ring.Push(Resampled.GetData(), Resampled.Num());
	}

	virtual const FString& GetListenerName() const override { return Name; }

	/** Echo reference, game thread: the 48 kHz mono samples collected since the last call. */
	void Drain(TArray<float>& Out)
	{
		const int32 Available = static_cast<int32>(Ring.Num());
		Out.SetNumUninitialized(Available, EAllowShrinking::No);
		Out.SetNum(Available > 0 ? Ring.Pop(Out.GetData(), Available) : 0, EAllowShrinking::No);
	}

	/** Output meter, game thread: the RMS since the last call, and how many samples it covers. */
	float TakeRms(int32& OutSamples)
	{
		OutSamples = Samples.exchange(0, std::memory_order_relaxed);
		const float Sum = Squares.exchange(0.0f, std::memory_order_relaxed);
		return OutSamples > 0 ? FMath::Sqrt(Sum / OutSamples) : 0.0f;
	}

private:
	FString Name;
	bool bReference;
	Audio::TCircularAudioBuffer<float> Ring;
	TArray<float> Mono;
	TArray<float> Resampled;
	double Position = -1.0;
	float Previous = 0.0f;
	std::atomic<float> Squares{ 0.0f };
	std::atomic<int32> Samples{ 0 };
};

UScctVoiceSubsystem::UScctVoiceSubsystem() = default;
UScctVoiceSubsystem::~UScctVoiceSubsystem() = default;

void UScctVoiceSubsystem::RegisterSettings()
{
	// Godot's GameSettings voice section (game/Scripts/Options/GameSettings.cs) and its Voice page, with its defaults.
	int32 Order = 0;
	const auto Add = [&Order](FName Key, FText Group, FText Label, FText Tooltip, EScctSettingType Type, const TCHAR* Default, float Min = 0.0f, float Max = 1.0f, float Step = 0.0f)
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = TEXT("Voice");
		Desc.Group = MoveTemp(Group);
		Desc.Label = MoveTemp(Label);
		Desc.Tooltip = MoveTemp(Tooltip);
		Desc.Type = Type;
		Desc.Default = Default;
		Desc.Min = Min;
		Desc.Max = Max;
		Desc.Step = Step;
		Desc.Order = Order++;
		return Desc;
	};
	const FText Chat = LOCTEXT("GroupChat", "Voice chat");
	const FText Microphone = LOCTEXT("GroupMicrophone", "Microphone");
	const FText Activation = LOCTEXT("GroupActivation", "Voice activation");
	const FText Network = LOCTEXT("GroupNetwork", "Network");

	FScctSettings::Register(Add(EnabledKey, Chat, LOCTEXT("Enabled", "Enable voice chat"),
		LOCTEXT("EnabledTip", "Off: you hear nobody and nobody hears you."), EScctSettingType::Bool, TEXT("true")));
	FScctSettings::Register(Add(PushToTalkKey, Chat, LOCTEXT("PushToTalk", "Push to talk"),
		LOCTEXT("PushToTalkTip", "Transmit only while Push To Talk (or Team Push To Talk) is held. Off: your voice opens the microphone when it is loud enough."), EScctSettingType::Bool, TEXT("false")));
	FScctSettings::Register(Add(MicMutedKey, Chat, LOCTEXT("MicMuted", "Mute microphone"),
		LOCTEXT("MicMutedTip", "Nothing is sent; everyone else is still heard. The Toggle microphone mute key flips it."), EScctSettingType::Bool, TEXT("false")));
	FScctSettings::Register(Add(InputDeviceKey, Microphone, LOCTEXT("InputDevice", "Input device"),
		LOCTEXT("InputDeviceTip", "The microphone's name as the system lists it (the log lists them under 'ScctVoice: input devices'). Empty: the system's input device."), EScctSettingType::Text, TEXT("")));
	FScctSettings::Register(Add(InputGainKey, Microphone, LOCTEXT("InputGain", "Microphone volume"),
		LOCTEXT("InputGainTip", "Gain before sending (1 = unchanged). Not used while the automatic adjustment is on."), EScctSettingType::Float, TEXT("1.0"), 0.0f, 8.0f, 0.1f));
	FScctSettings::Register(Add(AutoGainKey, Microphone, LOCTEXT("AutoGain", "Auto-adjust microphone volume"),
		LOCTEXT("AutoGainTip", "Keeps your voice at a steady level."), EScctSettingType::Bool, TEXT("true")));
	FScctSettings::Register(Add(NoiseKey, Microphone, LOCTEXT("Noise", "Noise suppression"),
		LOCTEXT("NoiseTip", "Steady background noise is taken out before the voice activation decides."), EScctSettingType::Bool, TEXT("true")));
	{
		FScctSettingDesc Desc = Add(NoiseLevelKey, Microphone, LOCTEXT("NoiseLevel", "Noise suppression level"),
			LOCTEXT("NoiseLevelTip", "How hard noise is suppressed: 15, 30, 45 or 60 dB."), EScctSettingType::Choice, TEXT("1"), 0.0f, 3.0f, 1.0f);
		Desc.Choices = { LOCTEXT("Low", "Low"), LOCTEXT("Standard", "Standard"), LOCTEXT("High", "High"), LOCTEXT("Extreme", "Extreme") };
		FScctSettings::Register(Desc);
	}
	FScctSettings::Register(Add(EchoKey, Microphone, LOCTEXT("Echo", "Echo cancellation (experimental)"),
		LOCTEXT("EchoTip", "Takes what your speakers play back out of the microphone."), EScctSettingType::Bool, TEXT("false")));
	FScctSettings::Register(Add(ThresholdKey, Activation, LOCTEXT("Threshold", "Activation threshold"),
		LOCTEXT("ThresholdTip", "How loud the microphone must be to transmit when push to talk is off: 0 always, 1 never."), EScctSettingType::Float, TEXT("0.02"), 0.0f, 1.0f, 0.01f));
	FScctSettings::Register(Add(HoldKey, Activation, LOCTEXT("Hold", "Release time (ms)"),
		LOCTEXT("HoldTip", "How long transmission goes on after you stop speaking."), EScctSettingType::Int, TEXT("300"), 0.0f, 5000.0f, 50.0f));
	FScctSettings::Register(Add(JitterKey, Network, LOCTEXT("Jitter", "Buffering (20 ms frames)"),
		LOCTEXT("JitterTip", "More smooths a bumpy connection, at the cost of delay."), EScctSettingType::Int, TEXT("3"), 1.0f, 12.0f, 1.0f));
}

bool UScctVoiceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && !IsRunningCommandlet();
}

void UScctVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UScctAudioMix>();
	Super::Initialize(Collection);
	RegisterSettings();

	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("ScctVoiceTone="), ToneHz);
	ToneHz = FMath::Clamp(ToneHz, 0.0f, 20000.0f);
	bForceTalk = FParse::Param(CommandLine, TEXT("ScctVoiceTalk"));
	if (FParse::Value(CommandLine, TEXT("ScctVoiceEcho="), EchoSeconds))
	{
		EchoSeconds = FMath::Clamp(EchoSeconds, 0, 10);
	}
	else if (FParse::Param(CommandLine, TEXT("ScctVoiceEcho")))
	{
		EchoSeconds = 3;
	}
	if (FParse::Value(CommandLine, TEXT("ScctVoicePushToTalk="), ForcedPushToTalk))
	{
		ForcedPushToTalk = ForcedPushToTalk != 0 ? 1 : 0;
	}

	Capture = MakeUnique<FScctVoiceCapture>();
	if (ToneHz <= 0.0f)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: input devices: %s"), *FString::Join(FScctVoiceCapture::ListDevices(), TEXT(", ")));
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone %s%s%s%s"),
		ToneHz > 0.0f ? *FString::Printf(TEXT("is a %.0f Hz tone (-ScctVoiceTone)"), ToneHz) : TEXT("is the input device"),
		bForceTalk ? TEXT(", talk key held (-ScctVoiceTalk)") : TEXT(""),
		EchoSeconds >= 0 ? *FString::Printf(TEXT(", microphone test of %d s (-ScctVoiceEcho)"), EchoSeconds) : TEXT(""),
		ForcedPushToTalk >= 0 ? (ForcedPushToTalk != 0 ? TEXT(", push to talk forced on") : TEXT(", push to talk forced off")) : TEXT(""));

	SettingsHandle = FScctSettings::OnChanged().AddUObject(this, &UScctVoiceSubsystem::HandleSettingChanged);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctVoiceSubsystem::Tick));
}

void UScctVoiceSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	FScctSettings::OnChanged().Remove(SettingsHandle);
	if (CommandSource.IsValid())
	{
		CommandSource->OnLocalCommand.Remove(CommandHandle);
	}
	CommandSource.Reset();
	ClearOutputs();
	RemoveTaps();
	if (Capture.IsValid())
	{
		Capture->Close();
	}
	if (Api != nullptr)
	{
		Api->SetKeys(0, 0);
		if (bEchoStarted)
		{
			Api->StopEchoTest();
		}
	}
	Super::Deinitialize();
}

bool UScctVoiceSubsystem::Tick(float DeltaTime)
{
	if (Api == nullptr)
	{
		// The bridge comes up once the content folder is known; ask again once a second until then.
		ApiRetry -= DeltaTime;
		if (ApiRetry > 0.0f)
		{
			return true;
		}
		ApiRetry = 1.0f;
		Api = ScctFeature<FScctVoiceApi>();
		if (Api == nullptr)
		{
			return true;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: the bridge's voice table is ready"));
		ApplySettings();
	}

	BindCommands();
	SendKeys();
	if (EchoSeconds >= 0 && !bEchoStarted)
	{
		bEchoStarted = true;
		const bool bStarted = Api->StartEchoTest(EchoSeconds) != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone test %s"), bStarted
			? *FString::Printf(TEXT("started (%s)"), EchoSeconds > 0 ? *FString::Printf(TEXT("%d s recorded, then played back"), EchoSeconds) : TEXT("live loopback"))
			: TEXT("refused: an online visit owns the microphone"));
	}

	Api->Update();
	Api->GetState(&Status);
	if (Status.EchoPhase != LastEchoPhase)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone test %s (sent %lld frames)"), EchoPhaseName(Status.EchoPhase), Status.FramesSent);
		LastEchoPhase = Status.EchoPhase;
	}
	if (Status.Connected != LastConnected)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: voice client %s (local talker %d)"), Status.Connected != 0 ? TEXT("connected") : TEXT("disconnected"), Status.LocalTalker);
		LastConnected = Status.Connected;
	}

	EnsureTaps();
	UpdateCapture(DeltaTime);
	PumpPlayback(DeltaTime);
	PumpEchoReference();
	ReportError();

	LogTimer += DeltaTime;
	if (LogTimer >= 1.0f)
	{
		LogTimer = FMath::Fmod(LogTimer, 1.0f);
		LogSecond();
	}
	return true;
}

// ---- keys and options -----------------------------------------------------------------------------------------

void UScctVoiceSubsystem::BindCommands()
{
	const UGameInstance* GameInstance = GetGameInstance();
	AScctPlayerController* Player = GameInstance != nullptr ? Cast<AScctPlayerController>(GameInstance->GetFirstLocalPlayerController()) : nullptr;
	if (Player == CommandSource.Get())
	{
		return;
	}
	if (CommandSource.IsValid())
	{
		CommandSource->OnLocalCommand.Remove(CommandHandle);
	}
	CommandHandle.Reset();
	CommandSource = Player;
	// A key held on the old controller (a travel) is no longer held.
	bTalkHeld = false;
	bTeamHeld = false;
	if (Player != nullptr)
	{
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctVoiceSubsystem::HandleLocalCommand);
	}
}

void UScctVoiceSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	// The bridge sends the release of a held command when a menu takes the keys, so a talk key never sticks.
	if (Name == TEXT("PushToTalk"))
	{
		bTalkHeld = bPressed;
	}
	else if (Name == TEXT("TeamPushToTalk"))
	{
		bTeamHeld = bPressed;
	}
	else if (bPressed && Name == TEXT("ToggleVoiceMute"))
	{
		const bool bMuted = !FScctSettings::GetBool(MicMutedKey);
		FScctSettings::SetBool(MicMutedKey, bMuted);
		FScctSettings::Save();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone %s (Toggle microphone mute)"), bMuted ? TEXT("muted") : TEXT("unmuted"));
	}
}

void UScctVoiceSubsystem::SendKeys()
{
	const bool bTalk = bTalkHeld || bForceTalk;
	const bool bTeam = bTeamHeld;
	if (bKeysSent && bTalk == bSentTalk && bTeam == bSentTeam)
	{
		return;
	}
	Api->SetKeys(bTalk ? 1 : 0, bTeam ? 1 : 0);
	bKeysSent = true;
	bSentTalk = bTalk;
	bSentTeam = bTeam;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: keys talk=%d team=%d"), bTalk ? 1 : 0, bTeam ? 1 : 0);
}

void UScctVoiceSubsystem::HandleSettingChanged(FName Key)
{
	if (Key == InputDeviceKey)
	{
		// Reopened on the new device by the next frame's capture update.
		if (Capture.IsValid() && Capture->IsOpen())
		{
			Capture->Close();
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: input device changed to '%s'"), *FScctSettings::GetString(InputDeviceKey));
		}
		CaptureRetry = 0.0f;
		return;
	}
	if (IsVoiceKey(Key))
	{
		ApplySettings();
	}
}

void UScctVoiceSubsystem::ApplySettings()
{
	if (Api == nullptr)
	{
		return;
	}
	FScctVoiceSettings Settings = {};
	Settings.Enabled = FScctSettings::GetBool(EnabledKey) ? 1 : 0;
	Settings.PushToTalk = ForcedPushToTalk >= 0 ? ForcedPushToTalk : (FScctSettings::GetBool(PushToTalkKey) ? 1 : 0);
	Settings.MicMuted = FScctSettings::GetBool(MicMutedKey) ? 1 : 0;
	Settings.ActivationThreshold = FMath::Clamp(FScctSettings::GetFloat(ThresholdKey), 0.0f, 1.0f);
	Settings.ActivationHoldMs = FMath::Clamp(FScctSettings::GetInt(HoldKey), 0, 5000);
	Settings.InputGain = FMath::Clamp(FScctSettings::GetFloat(InputGainKey), 0.0f, 8.0f);
	Settings.OutputVolume = FMath::Clamp(FScctSettings::GetFloat(OutputVolumeKey), 0.0f, 1.0f);
	Settings.JitterFrames = FMath::Clamp(FScctSettings::GetInt(JitterKey), 1, 12);
	Settings.NoiseSuppression = FScctSettings::GetBool(NoiseKey) ? 1 : 0;
	Settings.NoiseSuppressionDecibels = NoiseDecibels[FMath::Clamp(FScctSettings::GetInt(NoiseLevelKey), 0, 3)];
	Settings.AutoGain = FScctSettings::GetBool(AutoGainKey) ? 1 : 0;
	Settings.EchoCancellation = FScctSettings::GetBool(EchoKey) ? 1 : 0;
	if (ToneHz > 0.0f)
	{
		// As the Godot client's synthetic microphone: a steady tone is exactly what the clean-up takes away.
		Settings.AutoGain = 0;
		Settings.NoiseSuppression = 0;
		Settings.EchoCancellation = 0;
	}
	Api->ApplySettings(&Settings);
	if (ReferenceTap.IsValid())
	{
		ReferenceTap->bActive.store(Settings.EchoCancellation != 0, std::memory_order_relaxed);
	}
	bEchoReference = Settings.EchoCancellation != 0;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: options enabled=%d pushtotalk=%d muted=%d threshold=%.2f hold=%dms gain=%.2f autogain=%d noise=%d/%ddB echo=%d jitter=%d output=%.2f"),
		Settings.Enabled, Settings.PushToTalk, Settings.MicMuted, Settings.ActivationThreshold, Settings.ActivationHoldMs, Settings.InputGain,
		Settings.AutoGain, Settings.NoiseSuppression, Settings.NoiseSuppressionDecibels, Settings.EchoCancellation, Settings.JitterFrames, Settings.OutputVolume);
}

// ---- the microphone -------------------------------------------------------------------------------------------

void UScctVoiceSubsystem::UpdateCapture(float DeltaTime)
{
	// A finished microphone test still reads as connected until it is stopped; it needs no microphone any more.
	const bool bWanted = Status.Enabled != 0 && ((Status.Connected != 0 && Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Off))
		|| Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Recording) || Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Live));

	if (ToneHz > 0.0f)
	{
		if (SentCaptureRate != ScctVoiceSampleRate)
		{
			SentCaptureRate = ScctVoiceSampleRate;
			Api->SetCaptureRate(SentCaptureRate);
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: capture rate %d Hz (tone)"), SentCaptureRate);
		}
		if (bWanted)
		{
			FeedTone(DeltaTime);
		}
		else
		{
			ToneOwed = 0.0;
		}
		return;
	}

	if (bWanted)
	{
		IdleSeconds = 0.0f;
		CaptureRetry -= DeltaTime;
		if (!Capture->IsOpen() && CaptureRetry <= 0.0f)
		{
			FString Problem;
			const FString Device = FScctSettings::GetString(InputDeviceKey);
			if (Capture->Open(Device, Problem))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone open on '%s'%s%s"), *Capture->GetDeviceName(), Problem.IsEmpty() ? TEXT("") : TEXT(": "), *Problem);
			}
			else
			{
				// Asked again in a while: a headset may be plugged in.
				CaptureRetry = 5.0f;
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctVoice: no microphone: %s"), *Problem);
			}
		}
	}
	else if (Capture->IsOpen())
	{
		IdleSeconds += DeltaTime;
		if (IdleSeconds >= CloseAfterSeconds)
		{
			Capture->Close();
			SentCaptureRate = 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone closed (voice chat has nowhere to send)"));
		}
	}

	if (!Capture->IsOpen())
	{
		return;
	}
	const int32 Rate = Capture->GetSampleRate();
	if (Rate <= 0)
	{
		return;
	}
	if (Rate != SentCaptureRate)
	{
		SentCaptureRate = Rate;
		Api->SetCaptureRate(Rate);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: capture rate %d Hz (device '%s')"), Rate, *Capture->GetDeviceName());
	}
	if (Capture->Drain(CaptureBlock) > 0)
	{
		Api->SubmitCapture(CaptureBlock.GetData(), CaptureBlock.Num());
	}
}

void UScctVoiceSubsystem::FeedTone(float DeltaTime)
{
	// What a microphone would have delivered since the last frame; a long hitch is not made up all at once.
	ToneOwed += FMath::Min(DeltaTime, 0.25f) * ScctVoiceSampleRate;
	const int32 Due = FMath::FloorToInt32(ToneOwed);
	if (Due <= 0)
	{
		return;
	}
	ToneOwed -= Due;
	CaptureBlock.SetNumUninitialized(Due, EAllowShrinking::No);
	const double Step = 2.0 * PI * ToneHz / ScctVoiceSampleRate;
	for (int32 Index = 0; Index < Due; ++Index)
	{
		CaptureBlock[Index] = ToneAmplitude * static_cast<float>(FMath::Sin(TonePhase));
		TonePhase += Step;
	}
	TonePhase = FMath::Fmod(TonePhase, 2.0 * PI);
	Api->SubmitCapture(CaptureBlock.GetData(), Due);
}

// ---- playback -------------------------------------------------------------------------------------------------

void UScctVoiceSubsystem::PumpPlayback(float DeltaTime)
{
	FScctVoiceTalker Rows[MaxTalkerRows];
	const int32 Count = FMath::Clamp(Api->GetTalkers(Rows, MaxTalkerRows), 0, MaxTalkerRows);
	Talkers.Reset();
	Talkers.Append(Rows, Count);

	// Talkers that left, or a visit that moved on: their playback goes.
	for (auto It = Outputs.CreateIterator(); It; ++It)
	{
		const int32 Id = It.Key();
		if (!Talkers.ContainsByPredicate([Id](const FScctVoiceTalker& Row) { return Row.Talker == Id; }))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: talker %d (%s) gone after %lld frames"), Id, *It.Value().Name, It.Value().Frames);
			DestroyOutput(It.Value());
			It.RemoveCurrent();
		}
	}

	FAudioDevice* Device = GEngine != nullptr ? GEngine->GetMainAudioDeviceRaw() : nullptr;
	int32 ClockDue = 0;
	if (Device == nullptr)
	{
		ClockFrames += DeltaTime * (ScctVoiceSampleRate / ScctVoiceFrameSamples);
		ClockDue = FMath::Min(FMath::FloorToInt32(ClockFrames), 12);
		ClockFrames -= ClockDue;
		ClockFrames = FMath::Min(ClockFrames, 1.0);
	}

	float Frame[ScctVoiceFrameSamples];
	int16 Pcm[ScctVoiceFrameSamples];
	for (const FScctVoiceTalker& Row : Talkers)
	{
		FScctVoiceTalkerOutput* Output = Outputs.Find(Row.Talker);
		if (Output == nullptr)
		{
			Output = CreateOutput(Row);
		}
		const FString Name = TalkerName(Row);
		if (Output->Name != Name)
		{
			// Talker ids belong to a map: on the next one the same id may be someone else.
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: talker %d is now %s (was %s)"), Row.Talker, *Name, *Output->Name);
			Output->Name = Name;
		}
		if (IsValid(Output->Component) && !Output->Component->IsPlaying())
		{
			// Loading a map flushes the audio device, which stops every sound, these included: start again empty.
			Output->Wave->ResetAudio();
			Output->Component->Play();
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: talker %d (%s): playback had been stopped (a map load); playing again"), Row.Talker, *Output->Name);
		}

		for (int32 Read = 0; ; ++Read)
		{
			if (Output->Wave != nullptr)
			{
				if (Output->Wave->GetAvailableAudioByteCount() >= QueuedFrames * ScctVoiceFrameSamples * static_cast<int32>(sizeof(int16)))
				{
					break;
				}
			}
			else if (Read >= ClockDue)
			{
				break;
			}
			if (Api->ReadFrame(Row.Talker, Frame) == 0)
			{
				break;
			}
			double Squares = 0.0;
			for (int32 Index = 0; Index < ScctVoiceFrameSamples; ++Index)
			{
				const float Sample = FMath::Clamp(Frame[Index], -1.0f, 1.0f);
				Squares += Sample * Sample;
				Pcm[Index] = static_cast<int16>(FMath::RoundToInt32(Sample * 32767.0f));
			}
			Output->SecondSquares += Squares;
			++Output->SecondFrames;
			++Output->Frames;
			if (Output->Wave != nullptr)
			{
				Output->Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm), sizeof(Pcm));
			}
		}
	}
}

FScctVoiceTalkerOutput* UScctVoiceSubsystem::CreateOutput(const FScctVoiceTalker& Row)
{
	FScctVoiceTalkerOutput& Output = Outputs.Add(Row.Talker);
	Output.Name = TalkerName(Row);
	Output.Underruns = MakeShared<std::atomic<int32>>(0);

	FAudioDevice* Device = GEngine != nullptr ? GEngine->GetMainAudioDeviceRaw() : nullptr;
	const UScctAudioMix* Mix = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UScctAudioMix>() : nullptr;
	USoundClass* VoiceClass = Mix != nullptr ? Mix->GetClass(EScctAudioCategory::Voice) : nullptr;
	if (Device == nullptr)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: talker %d (%s) heard; no audio device, so read by the clock"), Row.Talker, *Output.Name);
		return &Output;
	}

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(ScctVoiceSampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->bLooping = false;
	Wave->SoundGroup = SOUNDGROUP_Voice;
	Wave->SoundClassObject = VoiceClass;
	TSharedPtr<std::atomic<int32>> Underruns = Output.Underruns;
	Wave->OnSoundWaveProceduralUnderflow.BindLambda([Underruns](USoundWaveProcedural*, int32)
	{
		Underruns->fetch_add(1, std::memory_order_relaxed);
	});

	// Made on the device rather than in a world, so the voice carries on through a travel from map to map.
	UAudioComponent* Component = FAudioDevice::CreateComponent(Wave, FAudioDevice::FCreateComponentParams(Device));
	if (Component == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctVoice: talker %d (%s): no audio component could be made, read by the clock"), Row.Talker, *Output.Name);
		return &Output;
	}
	Component->SoundClassOverride = VoiceClass;
	Component->bAllowSpatialization = false;
	Component->bIsUISound = true;
	Component->bAutoDestroy = false;
	Component->Play();
	Output.Wave = Wave;
	Output.Component = Component;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: talker %d (%s) heard; playing 2D at 48 kHz on sound class %s"), Row.Talker, *Output.Name,
		VoiceClass != nullptr ? *VoiceClass->GetName() : TEXT("(none)"));
	return &Output;
}

void UScctVoiceSubsystem::DestroyOutput(FScctVoiceTalkerOutput& Output)
{
	if (IsValid(Output.Component))
	{
		Output.Component->Stop();
		Output.Component->DestroyComponent();
	}
	Output.Component = nullptr;
	Output.Wave = nullptr;
}

void UScctVoiceSubsystem::ClearOutputs()
{
	for (TPair<int32, FScctVoiceTalkerOutput>& Pair : Outputs)
	{
		DestroyOutput(Pair.Value);
	}
	Outputs.Reset();
}

// ---- submix taps ----------------------------------------------------------------------------------------------

void UScctVoiceSubsystem::EnsureTaps()
{
	FAudioDevice* Device = GEngine != nullptr ? GEngine->GetMainAudioDeviceRaw() : nullptr;
	if (Device == nullptr || (ReferenceTap.IsValid() && TapDevice == Device->DeviceID))
	{
		return;
	}
	RemoveTaps();
	TapDevice = Device->DeviceID;
	ReferenceTap = MakeShared<FScctVoiceTap, ESPMode::ThreadSafe>(TEXT("ScctVoiceEchoReference"), true);
	ReferenceTap->bActive.store(bEchoReference, std::memory_order_relaxed);
	Device->RegisterSubmixBufferListener(ReferenceTap.ToSharedRef(), Device->GetMainSubmixObject());
	const UScctAudioMix* Mix = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UScctAudioMix>() : nullptr;
	if (USoundSubmix* VoiceSubmix = Mix != nullptr ? Mix->GetSubmix(EScctAudioCategory::Voice) : nullptr)
	{
		OutputTap = MakeShared<FScctVoiceTap, ESPMode::ThreadSafe>(TEXT("ScctVoiceOutput"), false);
		Device->RegisterSubmixBufferListener(OutputTap.ToSharedRef(), *VoiceSubmix);
	}
}

void UScctVoiceSubsystem::RemoveTaps()
{
	FAudioDeviceManager* Manager = GEngine != nullptr ? GEngine->GetAudioDeviceManager() : nullptr;
	FAudioDevice* Device = Manager != nullptr && ReferenceTap.IsValid() ? Manager->GetAudioDeviceRaw(TapDevice) : nullptr;
	if (Device != nullptr)
	{
		Device->UnregisterSubmixBufferListener(ReferenceTap.ToSharedRef(), Device->GetMainSubmixObject());
		const UScctAudioMix* Mix = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UScctAudioMix>() : nullptr;
		USoundSubmix* VoiceSubmix = Mix != nullptr ? Mix->GetSubmix(EScctAudioCategory::Voice) : nullptr;
		if (OutputTap.IsValid() && VoiceSubmix != nullptr)
		{
			Device->UnregisterSubmixBufferListener(OutputTap.ToSharedRef(), *VoiceSubmix);
		}
	}
	ReferenceTap.Reset();
	OutputTap.Reset();
	TapDevice = 0;
}

void UScctVoiceSubsystem::PumpEchoReference()
{
	if (!ReferenceTap.IsValid())
	{
		return;
	}
	ReferenceTap->Drain(ReferenceBlock);
	if (bEchoReference && ReferenceBlock.Num() > 0)
	{
		Api->SubmitPlayback(ReferenceBlock.GetData(), ReferenceBlock.Num());
	}
}

// ---- the log --------------------------------------------------------------------------------------------------

void UScctVoiceSubsystem::LogSecond()
{
	int32 OutputSamples = 0;
	const float OutputRms = OutputTap.IsValid() ? OutputTap->TakeRms(OutputSamples) : 0.0f;
	if (Status.Connected == 0 && Status.EchoPhase == 0 && Outputs.Num() == 0)
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: capture=%s rate=%d frames captured=%lld (+%lld) sent=%lld (+%lld) transmitting=%d channel=%s muted=%d input=%.3f connected=%d talker=%d talkers=%d received=%lld echo=%s voice out rms=%.4f/%d"),
		ToneHz > 0.0f ? TEXT("tone") : (Capture->IsOpen() ? *Capture->GetDeviceName() : TEXT("closed")), SentCaptureRate,
		Status.FramesCaptured, Status.FramesCaptured - LastFramesCaptured, Status.FramesSent, Status.FramesSent - LastFramesSent,
		Status.Transmitting, Status.Channel != 0 ? TEXT("team") : TEXT("all"), Status.MicMuted, Status.InputLevel, Status.Connected, Status.LocalTalker,
		Status.TalkerCount, Status.PacketsReceived, EchoPhaseName(Status.EchoPhase), OutputRms, OutputSamples);
	LastFramesCaptured = Status.FramesCaptured;
	LastFramesSent = Status.FramesSent;

	for (TPair<int32, FScctVoiceTalkerOutput>& Pair : Outputs)
	{
		FScctVoiceTalkerOutput& Output = Pair.Value;
		const int32 Underruns = Output.Underruns.IsValid() ? Output.Underruns->exchange(0, std::memory_order_relaxed) : 0;
		if (Output.SecondFrames > 0)
		{
			const FScctVoiceTalker* Row = Talkers.FindByPredicate([&Pair](const FScctVoiceTalker& R) { return R.Talker == Pair.Key; });
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: heard talker=%d name=%s rms=%.3f frames=%d total=%lld queued=%d team=%d channel=%s volume=%.2f muted=%d decode errors=%lld empty callbacks=%d%s"),
				Pair.Key, *Output.Name, FMath::Sqrt(Output.SecondSquares / (Output.SecondFrames * static_cast<double>(ScctVoiceFrameSamples))),
				Output.SecondFrames, Output.Frames, Row != nullptr ? Row->QueuedFrames : 0, Row != nullptr ? Row->Team : -1,
				Row != nullptr && Row->Channel != 0 ? TEXT("team") : TEXT("all"), Row != nullptr ? Row->Volume : 0.0f, Row != nullptr ? Row->Muted : 0,
				Row != nullptr ? Row->DecodeErrors : 0, Underruns, Output.Wave == nullptr ? TEXT(" (by the clock)") : TEXT(""));
		}
		Output.SecondFrames = 0;
		Output.SecondSquares = 0.0;
	}
}

void UScctVoiceSubsystem::ReportError()
{
	TCHAR Buffer[512];
	const int32 Length = Api->GetError(Buffer, UE_ARRAY_COUNT(Buffer));
	if (Length <= 0)
	{
		return;
	}
	Buffer[UE_ARRAY_COUNT(Buffer) - 1] = TEXT('\0');
	const FString Error(Buffer);
	if (Error != LastError)
	{
		LastError = Error;
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctVoice: bridge error: %s"), *Error);
	}
}

#undef LOCTEXT_NAMESPACE
