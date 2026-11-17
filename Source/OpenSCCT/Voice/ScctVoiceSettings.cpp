#include "Voice/ScctVoiceSettings.h"

#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"

#define LOCTEXT_NAMESPACE "ScctVoice"

namespace ScctVoiceSettings
{
	const FName EnabledKey(TEXT("Voice.Enabled"));
	const FName PushToTalkKey(TEXT("Voice.PushToTalk"));
	const FName MicMutedKey(TEXT("Voice.MicMuted"));
	const FName InputDeviceKey(TEXT("Voice.InputDevice"));
	const FName InputDeviceNameKey(TEXT("Voice.InputDeviceName"));
	const FName ThresholdKey(TEXT("Voice.Threshold"));
	const FName HoldKey(TEXT("Voice.HoldMs"));
	const FName InputGainKey(TEXT("Voice.InputGain"));
	const FName AutoGainKey(TEXT("Voice.AutoGain"));
	const FName NoiseKey(TEXT("Voice.NoiseSuppression"));
	const FName NoiseDbKey(TEXT("Voice.NoiseSuppressionDb"));
	const FName EchoKey(TEXT("Voice.EchoCancellation"));
	const FName JitterKey(TEXT("Voice.JitterFrames"));
	const FName TeamOnlyKey(TEXT("Voice.TeamOnly"));
	const FName TestKey(TEXT("Voice.Test"));
}

namespace
{
	using namespace ScctVoiceSettings;

	/** Godot's GameSettings.NoiseLevels and NoiseLevelDecibels: Low, Standard, High, Extreme. */
	constexpr int32 NoiseLevels[] = { 15, 30, 45, 60 };

	// The page's groups in Godot's order (the voice chat volume itself is on the Audio page, UScctUserSettings).
	FText PlaybackGroup() { return LOCTEXT("GroupPlayback", "Playback & devices"); }
	FText MicrophoneGroup() { return LOCTEXT("GroupMicrophone", "Microphone"); }
	FText TransmissionGroup() { return LOCTEXT("GroupTransmission", "Transmission"); }
	FText ShortcutsGroup() { return LOCTEXT("GroupShortcuts", "Voice shortcuts"); }
	FText LobbyGroup() { return LOCTEXT("GroupLobby", "Lobby"); }
	FText TestGroup() { return LOCTEXT("GroupTest", "Microphone test"); }

	FScctSettingDesc Make(FName Key, FText Group, int32 Order, FText Label, FText Tooltip, EScctSettingType Type, const TCHAR* Default,
		float Min = 0.0f, float Max = 1.0f, float Step = 0.0f)
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
		Desc.Order = Order;
		return Desc;
	}

	const TCHAR* TypeName(EScctSettingType Type)
	{
		switch (Type)
		{
		case EScctSettingType::Bool: return TEXT("bool");
		case EScctSettingType::Float: return TEXT("float");
		case EScctSettingType::Int: return TEXT("int");
		case EScctSettingType::Choice: return TEXT("choice");
		default: return TEXT("text");
		}
	}

	/** The devices of the last RegisterDevices, so a picked index can be turned back into a name. */
	TArray<FString>& RegisteredDevices()
	{
		static TArray<FString> Devices;
		return Devices;
	}
}

