#include "Play/ScctAudioMix.h"

#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/Engine.h"
#include "ISubmixBufferListener.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"

#include <atomic>

#define LOCTEXT_NAMESPACE "ScctAudioMix"

namespace
{
	constexpr int32 NumCategories = static_cast<int32>(EScctAudioCategory::Num);

	/** Godot's SettingsApplier.NonVoiceHeadroom: 3 dB of summing headroom for every category but Master and Voice. */
	constexpr float NonVoiceHeadroom = 0.70794576f;

	const FName MasterKey(TEXT("Audio.Master"));

	/** The option of each category, in EScctAudioCategory order. */
	const FName CategoryKeys[NumCategories] = {
		TEXT("Audio.Effects"), TEXT("Audio.RoundMusic"), TEXT("Audio.MenuMusic"), TEXT("Audio.Objective"),
		TEXT("Audio.MenuSounds"), TEXT("Audio.Voice") };

	bool ParseCategory(const FString& Name, int32& OutIndex);

	/** Sets a volume option from the console, as the Options screen would: "scct.Audio.Volume Music 0.25" or "... default". */
	FAutoConsoleCommand VolumeCommand(TEXT("scct.Audio.Volume"),
		TEXT("Sets an Audio volume option (Master, Effects, Music, MenuMusic, Objective, MenuSounds, Voice) to 0..1, or back to its default with 'default'. Applies at once and is kept in the user settings."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			int32 Index = INDEX_NONE;
			if (Args.Num() != 2 || !ParseCategory(Args[0], Index))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctAudio: usage: scct.Audio.Volume <Master|Effects|Music|MenuMusic|Objective|MenuSounds|Voice> <0..1|default>"));
				return;
			}
			const FName Key = Index == INDEX_NONE ? MasterKey : CategoryKeys[Index];
			if (Args[1].Equals(TEXT("default"), ESearchCase::IgnoreCase))
			{
				FScctSettings::Reset(Key);
			}
			else
			{
				FScctSettings::SetFloat(Key, FCString::Atof(*Args[1]));
			}
		}));

	/** Index of the main submix's peak listener in FDeviceState::Peaks; the categories follow from 1. */
	constexpr int32 MainPeak = 0;

	/** -ScctAudioVolume names: the category's own name and the option's. Returns INDEX_NONE for Master. */
	bool ParseCategory(const FString& Name, int32& OutIndex)
	{
		static const TCHAR* const Aliases[NumCategories][3] = {
			{ TEXT("World"), TEXT("Effects"), TEXT("Sound") },
			{ TEXT("Music"), TEXT("RoundMusic"), nullptr },
			{ TEXT("MenuMusic"), nullptr, nullptr },
			{ TEXT("Objective"), TEXT("Objectives"), nullptr },
			{ TEXT("MenuSound"), TEXT("MenuSounds"), nullptr },
			{ TEXT("Voice"), nullptr, nullptr } };
		if (Name.Equals(TEXT("Master"), ESearchCase::IgnoreCase))
		{
			OutIndex = INDEX_NONE;
			return true;
		}
		for (int32 Index = 0; Index < NumCategories; ++Index)
		{
			for (const TCHAR* Alias : Aliases[Index])
			{
				if (Alias != nullptr && Name.Equals(Alias, ESearchCase::IgnoreCase))
				{
					OutIndex = Index;
					return true;
				}
			}
		}
		return false;
	}
}

/**
 * Keeps the largest sample a submix rendered (after its own output volume) until the game thread reads it. The
 * audio render thread writes, the game thread reads and clears.
 */
class FScctSubmixPeak : public ISubmixBufferListener
{
public:
	explicit FScctSubmixPeak(FString InName) : Name(MoveTemp(InName)) {}

	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override
	{
		float Largest = 0.0f;
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			Largest = FMath::Max(Largest, FMath::Abs(AudioData[Index]));
		}
		float Known = Peak.load(std::memory_order_relaxed);
		while (Largest > Known && !Peak.compare_exchange_weak(Known, Largest, std::memory_order_relaxed))
		{
		}
		Buffers.fetch_add(1, std::memory_order_relaxed);
	}

	virtual const FString& GetListenerName() const override { return Name; }

	/** The peak since the last call, and how many buffers it covers. */
	float Take(int32& OutBuffers)
	{
		OutBuffers = Buffers.exchange(0, std::memory_order_relaxed);
		return Peak.exchange(0.0f, std::memory_order_relaxed);
	}

