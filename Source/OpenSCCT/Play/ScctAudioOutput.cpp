#include "Play/ScctAudioOutput.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "Engine/Engine.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"

TWeakObjectPtr<UScctAudioOutputSubsystem> UScctAudioOutputSubsystem::Current;

namespace ScctAudioOutputLocal
{
	const FName OutputDeviceKey(TEXT("Audio.OutputDevice"));
	const FName OutputModeKey(TEXT("Audio.OutputMode"));

	/** How often the device list is read (Godot's AudioOutput polls every 2 s). */
	constexpr float CheckSeconds = 2.0f;
	/** A swap asked for is not asked for again before this, while the engine carries it out. */
	constexpr double SwapRetrySeconds = 6.0;

	/** The spatializer as last looked at (it does not change while the game runs). */
	bool bSpatializerChecked = false;
	bool bSpatializerReady = false;
	FString SpatializerName;
	FString SpatializerProblemText;
	bool bSpatializerProblemLogged = false;

	FString DescribeDevices(const TArray<FScctAudioOutputDevice>& List, const FString& CurrentId)
	{
		FString Text;
		for (int32 Index = 0; Index < List.Num(); ++Index)
		{
			const FScctAudioOutputDevice& Device = List[Index];
			Text += FString::Printf(TEXT("%s[%d] '%s'%s%s"), Index > 0 ? TEXT(", ") : TEXT(""), Index, *Device.Name,
				Device.bSystemDefault ? TEXT(" (system default)") : TEXT(""), Device.Id == CurrentId ? TEXT(" (playing)") : TEXT(""));
		}
		return Text.IsEmpty() ? FString(TEXT("none")) : Text;
	}
}

using namespace ScctAudioOutputLocal;

void UScctAudioOutputSubsystem::RegisterSettings()
{
	{
		FScctSettingDesc Desc;
		Desc.Key = OutputDeviceKey;
		Desc.Page = TEXT("Audio");
		Desc.Group = FText::FromString(TEXT("Output"));
		Desc.Label = FText::FromString(TEXT("Output device"));
		Desc.Tooltip = FText::FromString(TEXT("Where the game's sound plays. Default follows the system's default device; a device that is unplugged falls back to it until it is back."));
		Desc.Type = EScctSettingType::Text;
		Desc.Default = FString();
		// Drawn by the settings panel's Output section, which follows the device list as it changes.
		Desc.bVisible = false;
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc;
		Desc.Key = OutputModeKey;
		Desc.Page = TEXT("Audio");
		Desc.Group = FText::FromString(TEXT("Output"));
		Desc.Label = FText::FromString(TEXT("Output mode"));
		Desc.Tooltip = FText::FromString(TEXT("Headphones renders the sounds around you binaurally (HRTF), so above, below and behind can be told apart. Speakers pans them between the speakers."));
		Desc.Type = EScctSettingType::Choice;
		Desc.Default = TEXT("0");
		Desc.Choices = { FText::FromString(TEXT("Speakers")), FText::FromString(TEXT("Headphones")) };
		Desc.bVisible = false;
		FScctSettings::Register(Desc);
	}
}

void UScctAudioOutputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Current = this;
	RegisterSettings();
	SettingsHandle = FScctSettings::OnChanged().AddUObject(this, &UScctAudioOutputSubsystem::OnSettingChanged);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctAudioOutputSubsystem::Tick));
	// The first look at once, not 2 s in.
	SinceCheck = CheckSeconds;
}

void UScctAudioOutputSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FScctSettings::OnChanged().Remove(SettingsHandle);
	if (Current.Get() == this)
	{
		Current.Reset();
	}
	Super::Deinitialize();
}

bool UScctAudioOutputSubsystem::HeadphonesChosen()
{
	return FScctSettings::GetInt(OutputModeKey) == 1;
}

