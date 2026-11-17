#include "Voice/ScctVoiceSubsystem.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
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
#include "Voice/ScctVoiceSettings.h"

#define LOCTEXT_NAMESPACE "ScctVoice"

namespace
{
	using namespace ScctVoiceSettings;

	/** Seconds the microphone test records before it plays back, as Godot's MicTestSeconds. */
	constexpr int32 TestSeconds = 3;
	/** Seconds between two looks at the system's input devices. */
	constexpr float DevicePollSeconds = 5.0f;

	/** Sets an option from the console as the Options screen would: "scct.Voice.Set Voice.InputGain 2" or "... default". */
	FAutoConsoleCommand SetCommand(TEXT("scct.Voice.Set"),
		TEXT("Sets a registered option (any page, e.g. Voice.InputGain 2, Voice.Enabled false, Voice.Test true) or back to its default with 'default'. Applies at once."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() != 2 || !ScctVoiceSettings::SetFromText(Args[0] + TEXT("=") + Args[1]))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctVoice: usage: scct.Voice.Set <registered key> <value|default>"));
			}
		}));

	/** Lists a page's options with their defaults and values: "scct.Voice.Options" (the Voice page) or "... Audio". */
	FAutoConsoleCommand OptionsCommand(TEXT("scct.Voice.Options"),
		TEXT("Logs every option of a settings page (default Voice) with its type, range, default and value."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			ScctVoiceSettings::LogPage(Args.Num() > 0 ? FName(*Args[0]) : FName(TEXT("Voice")));
		}));
	/** Decoded frames kept queued in a talker's wave (60 ms): the jitter buffer banks, the wave bridges game frames. */
	constexpr int32 QueuedFrames = 3;
	constexpr int32 MaxTalkerRows = 32;
	constexpr float ToneAmplitude = 0.3f;
	/** Seconds the device stays open after the voice client has nowhere to send (the visit travels, the test ends). */
	constexpr float CloseAfterSeconds = 2.0f;

	bool IsVoiceKey(FName Key)
	{
		static const FName Keys[] = { EnabledKey, PushToTalkKey, MicMutedKey, ThresholdKey, HoldKey, InputGainKey, AutoGainKey,
			NoiseKey, NoiseDbKey, EchoKey, JitterKey, OutputVolumeKey };
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
	ScctVoiceSettings::Register();
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

	bForceMeter = FParse::Param(CommandLine, TEXT("ScctVoiceMeter"));
	FString DumpPage;
	if (FParse::Value(CommandLine, TEXT("ScctVoiceOptions="), DumpPage))
	{
		OptionsToLog = DumpPage;
	}
	else if (FParse::Param(CommandLine, TEXT("ScctVoiceOptions")))
	{
		OptionsToLog = TEXT("Voice");
	}
	FString Assignments;
	if (FParse::Value(CommandLine, TEXT("ScctVoiceSet="), Assignments, false))
	{
		ParseScheduledSettings(Assignments);
	}

	// The test switch is a button: never on when the game starts, whatever a saved file says.
	FScctSettings::Reset(TestKey);
	TestState = LOCTEXT("TestIdle", "Hear exactly what other players hear from you.");

	InitPlayers();
	Capture = MakeUnique<FScctVoiceCapture>();
	RefreshDevices();
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
	ShowOverlay(false);
	ClearOutputs();
	RemoveTaps();
	if (Capture.IsValid())
	{
		Capture->Close();
	}
	if (Api != nullptr)
	{
		Api->SetKeys(0, 0);
		Api->SetMeter(0);
		if (bEchoStarted || bTestRunning)
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
		StartPlayers();
		if (!OptionsToLog.IsEmpty())
		{
			// Every subsystem has registered its options by now.
			ScctVoiceSettings::LogPage(FName(*OptionsToLog));
		}
	}

	ReadyTime += DeltaTime;
	RunScheduledSettings();
	DevicePoll += DeltaTime;
	if (DevicePoll >= DevicePollSeconds)
	{
		DevicePoll = 0.0f;
		RefreshDevices();
	}

	BindCommands();
	SendKeys();
	SendMeter();
	SyncTeamOnly();
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
	FollowTest();
	if (Status.Connected != LastConnected)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: voice client %s (local talker %d)"), Status.Connected != 0 ? TEXT("connected") : TEXT("disconnected"), Status.LocalTalker);
		LastConnected = Status.Connected;
	}

	EnsureTaps();
	UpdateCapture(DeltaTime);
	PumpPlayback(DeltaTime);
	Api->GetContext(&Context);
	UpdateOverlay();
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
		// Only the player's pick names a new device; a new device list just moves the index to the same device.
		if (!bSyncingDevices)
		{
			ScctVoiceSettings::RememberChosenDevice();
			FScctSettings::Save();
		}
		return;
	}
	if (Key == InputDeviceNameKey)
	{
		// Reopened on the new device by the next frame's capture update.
		const FString Device = ScctVoiceSettings::ChosenDevice();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: input device changed to %s"), Device.IsEmpty() ? TEXT("the system's") : *FString::Printf(TEXT("'%s'"), *Device));
		if (bDevicesKnown && !bSyncingDevices)
		{
			// The name set on its own (the console, a saved file): the choice points at it, listed as not connected if so.
			bSyncingDevices = true;
			ScctVoiceSettings::RegisterDevices(KnownDevices);
			bSyncingDevices = false;
		}
		if (Capture.IsValid() && Capture->IsOpen())
		{
			Capture->Close();
			SentCaptureRate = 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone closed to reopen it on the new device"));
		}
		CaptureRetry = 0.0f;
		return;
	}
	if (Key == TeamOnlyKey)
	{
		if (!bSyncingTeamOnly)
		{
			const bool bOnly = FScctSettings::GetBool(TeamOnlyKey);
			const bool bSent = Api != nullptr && Api->SetTeamVoiceOnly(bOnly ? 1 : 0) != 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: restrict mic to team %s %s"), bOnly ? TEXT("on") : TEXT("off"),
				bSent ? TEXT("sent to the server") : TEXT("kept for the next server joined"));
		}
		return;
	}
	if (Key == TestKey)
	{
		HandleTestSwitch();
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
	Settings.NoiseSuppressionDecibels = ScctVoiceSettings::NoiseDecibels();
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

void UScctVoiceSubsystem::RefreshDevices()
{
	TArray<FString> Devices;
	for (const FString& Device : FScctVoiceCapture::ListDevices())
	{
		// "Default" is the choice's own first entry, as Godot leaves the system's "Default" out of its list.
		if (!Device.Equals(TEXT("Default"), ESearchCase::IgnoreCase) && !Devices.Contains(Device))
		{
			Devices.Add(Device);
		}
	}
	if (bDevicesKnown && Devices == KnownDevices)
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: input devices%s: %s"), bDevicesKnown ? TEXT(" changed") : TEXT(""),
		Devices.Num() > 0 ? *FString::Join(Devices, TEXT(", ")) : TEXT("(none)"));
	bDevicesKnown = true;
	KnownDevices = Devices;
	bSyncingDevices = true;
	ScctVoiceSettings::RegisterDevices(Devices);
	bSyncingDevices = false;
}