private:
	FString Name;
	std::atomic<float> Peak{ 0.0f };
	std::atomic<int32> Buffers{ 0 };
};

const TCHAR* UScctAudioMix::CategoryName(EScctAudioCategory Category)
{
	static const TCHAR* const Names[NumCategories] = {
		TEXT("World"), TEXT("Music"), TEXT("MenuMusic"), TEXT("Objective"), TEXT("MenuSound"), TEXT("Voice") };
	const int32 Index = static_cast<int32>(Category);
	return Index >= 0 && Index < NumCategories ? Names[Index] : TEXT("?");
}

void UScctAudioMix::RegisterSettings()
{
	// Godot's GameSettings (game/Scripts/Options/GameSettings.cs, the audio section): master 1, the categories 0.5
	// (z_cache_sound_volume on a first run), other players' voices 1.
	struct FOption
	{
		FName Key;
		FText Label;
		FText Tooltip;
		const TCHAR* Default;
	};
	const FOption Options[] = {
		{ MasterKey, LOCTEXT("Master", "Master volume"), LOCTEXT("MasterTip", "Everything the game plays."), TEXT("1.0") },
		{ CategoryKeys[0], LOCTEXT("Effects", "Sound effects"), LOCTEXT("EffectsTip", "The match's sounds: steps, shots, voices of the characters, the map's ambience and its rooms' reverb."), TEXT("0.5") },
		{ CategoryKeys[1], LOCTEXT("RoundMusic", "In-game music volume"), LOCTEXT("RoundMusicTip", "The music of a round and of its end."), TEXT("0.5") },
		{ CategoryKeys[2], LOCTEXT("MenuMusic", "Menu music volume"), LOCTEXT("MenuMusicTip", "The music of the main menu and the lobby."), TEXT("0.5") },
		{ CategoryKeys[3], LOCTEXT("Objective", "Objective sounds"), LOCTEXT("ObjectiveTip", "The jingles that mark an objective. At 0 they are not played at all."), TEXT("0.5") },
		{ CategoryKeys[4], LOCTEXT("MenuSounds", "Menu sounds"), LOCTEXT("MenuSoundsTip", "The menus' own sounds: button feedback, the join sound."), TEXT("0.5") },
		{ CategoryKeys[5], LOCTEXT("Voice", "Voice chat volume"), LOCTEXT("VoiceTip", "Other players' voices."), TEXT("1.0") },
	};
	int32 Order = 0;
	for (const FOption& Option : Options)
	{
		FScctSettingDesc Desc;
		Desc.Key = Option.Key;
		Desc.Page = TEXT("Audio");
		Desc.Group = LOCTEXT("Volume", "Volume");
		Desc.Label = Option.Label;
		Desc.Tooltip = Option.Tooltip;
		Desc.Type = EScctSettingType::Float;
		Desc.Default = Option.Default;
		Desc.Min = 0.0f;
		Desc.Max = 1.0f;
		Desc.Step = 0.05f;
		Desc.Order = Order++;
		FScctSettings::Register(Desc);
	}
}

void UScctAudioMix::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSettings();

	for (float& Value : Forced)
	{
		Value = -1.0f;
	}
	FString ForcedText;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctAudioVolume="), ForcedText, false))
	{
		TArray<FString> Pairs;
		ForcedText.ParseIntoArray(Pairs, TEXT(","));
		for (const FString& Pair : Pairs)
		{
			FString Name, Value;
			int32 Index = INDEX_NONE;
			if (!Pair.Split(TEXT(":"), &Name, &Value) || !ParseCategory(Name.TrimStartAndEnd(), Index))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctAudio: -ScctAudioVolume: '%s' is not Category:Value (Master, Effects, Music, MenuMusic, Objective, MenuSounds, Voice)"), *Pair);
				continue;
			}
			const float Gain = FMath::Clamp(FCString::Atof(*Value), 0.0f, 1.0f);
			(Index == INDEX_NONE ? ForcedMaster : Forced[Index]) = Gain;
		}
	}
	bLogPeaks = FParse::Param(FCommandLine::Get(), TEXT("ScctAudioPeaks"));

	CreateObjects();
	SettingsHandle = FScctSettings::OnChanged().AddUObject(this, &UScctAudioMix::HandleSettingChanged);
	EnsureApplied();

	// What the Options screen will list on the Audio page, once, so a run's log shows the options exist.
	for (const FScctSettingDesc* Desc : FScctSettings::OptionsOf(TEXT("Audio")))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: option %s '%s' (group %s, %.2f..%.2f step %.2f, default %s, now %s)"), *Desc->Key.ToString(),
			*Desc->Label.ToString(), *Desc->Group.ToString(), Desc->Min, Desc->Max, Desc->Step, *Desc->Default, *FScctSettings::GetString(Desc->Key));
	}

	if (bLogPeaks)
	{
		PeakTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctAudioMix::LogPeaks), 1.0f);
	}
}

