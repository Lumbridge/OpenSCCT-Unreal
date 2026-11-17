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
#include "Menu/ScctUserSettings.h"
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

	/** The player's volume (Menu/ScctUserSettings.h) of each category, in EScctAudioCategory order. */
	const EScctSoundKind CategoryKinds[NumCategories] = {
		EScctSoundKind::Effects, EScctSoundKind::Music, EScctSoundKind::MenuMusic, EScctSoundKind::Objective,
		EScctSoundKind::MenuSounds, EScctSoundKind::Voice };

	/** The settings' field of a category's volume (0..100), or of the master volume for INDEX_NONE. */
	int32* VolumeField(UScctUserSettings& Settings, int32 Index)
	{
		switch (Index)
		{
		case INDEX_NONE: return &Settings.MasterVolume;
		case 0: return &Settings.EffectsVolume;
		case 1: return &Settings.MusicVolume;
		case 2: return &Settings.MenuMusicVolume;
		case 3: return &Settings.ObjectiveVolume;
		case 4: return &Settings.MenuSoundsVolume;
		default: return &Settings.VoiceVolume;
		}
	}

	bool ParseCategory(const FString& Name, int32& OutIndex);

	/** Sets a volume from the console, as the settings page would: "scct.Audio.Volume Music 0.25" or "... default". */
	FAutoConsoleCommand VolumeCommand(TEXT("scct.Audio.Volume"),
		TEXT("Sets a volume of the settings (Master, Effects, Music, MenuMusic, Objective, MenuSounds, Voice) to 0..1, or back to its default with 'default'. Applies at once and is kept in the user settings."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			int32 Index = INDEX_NONE;
			if (Args.Num() != 2 || !ParseCategory(Args[0], Index))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctAudio: usage: scct.Audio.Volume <Master|Effects|Music|MenuMusic|Objective|MenuSounds|Voice> <0..1|default>"));
				return;
			}
			UScctUserSettings* Settings = UScctUserSettings::Get();
			int32* Field = VolumeField(*Settings, Index);
			if (Args[1].Equals(TEXT("default"), ESearchCase::IgnoreCase))
			{
				// A fresh object holds the defaults (its members' initial values).
				UScctUserSettings* Defaults = NewObject<UScctUserSettings>();
				*Field = *VolumeField(*Defaults, Index);
			}
			else
			{
				*Field = FMath::Clamp(FMath::RoundToInt32(FCString::Atof(*Args[1]) * 100.0f), 0, 100);
			}
			Settings->ApplyAll();
			Settings->Save();
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

void UScctAudioMix::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

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
	SettingsHandle = UScctUserSettings::Get()->OnApplied.AddUObject(this, &UScctAudioMix::HandleSettingsApplied);
	EnsureApplied();

	if (bLogPeaks)
	{
		PeakTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctAudioMix::LogPeaks), 1.0f);
	}
}

void UScctAudioMix::Deinitialize()
{
	UScctUserSettings::Get()->OnApplied.Remove(SettingsHandle);
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

void UScctAudioMix::HandleSettingsApplied()
{
	const float OldMaster = MasterGain;
	float Old[NumCategories];
	FMemory::Memcpy(Old, Gains, sizeof(Gains));
	ReadGains();
	if (OldMaster != MasterGain || FMemory::Memcmp(Old, Gains, sizeof(Gains)) != 0)
	{
		Apply();
	}
}

void UScctAudioMix::ReadGains()
{
	const UScctUserSettings* Settings = UScctUserSettings::Get();
	MasterGain = ForcedMaster >= 0.0f ? ForcedMaster : FMath::Clamp(Settings->MasterVolume / 100.0f, 0.0f, 1.0f);
	for (int32 Index = 0; Index < NumCategories; ++Index)
	{
		Gains[Index] = Forced[Index] >= 0.0f ? Forced[Index] : Settings->GetGain(CategoryKinds[Index]);
	}
}

void UScctAudioMix::Apply()
{
	// The player's volumes are set where the sounds are played: the master volume is the engine's
	// (FApp::SetVolumeMultiplier), the match's voices carry the volume of their kind (Play/ScctAudioSubsystem) and the
	// menus' sounds theirs (Menu/ScctMenuSubsystem). The submixes keep only their headroom, so nothing is turned down
	// twice; other players' voices have no player of their own yet, so the voice submix carries the voice volume. A gain
	// forced by -ScctAudioVolume is set on the submix (over the player's volume) for a check.
	float Applied[NumCategories];
	for (int32 Index = 0; Index < NumCategories; ++Index)
	{
		const bool bVoice = static_cast<EScctAudioCategory>(Index) == EScctAudioCategory::Voice;
		const float SubmixGain = Forced[Index] >= 0.0f ? Forced[Index] : bVoice ? Gains[Index] : 1.0f;
		Applied[Index] = SubmixGain * (bVoice ? 1.0f : NonVoiceHeadroom);
	}
	const float MainSubmixGain = ForcedMaster >= 0.0f ? ForcedMaster : 1.0f;

	int32 NumDevices = 0;
	if (FAudioDeviceManager* Manager = GEngine != nullptr ? GEngine->GetAudioDeviceManager() : nullptr)
	{
		Manager->IterateOverAllDevices([this, &Applied, &NumDevices, MainSubmixGain](Audio::FDeviceId Id, FAudioDevice* Device)
		{
			if (Device == nullptr)
			{
				return;
			}
			++NumDevices;
			Device->SetSubmixOutputVolume(&Device->GetMainSubmixObject(), MainSubmixGain);
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

	UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: volumes master=%.2f effects=%.2f music=%.2f menumusic=%.2f objective=%.2f menusounds=%.2f voice=%.2f (from the settings; set on the voices, the submixes keep 3 dB headroom on all but master and voice; %d audio devices%s%s)"),
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