void UScctVoiceSubsystem::ParseScheduledSettings(const FString& Text)
{
	// "Key=Value;4:Key=Value": an optional "Seconds:" counts from the voice table being ready.
	TArray<FString> Entries;
	Text.ParseIntoArray(Entries, TEXT(";"));
	for (FString& Entry : Entries)
	{
		Entry.TrimStartAndEndInline();
		FScheduledSetting Scheduled;
		FString Seconds;
		FString Rest;
		if (Entry.Split(TEXT(":"), &Seconds, &Rest) && Seconds.IsNumeric())
		{
			Scheduled.Seconds = FCString::Atof(*Seconds);
			Scheduled.Assignment = Rest;
		}
		else
		{
			Scheduled.Assignment = Entry;
		}
		if (!Scheduled.Assignment.IsEmpty())
		{
			ScheduledSettings.Add(MoveTemp(Scheduled));
		}
	}
	ScheduledSettings.StableSort([](const FScheduledSetting& A, const FScheduledSetting& B) { return A.Seconds < B.Seconds; });
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: %d option changes scheduled (-ScctVoiceSet)"), ScheduledSettings.Num());
}

void UScctVoiceSubsystem::RunScheduledSettings()
{
	while (ScheduledSettings.Num() > 0 && ScheduledSettings[0].Seconds <= ReadyTime)
	{
		const FScheduledSetting Next = ScheduledSettings[0];
		ScheduledSettings.RemoveAt(0);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: t=%.1f s -ScctVoiceSet %s"), ReadyTime, *Next.Assignment);
		const bool bOptions = Next.Assignment.Equals(TEXT("options"), ESearchCase::IgnoreCase) || Next.Assignment.StartsWith(TEXT("options="), ESearchCase::IgnoreCase);
		const FString Page = bOptions ? Next.Assignment.Mid(8) : FString();
		if (bOptions)
		{
			// "options" or "options=Page": the page as it stands now, into the log.
			ScctVoiceSettings::LogPage(FName(Page.IsEmpty() ? TEXT("Voice") : *Page));
		}
		else if (!ScctVoiceSettings::SetFromText(Next.Assignment))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctVoice: -ScctVoiceSet: '%s' is not Key=Value of a registered option"), *Next.Assignment);
		}
	}
}