void UScctAudioMix::Deinitialize()
{
	FScctSettings::OnChanged().Remove(SettingsHandle);
	FTSTicker::RemoveTicker(PeakTicker);

	if (FAudioDeviceManager* Manager = GEngine != nullptr ? GEngine->GetAudioDeviceManager() : nullptr)
	{
		Manager->IterateOverAllDevices([this](Audio::FDeviceId Id, FAudioDevice* Device)
		{
			FDeviceState* State = Devices.Find(Id);
			if (State == nullptr)
			{
				return;
			}
			if (State->Peaks.Num() > 0)
			{
				Device->UnregisterSubmixBufferListener(State->Peaks[MainPeak], Device->GetMainSubmixObject());
				for (int32 Index = 0; Index < NumCategories && Index + 1 < State->Peaks.Num(); ++Index)
				{
					Device->UnregisterSubmixBufferListener(State->Peaks[Index + 1], *Submixes[Index]);
				}
			}
			for (USoundSubmix* Submix : Submixes)
			{
				Device->UnregisterSoundSubmix(Submix, false);
			}
			for (USoundClass* Class : Classes)
			{
				Device->UnregisterSoundClass(Class);
			}
			Device->UnregisterSoundClass(MasterClass);
		});
	}
	Devices.Reset();
	Super::Deinitialize();
}

USoundClass* UScctAudioMix::GetClass(EScctAudioCategory Category) const
{
	const int32 Index = static_cast<int32>(Category);
	return Classes.IsValidIndex(Index) ? Classes[Index].Get() : nullptr;
}

USoundSubmix* UScctAudioMix::GetSubmix(EScctAudioCategory Category) const
{
	const int32 Index = static_cast<int32>(Category);
	return Submixes.IsValidIndex(Index) ? Submixes[Index].Get() : nullptr;
}

void UScctAudioMix::CreateObjects()
{
	MasterClass = NewObject<USoundClass>(this, TEXT("ScctMaster"));
	for (int32 Index = 0; Index < NumCategories; ++Index)
	{
		const EScctAudioCategory Category = static_cast<EScctAudioCategory>(Index);
		const FString Name = CategoryName(Category);

		// No parent: the submix sums into the engine's main submix, which is Master.
		USoundSubmix* Submix = NewObject<USoundSubmix>(this, *(TEXT("ScctSubmix") + Name));
		Submixes.Add(Submix);

		USoundClass* Class = NewObject<USoundClass>(this, *(TEXT("Scct") + Name));
		Class->ParentClass = MasterClass;
		MasterClass->ChildClasses.Add(Class);
		Class->Properties.DefaultSubmix = Submix;
		Class->Properties.bIsMusic = Category == EScctAudioCategory::Music || Category == EScctAudioCategory::MenuMusic;
		Class->Properties.bIsUISound = Category == EScctAudioCategory::MenuSound || Category == EScctAudioCategory::MenuMusic;
		if (Category != EScctAudioCategory::World)
		{
			// Only the match's own sounds are heard in the room (Godot's environment bus takes the effects volume):
			// music, jingles, menus and voices go to their submix dry.
			Class->Properties.bReverb = false;
			Class->Properties.Default2DReverbSendAmount = 0.0f;
		}
		Classes.Add(Class);
	}
	ReverbSubmix = Cast<USoundSubmix>(GetDefault<UAudioSettings>()->ReverbSubmix.TryLoad());
}