void ScctVoiceSettings::Register()
{
	// Orders leave room between the groups; the device choice is 0.
	FScctSettings::Register(Make(EnabledKey, PlaybackGroup(), 1, LOCTEXT("Enabled", "Enable voice chat"),
		LOCTEXT("EnabledTip", "Off: you hear nobody and nobody hears you."), EScctSettingType::Bool, TEXT("true")));
	FScctSettings::Register(Make(JitterKey, PlaybackGroup(), 3, LOCTEXT("Jitter", "Playback buffer (20 ms per step)"),
		LOCTEXT("JitterTip", "More smooths a bumpy connection, at the cost of delay."), EScctSettingType::Int, TEXT("3"), 1.0f, 12.0f, 1.0f));

	FScctSettings::Register(Make(MicMutedKey, MicrophoneGroup(), 10, LOCTEXT("MicMuted", "Mute microphone"),
		LOCTEXT("MicMutedTip", "Nothing is sent; everyone else is still heard. The Mute Microphone key flips it."), EScctSettingType::Bool, TEXT("false")));
	FScctSettings::Register(Make(InputGainKey, MicrophoneGroup(), 11, LOCTEXT("InputGain", "Microphone gain multiplier"),
		LOCTEXT("InputGainTip", "Gain before sending (1 = unchanged). Automatic microphone volume overrides it."), EScctSettingType::Float, TEXT("1.0"), 0.0f, 4.0f, 0.05f));
	FScctSettings::Register(Make(AutoGainKey, MicrophoneGroup(), 12, LOCTEXT("AutoGain", "Automatic microphone volume"),
		LOCTEXT("AutoGainTip", "Keeps your voice at a steady level; the gain multiplier is not used while it is on."), EScctSettingType::Bool, TEXT("true")));
	FScctSettings::Register(Make(NoiseKey, MicrophoneGroup(), 13, LOCTEXT("Noise", "Noise suppression"),
		LOCTEXT("NoiseTip", "Steady background noise is taken out before the voice activation decides."), EScctSettingType::Bool, TEXT("true")));
	{
		FScctSettingDesc Desc = Make(NoiseDbKey, MicrophoneGroup(), 14, LOCTEXT("NoiseLevel", "Noise suppression level"),
			LOCTEXT("NoiseLevelTip", "How hard steady noise is suppressed."), EScctSettingType::Choice, TEXT("1"), 0.0f, 3.0f, 1.0f);
		Desc.Choices = { LOCTEXT("Low", "Low (15 dB)"), LOCTEXT("Standard", "Standard (30 dB)"), LOCTEXT("High", "High (45 dB)"), LOCTEXT("Extreme", "Extreme (60 dB)") };
		FScctSettings::Register(Desc);
	}
	FScctSettings::Register(Make(EchoKey, MicrophoneGroup(), 15, LOCTEXT("Echo", "Echo cancellation (experimental: for speakers)"),
		LOCTEXT("EchoTip", "Takes what your speakers play back out of the microphone."), EScctSettingType::Bool, TEXT("false")));

	FScctSettings::Register(Make(ThresholdKey, TransmissionGroup(), 20, LOCTEXT("Threshold", "Voice activation threshold"),
		LOCTEXT("ThresholdTip", "How loud your microphone must be to transmit when push to talk is off."), EScctSettingType::Float, TEXT("0.02"), 0.0f, 0.25f, 0.001f));
	FScctSettings::Register(Make(HoldKey, TransmissionGroup(), 21, LOCTEXT("Hold", "Transmission release delay (ms)"),
		LOCTEXT("HoldTip", "Keeps transmission open briefly after your voice drops below the threshold."), EScctSettingType::Int, TEXT("300"), 0.0f, 2000.0f, 10.0f));

	FScctSettings::Register(Make(PushToTalkKey, ShortcutsGroup(), 30, LOCTEXT("PushToTalk", "Require push to talk"),
		LOCTEXT("PushToTalkTip", "Transmit only while Push To Talk (or Team Push To Talk) is held; change the keys in the role bindings."), EScctSettingType::Bool, TEXT("false")));

	FScctSettings::Register(Make(TeamOnlyKey, LobbyGroup(), 40, LOCTEXT("TeamOnly", "Restrict Mic to Team"),
		LOCTEXT("TeamOnlyTip", "In the lobby your voice reaches only your own team. Sent to the server you are on, and again whenever you join one."), EScctSettingType::Bool, TEXT("false")));

	FScctSettings::Register(Make(TestKey, TestGroup(), 50, LOCTEXT("Test", "Test my microphone"),
		LOCTEXT("TestTip", "Records three seconds and plays back what other players hear from you after microphone processing, transmission and playback. Not while you are in an online session."),
		EScctSettingType::Bool, TEXT("false")));

	FScctSettingDesc Name = Make(InputDeviceNameKey, PlaybackGroup(), 0, LOCTEXT("InputDeviceName", "Microphone device name"),
		LOCTEXT("InputDeviceNameTip", "The chosen microphone by name; empty for the system's."), EScctSettingType::Text, TEXT(""));
	Name.bVisible = false;
	FScctSettings::Register(Name);
}

