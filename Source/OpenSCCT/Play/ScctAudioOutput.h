#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctAudioOutput.generated.h"

/** One sound output of the machine, as the audio engine lists it. */
struct FScctAudioOutputDevice
{
	FString Name;
	FString Id;
	bool bSystemDefault = false;
};

/**
 * Where the game's sound goes and how 3D sound is rendered, a port of the Godot client's game/Scripts/Audio/AudioOutput.cs
 * and its Headphones mode (SoundSystem.Headphones, the binaural class of ReverbBuses.cs).
 *
 *   Audio.OutputDevice   the device's name; empty is the system's default device. A saved device that is not connected
 *                        falls back to the default, and the sound goes back to it when it is plugged in again: the
 *                        device list is read every 2 s, in the menus as in a match, as Godot polls it.
 *   Audio.OutputMode     0 Speakers: 3D voices are panned by the engine. 1 Headphones: they are rendered binaurally by
 *                        HRTF through the spatialization plugin (Resonance Audio, Config/DefaultEngine.ini). Without
 *                        the plugin the voices stay panned and SpatializerProblem says why (logged once).
 *
 * Both options are kept in the settings registry (Settings/ScctSettings.h) but drawn by the settings panel's own Audio
 * tab (Menu/SScctSettingsPanel.cpp), as one Output section with a stepper each: the device list changes while the page
 * is open, which a registry Choice cannot follow. A mode change reaches the voices started after it; a playing voice
 * keeps how it started (the engine cannot change a playing voice's spatialization).
 *
 * Unattended: -ScctSettingSet=Audio.OutputMode=1 or Audio.OutputDevice=<name> force them for a run. The log has the
 * device list and the chosen device ("ScctAudioOutput: devices (2): ... device=Default"), every switch and fallback, and
 * the mode with the plugin it uses.
 */
UCLASS()
class OPENSCCT_API UScctAudioOutputSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UScctAudioOutputSubsystem* GetCurrent() { return Current.Get(); }

	/** Registers the two options (also done by Initialize; safe to call again). */
	static void RegisterSettings();

	/** True when 3D voices started now are rendered by HRTF: Headphones is chosen and the plugin is there. */
	static bool UseHrtf();

	/** Headphones is chosen (whether or not the plugin can do it). */
	static bool HeadphonesChosen();

	/** Why Headphones falls back to panning, or empty. */
	static FString SpatializerProblem();

	/** The settings page's choices: "Default (<device>)" then each connected device, and the saved device when it is missing. */
	TArray<FString> DeviceChoices() const;
	/** The index in DeviceChoices of the saved device. */
	int32 ChosenDeviceIndex() const;
	/** Saves the device at an index of DeviceChoices and switches to it. */
	void ChooseDevice(int32 Index);

	/** The connected devices, as last read. */
	const TArray<FScctAudioOutputDevice>& GetDevices() const { return Devices; }
	/** The name of the device the sound plays on now. */
	const FString& GetCurrentDeviceName() const { return CurrentDeviceName; }

private:
	bool Tick(float DeltaSeconds);
	/** Reads the device list on the audio thread; the answer comes back in OnDevicesRead. */
	void RequestDeviceList();
	void OnDevicesRead(TArray<FScctAudioOutputDevice>&& NewDevices, FString&& CurrentId, FString&& CurrentName);
	/** Switches to the saved device when connected, else to the default, when the sound is elsewhere. */
	void SelectAvailable(const TCHAR* Why);
	void RequestSwap(const FString& DeviceId, const FString& Label, const TCHAR* Why);
	void OnSettingChanged(FName Key);
	void ReportMode(const TCHAR* Why);
	static void CheckSpatializer();

	static TWeakObjectPtr<UScctAudioOutputSubsystem> Current;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle SettingsHandle;
	float SinceCheck = 0.0f;
	bool bReadPending = false;
	bool bHaveList = false;
	bool bReportedNoDevice = false;

	TArray<FScctAudioOutputDevice> Devices;
	FString CurrentDeviceId;
	FString CurrentDeviceName;
	/** The device last asked for ("" is the default) and when, so a swap in flight is not asked for again at once. */
	FString LastRequestedId;
	double LastRequestTime = -1000.0;
	/** The saved device was missing at the last look (so its fallback and its return are each logged once). */
	bool bFallingBack = false;
	FString LastListText;
};