void UScctAudioOutputSubsystem::CheckSpatializer()
{
	if (bSpatializerChecked || GEngine == nullptr)
	{
		return;
	}
	FAudioDeviceHandle Device = GEngine->GetMainAudioDevice();
	if (!Device.IsValid())
	{
		// No audio engine (-nosound, a server): nothing is heard, so there is nothing to spatialize. Asked again later.
		SpatializerProblemText = TEXT("Binaural audio unavailable: the game has no audio device; using speaker panning.");
		return;
	}
	bSpatializerChecked = true;
	if (!Device->IsSpatializationPluginEnabled())
	{
		SpatializerProblemText = TEXT("Binaural audio unavailable: no spatialization plugin is loaded (Resonance Audio, Config/DefaultEngine.ini SpatializationPlugin); using speaker panning.");
		return;
	}
	SpatializerName = Device->GetCurrentSpatializationPluginInterfaceInfo().PluginName.ToString();
	if (SpatializerName == TEXT("Resonance Audio") && !Device->IsReverbPluginEnabled())
	{
		SpatializerProblemText = TEXT("Binaural audio unavailable: Resonance Audio's spatializer needs its reverb plugin, which is not loaded; using speaker panning.");
		return;
	}
	bSpatializerReady = true;
	SpatializerProblemText.Reset();
}

bool UScctAudioOutputSubsystem::UseHrtf()
{
	if (!HeadphonesChosen())
	{
		return false;
	}
	CheckSpatializer();
	if (!bSpatializerReady && !bSpatializerProblemLogged && bSpatializerChecked)
	{
		bSpatializerProblemLogged = true;
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctAudioOutput: SpatializerProblem: %s"), *SpatializerProblemText);
	}
	return bSpatializerReady;
}

FString UScctAudioOutputSubsystem::SpatializerProblem()
{
	if (!HeadphonesChosen())
	{
		return FString();
	}
	CheckSpatializer();
	return bSpatializerReady ? FString() : SpatializerProblemText;
}

bool UScctAudioOutputSubsystem::Tick(float DeltaSeconds)
{
	SinceCheck += DeltaSeconds;
	if (SinceCheck >= CheckSeconds && !bReadPending)
	{
		SinceCheck = 0.0f;
		RequestDeviceList();
	}
	return true;
}

void UScctAudioOutputSubsystem::RequestDeviceList()
{
	if (GEngine == nullptr || GEngine->GetAudioDeviceManager() == nullptr || !GEngine->GetMainAudioDevice().IsValid())
	{
		if (!bReportedNoDevice)
		{
			bReportedNoDevice = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: no audio device (sound is off for this run); device=%s mode=%s"),
				FScctSettings::GetString(OutputDeviceKey).IsEmpty() ? TEXT("Default") : *FScctSettings::GetString(OutputDeviceKey),
				HeadphonesChosen() ? TEXT("Headphones") : TEXT("Speakers"));
		}
		return;
	}

	bReadPending = true;
	const Audio::FDeviceId MainId = GEngine->GetMainAudioDeviceID();
	TWeakObjectPtr<UScctAudioOutputSubsystem> Weak(this);
	// The device list belongs to the audio thread's side of the mixer, as UAudioMixerBlueprintLibrary reads it.
	FAudioThread::RunCommandOnAudioThread([MainId, Weak]()
	{
		TArray<FScctAudioOutputDevice> List;
		FString CurrentId;
		FString CurrentName;
		FAudioDeviceManager* Manager = FAudioDeviceManager::Get();
		FAudioDevice* Device = Manager != nullptr ? Manager->GetAudioDeviceRaw(MainId) : nullptr;
		Audio::IAudioMixerPlatformInterface* Platform = Device != nullptr ? static_cast<Audio::FMixerDevice*>(Device)->GetAudioMixerPlatform() : nullptr;
		if (Platform != nullptr)
		{
			const Audio::FAudioPlatformDeviceInfo Playing = Platform->GetPlatformDeviceInfo();
			CurrentId = Playing.DeviceId;
			CurrentName = Playing.Name;
			const auto Add = [&List](const Audio::FAudioPlatformDeviceInfo& Info)
			{
				FScctAudioOutputDevice& Entry = List.AddDefaulted_GetRef();
				Entry.Name = Info.Name;
				Entry.Id = Info.DeviceId;
				Entry.bSystemDefault = Info.bIsSystemDefault;
			};
			if (Audio::IAudioPlatformDeviceInfoCache* Cache = Platform->GetDeviceInfoCache())
			{
				for (const Audio::FAudioPlatformDeviceInfo& Info : Cache->GetAllActiveOutputDevices())
				{
					Add(Info);
				}
			}
			else
			{
				uint32 Count = 0;
				Platform->GetNumOutputDevices(Count);
				for (uint32 Index = 0; Index < Count; ++Index)
				{
					Audio::FAudioPlatformDeviceInfo Info;
					if (Platform->GetOutputDeviceInfo(Index, Info))
					{
						Add(Info);
					}
				}
			}
		}
		FAudioThread::RunCommandOnGameThread([Weak, List = MoveTemp(List), CurrentId = MoveTemp(CurrentId), CurrentName = MoveTemp(CurrentName)]() mutable
		{
			if (UScctAudioOutputSubsystem* Self = Weak.Get())
			{
				Self->OnDevicesRead(MoveTemp(List), MoveTemp(CurrentId), MoveTemp(CurrentName));
			}
		});
	});
}

