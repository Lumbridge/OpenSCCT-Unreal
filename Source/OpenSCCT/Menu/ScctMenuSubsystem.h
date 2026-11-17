#pragma once

#include "CoreMinimal.h"
#include "Play/ScctSoundData.h"
#include "Runtime/ScctMenuApi.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctMenuSubsystem.generated.h"

class UAudioComponent;
class UScctSoundWave;
class UTexture2D;

/** The sounds the menus make, from the content's own interface sounds (shared/audio/wav/Interface/Menu). */
enum class EScctUiSound : uint8
{
	Move,   // the focus went to another control
	Accept, // a control was pressed
	Back,   // a page or a dialog was closed
	Open,   // a page or a dialog opened
	Notice, // a toast, a chat line
	Count,
};

/** A map the menus list: OpenSCCT's catalog entry and whether its level is imported into this project. */
struct FScctMenuMapInfo
{
	FString Id;
	FString Title;
	FScctMenuMap Data = {};
	bool bImported = false;

	/** Index into Data.Modes of the bridge's mode key (Story, DiskHunt, Deathmatch, MercDeathMatch). */
	bool Plays(int32 ModeIndex) const { return ModeIndex >= 0 && ModeIndex < 4 && Data.Modes[ModeIndex].Playable != 0; }
};

/**
 * What every menu shares and what must outlive a level: the map catalog and the game's own strings (through the
 * bridge's menu table, Runtime/ScctMenuApi.h), the pictures read from the content folder, the interface sounds (the menu music is
 * Play/ScctFrontEndAudio.h's). -ScctMenuSoundLog logs every interface sound played. It also puts the saved settings in force when the game starts (Menu/ScctUserSettings.h).
 *
 * Everything degrades without content: no maps, no pictures, the menus' own English captions, silence.
 */
UCLASS()
class OPENSCCT_API UScctMenuSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UScctMenuSubsystem* Get(const UObject* WorldContext);

	/** The one instance of the running game, for the widgets (which have no world of their own). */
	static UScctMenuSubsystem* GetCurrent() { return Current.Get(); }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- maps and texts --------------------------------------------------------------------------------------

	/** The host list's maps that can be played here (content converted and level imported), in the host list's order. */
	const TArray<FScctMenuMapInfo>& GetMaps();

	/** Any catalog map by folder name, playable here or not; null when the content does not have it. */
	const FScctMenuMapInfo* FindMap(const FString& Id);

	/** Reads the map list again on next use: a map was downloaded or removed (Menu/SScctMenuContent.cpp). */
	void RefreshMaps();

	/** The name the pages show for a map ("ENHANCED AQUARIUS"), its folder name in capitals when unknown. */
	FString GetMapTitle(const FString& Id);

	FString GetMapText(const FString& Id, EScctMenuMapText Kind);

	/** What the map says about a mode (index 0..3) for a side: Kind 0 the objective line, 1 the briefing. */
	FString GetModeText(const FString& Id, int32 ModeIndex, bool bMerc, int32 Kind);

	/** A string of GUI.int from the content, or Fallback. A '|' (the original's line break) becomes a space. */
	FString Text(const TCHAR* InSection, const TCHAR* Key, const TCHAR* Fallback);

	/** "OpenSCCT 0.8.150" as the bridge was built, empty without the bridge. */
	FString GetGameVersion();

	const TArray<FScctMenuAction>& GetActions();

	// ---- pictures --------------------------------------------------------------------------------------------

	/** A PNG of the content folder as a brush, read once; null when the file is missing or unreadable. */
	const FSlateBrush* GetPicture(const FString& File);

	/** The best picture of a map for a wide frame: its banner, else the side's loading picture, else its list picture. */
	const FSlateBrush* GetMapPicture(const FString& Id, bool bMerc);

	// ---- sound -----------------------------------------------------------------------------------------------

	void PlaySound(EScctUiSound Sound);

	/** Loads a sound ahead of its first use (the first PlaySound of a sound only starts its decode). */
	void PrepareSound(EScctUiSound Sound);

	/** Allows or silences the menu music, which follows the page on screen (Play/ScctFrontEndAudio.h). */
	void SetMusicPlaying(bool bPlaying);

	/** The converted content's folder, empty without the bridge. */
	FString GetContentRoot();

	/** Brings the music's volume up to date with the settings. */
	void RefreshVolumes();

	/** The mode names and keys of the original's mode box, in its order. */
	static const TCHAR* ModeKey(int32 ModeIndex);
	static int32 ModeId(int32 ModeIndex);
	FString GetModeName(int32 ModeIndex);

private:
	const FScctMenuApi* Connect();
	void LoadMaps();
	UScctSoundWave* FindWave(int32 Slot, const FString& File, bool bLoop);

	static TWeakObjectPtr<UScctMenuSubsystem> Current;

	const FScctMenuApi* Api = nullptr;
	bool bMapsLoaded = false;
	TArray<FScctMenuMapInfo> AllMaps;
	TArray<FScctMenuMapInfo> PlayableMaps;
	TArray<FScctMenuAction> Actions;
	TMap<FString, FString> Strings;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Textures;

	TMap<FString, TSharedPtr<FSlateBrush>> Brushes;

	FScctSoundCache SoundCache;
	FString SharedRoot;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UScctSoundWave>> Waves;

	/** -ScctMenuSoundLog: every interface sound is logged. */
	bool bLogSounds = false;
	double LastMoveSound = 0.0;
};