void UScctVoiceSubsystem::SetMeterWanted(bool bWanted)
{
	bMeterWanted = bWanted;
}

void UScctVoiceSubsystem::SendMeter()
{
	const bool bMeter = bMeterWanted || bForceMeter;
	if (bMeter == bMeterSent)
	{
		return;
	}
	bMeterSent = bMeter;
	Api->SetMeter(bMeter ? 1 : 0);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: level meter %s"), bMeter ? TEXT("on") : TEXT("off"));
}

void UScctVoiceSubsystem::SyncTeamOnly()
{
	const int32 Server = Api->GetTeamVoiceOnly();
	if (Server == LastServerTeamOnly)
	{
		return;
	}
	const bool bWanted = FScctSettings::GetBool(TeamOnlyKey);
	if (Server >= 0 && LastServerTeamOnly < 0)
	{
		// A server joined: it hears the player's choice (it keeps it across its maps from then on).
		if (bWanted != (Server != 0))
		{
			Api->SetTeamVoiceOnly(bWanted ? 1 : 0);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: restrict mic to team: the server has it %s%s"), Server != 0 ? TEXT("on") : TEXT("off"),
			bWanted != (Server != 0) ? (bWanted ? TEXT(", turned on") : TEXT(", turned off")) : TEXT(""));
	}
	else if (Server >= 0)
	{
		// The server's answer, or a change made elsewhere (a lobby toggle): the option follows it.
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: restrict mic to team is %s on the server"), Server != 0 ? TEXT("on") : TEXT("off"));
		if (bWanted != (Server != 0))
		{
			bSyncingTeamOnly = true;
			FScctSettings::SetBool(TeamOnlyKey, Server != 0);
			bSyncingTeamOnly = false;
		}
	}
	LastServerTeamOnly = Server;
}

void UScctVoiceSubsystem::HandleTestSwitch()
{
	const bool bOn = FScctSettings::GetBool(TestKey);
	if (bOn == bTestRunning)
	{
		return;
	}
	if (!bOn)
	{
		// Turned off while it ran: stopped where it is.
		bTestRunning = false;
		if (Api != nullptr)
		{
			Api->StopEchoTest();
		}
		TestState = LOCTEXT("TestIdle", "Hear exactly what other players hear from you.");
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone test stopped (Voice.Test)"));
		return;
	}

	if (Api == nullptr || Api->StartEchoTest(TestSeconds) == 0)
	{
		// The bridge refuses while an online visit has the microphone, as Godot's page does.
		TestState = Api == nullptr ? LOCTEXT("TestNoVoice", "Voice chat is not running.") : LOCTEXT("TestOnline", "Leave the session to test the microphone.");
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone test refused (%s)"), *TestState.ToString());
		FScctSettings::SetBool(TestKey, false);
		return;
	}
	bTestRunning = true;
	bEchoStarted = false;
	EchoSeconds = -1;
	TestState = LOCTEXT("TestRecording", "Recording - speak now");
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone test started (Voice.Test): %d s recorded, then played back"), TestSeconds);
}

