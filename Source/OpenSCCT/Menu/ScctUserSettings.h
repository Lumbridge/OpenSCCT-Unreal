#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ScctUserSettings.generated.h"

/** The kinds of sound the audio tab has a volume for. */
enum class EScctSoundKind : uint8
{
	Effects,  // everything a match plays that is neither ambient nor music
	Ambient,  // the map's ambient loops
	Music,    // the match's music
	MenuMusic,
	MenuSounds,
	Objective, // the objective jingles (at 0 they are not played at all)
	Voice,     // other players' voices
};

/**
 * The player's video, audio and interface settings that are Unreal's to keep (the controls are the bridge's:
 * Play/ScctControlSettings.h). One object for the process: UScctUserSettings::Get().
 *
 * Where each value lives:
 *   - resolution, window mode, vertical sync, frame limit and the quality groups are UGameUserSettings' own
 *     (GameUserSettings.ini); the settings page changes them there and this object only applies and saves them;
 *   - everything below is kept in the same file under [OpenSCCT.Settings] and applied here: the volumes (the match's
 *     audio subsystem and the menus' own sounds ask GetGain), motion blur and the display gamma (console variables,
 *     set as a game setting so a command line or a console still wins), the unit of the HUD's distances.
 *
 * ApplyAll runs once when the game instance starts, so a setting is in force from the first frame of every run.
 * Unattended runs (-unattended) never change the window: the resolution and the window mode are saved but not applied.
 */
UCLASS()
class OPENSCCT_API UScctUserSettings : public UObject
{
	GENERATED_BODY()

public:
	static UScctUserSettings* Get();

	/** Reads [OpenSCCT.Settings]; missing values keep their defaults. Done once by Get. */
	void Load();

	/** Writes [OpenSCCT.Settings] and Unreal's own game user settings. */
	void Save();

	/** Puts every value in force: the engine's volume, the console variables, the HUD option of a running match. */
	void ApplyAll();

	/** The defaults of one tab: 0 video, 1 audio, 2 interface. Not saved until Save. */
	void ResetVideo();
	void ResetAudio();
	void ResetInterface();

	/** 0..1: the volume of a kind of sound (the master volume is the engine's and not part of it). */
	float GetGain(EScctSoundKind Kind) const;

	/** Counts up whenever a volume changed, so a player of sound can tell its voices need their gain again. */
	int32 GetAudioRevision() const { return AudioRevision; }

	/** Raised by ApplyAll, after the volumes are in force: a mixer that sets gains of its own reads them again. */
	FSimpleMulticastDelegate OnApplied;

	// Volumes, 0 to 100.
	int32 MasterVolume = 100;
	int32 EffectsVolume = 80;
	int32 AmbientVolume = 80;
	int32 MusicVolume = 70;
	int32 MenuMusicVolume = 50;
	int32 MenuSoundsVolume = 70;
	int32 ObjectiveVolume = 80;
	int32 VoiceVolume = 100;

	bool bMotionBlur = true;

	/** The display gamma: 2.2 is the engine's own, higher is brighter. */
	float Gamma = 2.2f;

	/** The 3D picture's share of the window's resolution, 50 to 100 per cent. */
	int32 RenderScale = 100;

	/** The HUD's distances in feet (true) or metres. */
	bool bDistanceInFeet = false;

	/** Log lines with every value, for unattended checks. */
	void LogValues(const TCHAR* Why) const;

private:
	int32 AudioRevision = 0;
	bool bLoaded = false;
};