void UScctAudioOutputSubsystem::OnDevicesRead(TArray<FScctAudioOutputDevice>&& NewDevices, FString&& CurrentId, FString&& CurrentName)
{
	bReadPending = false;
	Devices = MoveTemp(NewDevices);
	CurrentDeviceId = MoveTemp(CurrentId);
	CurrentDeviceName = MoveTemp(CurrentName);

	const bool bFirst = !bHaveList;
	bHaveList = true;
	// The list is logged when it is first read and whenever a device comes or goes.
	FString ListText;
	for (const FScctAudioOutputDevice& Device : Devices)
	{
		ListText += Device.Id + TEXT("|");
	}
	if (bFirst || ListText != LastListText)
	{
		LastListText = ListText;
		const FString Saved = FScctSettings::GetString(OutputDeviceKey);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: devices (%d): %s; playing on '%s'; device=%s"), Devices.Num(),
			*DescribeDevices(Devices, CurrentDeviceId), *CurrentDeviceName, Saved.IsEmpty() ? TEXT("Default") : *Saved);
	}
	if (bFirst)
	{
		ReportMode(TEXT("start"));
	}
	SelectAvailable(bFirst ? TEXT("start") : TEXT("device check"));
}

void UScctAudioOutputSubsystem::SelectAvailable(const TCHAR* Why)
{
	if (!bHaveList || Devices.Num() == 0)
	{
		return;
	}
	const FString Saved = FScctSettings::GetString(OutputDeviceKey);
	const FScctAudioOutputDevice* Wanted = nullptr;
	if (!Saved.IsEmpty())
	{
		Wanted = Devices.FindByPredicate([&Saved](const FScctAudioOutputDevice& Device) { return Device.Name == Saved; });
		if (Wanted == nullptr && !bFallingBack)
		{
			bFallingBack = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctAudioOutput: the saved output device '%s' is not connected; falling back to the default device until it is (%s)"), *Saved, Why);
		}
		else if (Wanted != nullptr && bFallingBack)
		{
			bFallingBack = false;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: the saved output device '%s' is connected again (%s)"), *Saved, Why);
		}
	}
	else
	{
		bFallingBack = false;
	}

	if (Wanted != nullptr)
	{
		if (Wanted->Id != CurrentDeviceId)
		{
			RequestSwap(Wanted->Id, Wanted->Name, Why);
		}
		return;
	}
	// The default: only when the sound is on another device than the system's default one (the engine follows the
	// default by itself once it is on it).
	const FScctAudioOutputDevice* SystemDefault = Devices.FindByPredicate([](const FScctAudioOutputDevice& Device) { return Device.bSystemDefault; });
	if (SystemDefault != nullptr && SystemDefault->Id != CurrentDeviceId)
	{
		RequestSwap(FString(), FString::Printf(TEXT("Default (%s)"), *SystemDefault->Name), Why);
	}
}