void UScctVoiceSubsystem::FollowTest()
{
	if (!bTestRunning)
	{
		return;
	}
	switch (static_cast<EScctVoiceEchoPhase>(Status.EchoPhase))
	{
	case EScctVoiceEchoPhase::Playing:
		TestState = LOCTEXT("TestPlaying", "Playing it back");
		break;
	case EScctVoiceEchoPhase::Done:
	case EScctVoiceEchoPhase::NothingCaptured:
	case EScctVoiceEchoPhase::Off:
		TestState = Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::NothingCaptured)
			? LOCTEXT("TestNothing", "Nothing was captured. Check the microphone device.")
			: LOCTEXT("TestIdle", "Hear exactly what other players hear from you.");
		// The switch goes back off by itself, as Godot's button comes back when the test is over.
		bTestRunning = false;
		Api->StopEchoTest();
		FScctSettings::SetBool(TestKey, false);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone test over: %s"), *TestState.ToString());
		break;
	default:
		break;
	}
}

FText UScctVoiceSubsystem::GetMicState() const
{
	// Godot's Voice page label beside the level bar.
	if (FScctSettings::GetBool(MicMutedKey))
	{
		return LOCTEXT("MicMutedState", "muted");
	}
	if (FScctSettings::GetBool(PushToTalkKey))
	{
		return LOCTEXT("MicPushState", "push to talk");
	}
	return Status.InputLevel >= FScctSettings::GetFloat(ThresholdKey) ? LOCTEXT("MicOpenState", "transmitting") : LOCTEXT("MicSilentState", "silent");
}

// ---- the microphone -------------------------------------------------------------------------------------------

void UScctVoiceSubsystem::UpdateCapture(float DeltaTime)
{
	// A finished microphone test still reads as connected until it is stopped; it needs no microphone any more.
	const bool bTestCapturing = Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Recording) || Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Live);
	const bool bOnline = Status.Connected != 0 && Status.EchoPhase == static_cast<int32>(EScctVoiceEchoPhase::Off);
	// The level meter is the page's, like Godot's: it reads the microphone whether voice chat is on or not.
	const bool bMetering = bMeterSent && !bOnline && !bTestCapturing;
	const bool bWanted = (Status.Enabled != 0 && (bOnline || bTestCapturing)) || bMetering;
	const TCHAR* Reason = bOnline ? TEXT("online visit") : bTestCapturing ? TEXT("microphone test") : bMetering ? TEXT("level meter") : nullptr;
	if (bWanted != bCaptureWanted || (bWanted && CaptureReason != Reason))
	{
		if (bWanted)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: capture wanted for the %s"), Reason);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: capture not wanted (%s)"), Status.Enabled == 0 ? TEXT("voice chat is off") : TEXT("voice chat has nowhere to send"));
		}
		bCaptureWanted = bWanted;
		CaptureReason = bWanted ? Reason : nullptr;
	}

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
			const FString Device = ScctVoiceSettings::ChosenDevice();
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
		// Turned off: closed at once. Nowhere to send: a moment later, since a travel or a test's end is brief.
		IdleSeconds += DeltaTime;
		if (Status.Enabled == 0 || IdleSeconds >= CloseAfterSeconds)
		{
			Capture->Close();
			SentCaptureRate = 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: microphone closed (%s)"), Status.Enabled == 0 ? TEXT("voice chat is off") : TEXT("voice chat has nowhere to send"));
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
	if (Status.Connected == 0 && Status.EchoPhase == 0 && Outputs.Num() == 0 && !bMeterSent)
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