void ScctVoiceSettings::RegisterDevices(const TArray<FString>& Devices)
{
	RegisteredDevices() = Devices;
	const FString Saved = FScctSettings::GetString(InputDeviceNameKey);

	FScctSettingDesc Desc = Make(InputDeviceKey, PlaybackGroup(), 0, LOCTEXT("InputDevice", "Microphone device"),
		LOCTEXT("InputDeviceTip", "Default follows the system's choice. A chosen microphone that is not connected is replaced by the system's until it is back."),
		EScctSettingType::Choice, TEXT("0"));
	Desc.Choices.Add(LOCTEXT("DefaultDevice", "Default"));
	for (const FString& Device : Devices)
	{
		Desc.Choices.Add(FText::FromString(Device));
	}
	int32 Index = 0;
	if (!Saved.IsEmpty())
	{
		const int32 Found = Devices.IndexOfByKey(Saved);
		if (Found != INDEX_NONE)
		{
			Index = Found + 1;
		}
		else
		{
			Desc.Choices.Add(FText::Format(LOCTEXT("MissingDevice", "{0} (not connected)"), FText::FromString(Saved)));
			Index = Desc.Choices.Num() - 1;
		}
	}
	Desc.Min = 0.0f;
	Desc.Max = static_cast<float>(Desc.Choices.Num() - 1);
	Desc.Step = 1.0f;
	FScctSettings::Register(Desc);
	FScctSettings::SetInt(InputDeviceKey, Index);
}

FString ScctVoiceSettings::ChosenDevice()
{
	return FScctSettings::GetString(InputDeviceNameKey);
}

void ScctVoiceSettings::RememberChosenDevice()
{
	const TArray<FString>& Devices = RegisteredDevices();
	const int32 Index = FScctSettings::GetInt(InputDeviceKey);
	if (Index == 0)
	{
		FScctSettings::SetString(InputDeviceNameKey, FString());
	}
	else if (Devices.IsValidIndex(Index - 1))
	{
		FScctSettings::SetString(InputDeviceNameKey, Devices[Index - 1]);
	}
	// Past the devices: the saved device that is not connected, which stays chosen.
}

int32 ScctVoiceSettings::NoiseDecibels()
{
	return NoiseLevels[FMath::Clamp(FScctSettings::GetInt(NoiseDbKey), 0, UE_ARRAY_COUNT(NoiseLevels) - 1)];
}

void ScctVoiceSettings::LogPage(FName Page)
{
	TArray<const FScctSettingDesc*> Options = FScctSettings::OptionsOf(Page);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctSettings: page %s, %d options"), *Page.ToString(), Options.Num());
	if (Page == TEXT("Voice"))
	{
		// The page's hidden option, which OptionsOf leaves out.
		if (const FScctSettingDesc* Hidden = FScctSettings::Find(InputDeviceNameKey))
		{
			Options.Add(Hidden);
		}
	}
	for (const FScctSettingDesc* Desc : Options)
	{
		FString Extra;
		if (Desc->Type == EScctSettingType::Float || Desc->Type == EScctSettingType::Int)
		{
			Extra = FString::Printf(TEXT(" range %g..%g step %g"), Desc->Min, Desc->Max, Desc->Step);
		}
		else if (Desc->Type == EScctSettingType::Choice)
		{
			TArray<FString> Names;
			for (const FText& Choice : Desc->Choices)
			{
				Names.Add(Choice.ToString());
			}
			Extra = FString::Printf(TEXT(" choices [%s]"), *FString::Join(Names, TEXT(" | ")));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctSettings:   %d %s [%s] '%s' %s default=%s value=%s%s%s"), Desc->Order, *Desc->Key.ToString(),
			*Desc->Group.ToString(), *Desc->Label.ToString(), TypeName(Desc->Type), *Desc->Default, *FScctSettings::GetString(Desc->Key), *Extra,
			Desc->bVisible ? TEXT("") : TEXT(" (hidden)"));
	}
}

bool ScctVoiceSettings::SetFromText(const FString& Assignment)
{
	FString Key;
	FString Value;
	if (!Assignment.Split(TEXT("="), &Key, &Value))
	{
		return false;
	}
	Key.TrimStartAndEndInline();
	Value.TrimStartAndEndInline();
	const FName Name(*Key);
	if (FScctSettings::Find(Name) == nullptr)
	{
		return false;
	}
	if (Value.Equals(TEXT("default"), ESearchCase::IgnoreCase))
	{
		FScctSettings::Reset(Name);
	}
	else
	{
		FScctSettings::SetString(Name, Value);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctSettings: %s = %s"), *Key, *FScctSettings::GetString(Name));
	return true;
}

#undef LOCTEXT_NAMESPACE