void UScctAudioOutputSubsystem::RequestSwap(const FString& DeviceId, const FString& Label, const TCHAR* Why)
{
	const double Now = FPlatformTime::Seconds();
	if (DeviceId == LastRequestedId && Now - LastRequestTime < SwapRetrySeconds)
	{
		return;
	}
	LastRequestedId = DeviceId;
	LastRequestTime = Now;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: switching the sound from '%s' to '%s' (%s)"), *CurrentDeviceName, *Label, Why);

	if (GEngine == nullptr || !GEngine->GetMainAudioDevice().IsValid())
	{
		return;
	}
	const Audio::FDeviceId MainId = GEngine->GetMainAudioDeviceID();
	FAudioThread::RunCommandOnAudioThread([MainId, DeviceId]()
	{
		FAudioDeviceManager* Manager = FAudioDeviceManager::Get();
		FAudioDevice* Device = Manager != nullptr ? Manager->GetAudioDeviceRaw(MainId) : nullptr;
		if (Audio::IAudioMixerPlatformInterface* Platform = Device != nullptr ? static_cast<Audio::FMixerDevice*>(Device)->GetAudioMixerPlatform() : nullptr)
		{
			Platform->RequestDeviceSwap(DeviceId, /*bInForce*/ false, TEXT("OpenSCCT output device setting"));
		}
	});
	// Read the list again soon, to see where the sound went.
	SinceCheck = CheckSeconds - 0.5f;
}

TArray<FString> UScctAudioOutputSubsystem::DeviceChoices() const
{
	// The stepper's box holds about 20 capitals between its arrows; Windows' device names are often longer ("1. (Speakers) Rear Panel
	// (High Definition Audio Device)"), so a long one is cut, its start being what tells devices apart.
	const auto Fit = [](const FString& Name, int32 Room)
	{
		return Name.Len() <= Room ? Name : Name.Left(FMath::Max(Room - 3, 1)).TrimEnd() + TEXT("...");
	};
	constexpr int32 Room = 20;
	TArray<FString> Choices;
	const FScctAudioOutputDevice* SystemDefault = Devices.FindByPredicate([](const FScctAudioOutputDevice& Device) { return Device.bSystemDefault; });
	Choices.Add(SystemDefault != nullptr ? Fit(FString::Printf(TEXT("Default: %s"), *SystemDefault->Name), Room) : FString(TEXT("Default")));
	for (const FScctAudioOutputDevice& Device : Devices)
	{
		Choices.Add(Fit(Device.Name, Room));
	}
	const FString Saved = FScctSettings::GetString(OutputDeviceKey);
	if (!Saved.IsEmpty() && !Devices.ContainsByPredicate([&Saved](const FScctAudioOutputDevice& Device) { return Device.Name == Saved; }))
	{
		Choices.Add(Fit(Saved, Room - 10) + TEXT(" (missing)"));
	}
	return Choices;
}

int32 UScctAudioOutputSubsystem::ChosenDeviceIndex() const
{
	const FString Saved = FScctSettings::GetString(OutputDeviceKey);
	if (Saved.IsEmpty())
	{
		return 0;
	}
	const int32 Found = Devices.IndexOfByPredicate([&Saved](const FScctAudioOutputDevice& Device) { return Device.Name == Saved; });
	// A missing saved device is the last choice (DeviceChoices).
	return Found != INDEX_NONE ? Found + 1 : Devices.Num() + 1;
}

void UScctAudioOutputSubsystem::ChooseDevice(int32 Index)
{
	if (Index <= 0)
	{
		FScctSettings::SetString(OutputDeviceKey, FString());
	}
	else if (Devices.IsValidIndex(Index - 1))
	{
		FScctSettings::SetString(OutputDeviceKey, Devices[Index - 1].Name);
	}
	// The missing saved device's own entry keeps it as it is.
	FScctSettings::Save();
}

void UScctAudioOutputSubsystem::OnSettingChanged(FName Key)
{
	if (Key == OutputDeviceKey)
	{
		const FString Saved = FScctSettings::GetString(OutputDeviceKey);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: device=%s chosen"), Saved.IsEmpty() ? TEXT("Default") : *Saved);
		bFallingBack = false;
		// A new choice is acted on at once, not after the retry wait of the last request.
		LastRequestTime = -1000.0;
		SelectAvailable(TEXT("setting changed"));
	}
	else if (Key == OutputModeKey)
	{
		ReportMode(TEXT("setting changed"));
	}
}

void UScctAudioOutputSubsystem::ReportMode(const TCHAR* Why)
{
	if (!HeadphonesChosen())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: mode=Speakers (%s): 3D voices are panned by the engine"), Why);
		return;
	}
	if (UseHrtf())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: mode=Headphones (%s): 3D voices are rendered by HRTF through the spatialization plugin '%s'"), Why, *SpatializerName);
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOutput: mode=Headphones (%s) falls back to panning: %s"), Why, *SpatializerProblem());
	}
}