void UScctAudioMix::RegisterWithDevices()
{
	FAudioDeviceManager* Manager = GEngine != nullptr ? GEngine->GetAudioDeviceManager() : nullptr;
	if (Manager == nullptr)
	{
		return;
	}
	Manager->IterateOverAllDevices([this](Audio::FDeviceId Id, FAudioDevice* Device)
	{
		if (Device == nullptr || Devices.Contains(Id))
		{
			return;
		}
		FDeviceState& State = Devices.Add(Id);
		Device->RegisterSoundClass(MasterClass);
		for (USoundClass* Class : Classes)
		{
			Device->RegisterSoundClass(Class);
		}
		for (USoundSubmix* Submix : Submixes)
		{
			Device->RegisterSoundSubmix(Submix, true);
		}
		if (bLogPeaks)
		{
			State.Peaks.Add(MakeShared<FScctSubmixPeak, ESPMode::ThreadSafe>(TEXT("ScctPeakMaster")));
			Device->RegisterSubmixBufferListener(State.Peaks[MainPeak], Device->GetMainSubmixObject());
			for (int32 Index = 0; Index < NumCategories; ++Index)
			{
				State.Peaks.Add(MakeShared<FScctSubmixPeak, ESPMode::ThreadSafe>(FString(TEXT("ScctPeak")) + CategoryName(static_cast<EScctAudioCategory>(Index))));
				Device->RegisterSubmixBufferListener(State.Peaks.Last(), *Submixes[Index]);
			}
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: %d sound classes and %d submixes registered with audio device %u"), Classes.Num() + 1, Submixes.Num(), Id);
	});
}

void UScctAudioMix::EnsureApplied()
{
	RegisterWithDevices();
	ReadGains();
	Apply();
}

void UScctAudioMix::HandleSettingChanged(FName Key)
{
	if (Key != MasterKey && Algo::Find(CategoryKeys, Key) == nullptr)
	{
		return;
	}
	ReadGains();
	Apply();
}

void UScctAudioMix::ReadGains()
{
	MasterGain = ForcedMaster >= 0.0f ? ForcedMaster : FMath::Clamp(FScctSettings::GetFloat(MasterKey), 0.0f, 1.0f);
	for (int32 Index = 0; Index < NumCategories; ++Index)
	{
		Gains[Index] = Forced[Index] >= 0.0f ? Forced[Index] : FMath::Clamp(FScctSettings::GetFloat(CategoryKeys[Index]), 0.0f, 1.0f);
	}
}

void UScctAudioMix::Apply()
{
	float Applied[NumCategories];
	for (int32 Index = 0; Index < NumCategories; ++Index)
	{
		Applied[Index] = Gains[Index] * (static_cast<EScctAudioCategory>(Index) == EScctAudioCategory::Voice ? 1.0f : NonVoiceHeadroom);
	}

	int32 NumDevices = 0;
	if (FAudioDeviceManager* Manager = GEngine != nullptr ? GEngine->GetAudioDeviceManager() : nullptr)
	{
		Manager->IterateOverAllDevices([this, &Applied, &NumDevices](Audio::FDeviceId Id, FAudioDevice* Device)
		{
			if (Device == nullptr)
			{
				return;
			}
			++NumDevices;
			Device->SetSubmixOutputVolume(&Device->GetMainSubmixObject(), MasterGain);
			for (int32 Index = 0; Index < NumCategories; ++Index)
			{
				Device->SetSubmixOutputVolume(Submixes[Index], Applied[Index]);
			}
			if (ReverbSubmix != nullptr)
			{
				Device->SetSubmixOutputVolume(ReverbSubmix, Applied[static_cast<int32>(EScctAudioCategory::World)]);
			}
		});
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: mix master=%.2f effects=%.2f music=%.2f menumusic=%.2f objective=%.2f menusounds=%.2f voice=%.2f (submix volumes with 3 dB headroom on all but master and voice; %d audio devices%s%s)"),
		MasterGain, Gains[0], Gains[1], Gains[2], Gains[3], Gains[4], Gains[5], NumDevices,
		ForcedMaster >= 0.0f || Algo::AnyOf(Forced, [](float Value) { return Value >= 0.0f; }) ? TEXT(", some forced by -ScctAudioVolume") : TEXT(""),
		AreObjectiveSoundsOn() ? TEXT("") : TEXT(", objective jingles off"));
}

bool UScctAudioMix::LogPeaks(float DeltaTime)
{
	for (TPair<uint32, FDeviceState>& Pair : Devices)
	{
		if (Pair.Value.Peaks.Num() != NumCategories + 1)
		{
			continue;
		}
		FString Line;
		int32 Buffers = 0;
		const float Master = Pair.Value.Peaks[MainPeak]->Take(Buffers);
		for (int32 Index = 0; Index < NumCategories; ++Index)
		{
			int32 CategoryBuffers = 0;
			const float Peak = Pair.Value.Peaks[Index + 1]->Take(CategoryBuffers);
			Line += FString::Printf(TEXT(" %s=%.6f/%d"), CategoryName(static_cast<EScctAudioCategory>(Index)), Peak, CategoryBuffers);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: peak master=%.6f/%d%s (largest sample after the submix's volume / buffers rendered in the last second; device %u)"), Master, Buffers, *Line, Pair.Key);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
