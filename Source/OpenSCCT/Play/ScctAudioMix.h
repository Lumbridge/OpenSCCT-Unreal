#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctAudioMix.generated.h"

class FAudioDevice;
class FScctSubmixPeak;
class USoundClass;
class USoundSubmix;

/**
 * What a sound is, for its volume slider. Godot sends each to a bus of its own (game/Scripts/Audio/ReverbBuses.cs:
 * SvmWorld and SvmFlat, SvmMusic, SvmMenuMusic, SvmObjective, SvmMenuSound, and the voice bus).
 */
enum class EScctAudioCategory : uint8
{
	/** The match's sound effects: 3D and flat event sounds, ambient loops, and the room's reverb. */
	World,
	/** The music of a round (the map's Song, the end of round music). */
	Music,
	/** The menus' music (main menu, lobby). */
	MenuMusic,
	/** The objective jingles (a Jingl sound of an Interface package's Objectifs group). */
	Objective,
	/** The menus' own sounds (button feedback, the join sound). */
	MenuSound,
	/** Other players' voices. */
	Voice,
	Num
};

/**
 * The game's volume categories as Unreal objects, and the player's Audio options that set their gains.
 *
 * Made at run time, once per game: a sound class per category (all children of one Master class) whose default
 * submix is the category's own submix, routed to the engine's main submix. Whoever plays a sound gives its voice the
 * category's class (UAudioComponent::SoundClassOverride) and the voice is then mixed into that submix. The gains are
 * the submixes' output volumes:
 *
 *   - Master is the engine's main submix, so it covers everything, the reverb included;
 *   - Effects is the World submix and the reverb submix (the room is heard on the match's sound effects only: the
 *     other classes send nothing to the reverb);
 *   - the rest are their own submixes.
 *
 * Every category but Master and Voice keeps 3 dB of summing headroom, as Godot's SettingsApplier.ApplyAudio does.
 * The options are on the settings page "Audio" (group Volume) with Godot's defaults, and a change applies at once.
 * Objective volume 0 also turns the objective jingles off, as the original profile's bObjectiveSounds does.
 *
 * Unattended switches:
 *   -ScctAudioVolume=Cat:Value[,Cat:Value...]   force gains for a check, over the saved options (Master, Effects,
 *                                               Music, MenuMusic, Objective, MenuSounds, Voice; 0..1)
 *   -ScctAudioPeaks                             log the peak sample of the main submix and of every category's
 *                                               submix (after its gain) once per second
 * Console: scct.Audio.Volume <Category> <0..1|default> sets an option as the Options screen would.
 *
 * An off-screen run never has the focus, and Unreal mutes an unfocused game ([Audio] UnfocusedVolumeMultiplier 0):
 * a check that reads the peaks adds -ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0 (with -DeterministicAudio
 * nothing is heard).
 */
UCLASS()
class OPENSCCT_API UScctAudioMix : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Puts the Audio page's volume options into the settings registry (harmless when they are already in). */
	static void RegisterSettings();

	/** The class a voice of this category plays with, null without an audio engine. */
	USoundClass* GetClass(EScctAudioCategory Category) const;
	USoundSubmix* GetSubmix(EScctAudioCategory Category) const;

	/** The category's gain as applied, without Master and without the headroom (the option, or its forced value). */
	float GetGain(EScctAudioCategory Category) const { return Gains[static_cast<int32>(Category)]; }
	float GetMasterGain() const { return MasterGain; }

	/** False when the objective volume is 0: the objective jingles are then not played at all. */
	bool AreObjectiveSoundsOn() const { return GetGain(EScctAudioCategory::Objective) > 0.0f; }

	/**
	 * Makes sure the audio engine knows the classes and submixes and has the current gains. The match calls this when
	 * it starts, so an audio device made after the game started gets them too.
	 */
	void EnsureApplied();

	static const TCHAR* CategoryName(EScctAudioCategory Category);

private:
	void CreateObjects();
	void RegisterWithDevices();
	void HandleSettingChanged(FName Key);
	void ReadGains();
	void Apply();
	bool LogPeaks(float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> MasterClass;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundClass>> Classes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundSubmix>> Submixes;

	/** The engine's reverb submix, which carries the room of the World sounds. */
	UPROPERTY(Transient)
	TObjectPtr<USoundSubmix> ReverbSubmix;

	/** Per audio device id: the classes and submixes are registered with it, and its peak listeners. */
	struct FDeviceState
	{
		TArray<TSharedRef<FScctSubmixPeak, ESPMode::ThreadSafe>> Peaks;
	};
	TMap<uint32, FDeviceState> Devices;

	float Gains[static_cast<int32>(EScctAudioCategory::Num)] = {};
	float MasterGain = 1.0f;

	/** Gains forced by -ScctAudioVolume, by category, and for Master; negative: not forced. */
	float Forced[static_cast<int32>(EScctAudioCategory::Num)] = {};
	float ForcedMaster = -1.0f;

	bool bLogPeaks = false;
	FTSTicker::FDelegateHandle PeakTicker;
	FDelegateHandle SettingsHandle;
};
