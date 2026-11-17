#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Play/ScctSoundData.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctFrontEndAudio.generated.h"

class UAudioComponent;
class UScctSoundWave;
class UWorld;

/** What the front end shows, for its music. */
enum class EScctFrontEndPage : uint8
{
	/** Not in the menus: a match or the hub plays its own sounds. */
	None,
	/** The main menu and the pages under it (the original's Entry level). */
	Entry,
	/** The lobby of a server (the original's Lobby level). */
	Lobby,
	/** Connecting, travelling, a level load. */
	Loading,
	/** A round's results. */
	Results,
};

/**
 * The front end's music and the lobby's join sound, a port of the Godot client's game/Scripts/Audio/FrontEndAudio.cs.
 *
 * The original plays a menu level's LevelInfo Song when the level loads, and its menus live on two levels: Entry (the
 * main menu and the pages under it, Song Menu_music1) and Lobby (the lobby room, Song Menu_music2, Enhanced's longer
 * lobby track). OpenSCCT's menus are pages of one front end level, so this subsystem plays the Song of the level that
 * goes with what is on screen: the Lobby's in a server's lobby, the Entry's on the other menu pages, none while
 * connecting, travelling or loading, on the results and in a match or the hub. The song is read from the converted
 * level's audio table (svmap/<Level>/audio/audio.json: the LevelInfo's sounds.Song and that sound's stream); a lobby
 * without its own falls back to the Entry's. A change of track crossfades over 2 s (PlayMusic's minimum fade); a level
 * load stops the music at once, as the load blocks the frames a fade needs. The music plays at the menu music volume
 * on the MenuMusic sound class (Play/ScctAudioMix.h), without the sound's own gain, as Godot's does.
 *
 * Join: Reloaded's lobby plays a notification when a person (not a bot, not the player) comes into the lobby after it
 * was reached; as in Godot that is the menus' message beep (Interface.Menu.bip_message, the menus' Notice sound).
 *
 * The page is found from the state of the game each frame (the online visit, the open level and a level load), so
 * nothing in the menus has to report it; UScctMenuSubsystem::SetMusicPlaying(false) silences the menus' music.
 *
 * Unattended switch: -ScctFrontEndPage=Entry|Lobby|Loading|Results holds the page for a run (a look at the lobby
 * music without a server). Every page change is logged ("ScctFrontEndAudio: page=Lobby track=Menu_music2 ...").
 */
UCLASS()
class OPENSCCT_API UScctFrontEndAudio : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The one instance of the running game. */
	static UScctFrontEndAudio* GetCurrent() { return Current.Get(); }

	static const TCHAR* PageName(EScctFrontEndPage Page);

	/** The page the music follows now. */
	EScctFrontEndPage GetPage() const { return Page; }

	/** False silences the menus' music until it is allowed again. */
	void SetMusicAllowed(bool bAllowed);

	/** Brings the music up to date with the menu music volume at once (it also follows it on its own). */
	void RefreshVolume();

	/** The lobby's join notification. */
	void PlayJoin(const FString& Who);

private:
	bool Tick(float DeltaSeconds);
	EScctFrontEndPage FindPage(UWorld* World) const;
	void ShowPage(EScctFrontEndPage NewPage, UWorld* World);
	void StartWanted(UWorld* World);
	void FadeOutCurrent(float Seconds);
	void NoteLobbyJoins();
	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* World);
	float MusicGain() const;

	/** A menu level's song as a stream file, read from its audio table once; empty when the content has none. */
	struct FSong
	{
		FString Sound; // "Interface.Menu.Menu_music2"
		FString File;  // the stream's full path
		bool bRead = false;
	};
	const FSong& SongOf(const TCHAR* Level);

	static TWeakObjectPtr<UScctFrontEndAudio> Current;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle PreLoadHandle;
	FDelegateHandle PostLoadHandle;

	EScctFrontEndPage Page = EScctFrontEndPage::None;
	EScctFrontEndPage ForcedPage = EScctFrontEndPage::None;
	bool bForced = false;
	bool bMusicAllowed = true;
	bool bLoadingLevel = false;

	TMap<FString, FSong> Songs;
	FScctSoundCache Cache;
	TMap<FString, int32> FileIds;

	/** The stream the page wants (empty: silence) and the one playing. */
	FString WantedFile;
	FString WantedName;
	FString PlayingFile;
	FString PlayingName;
	FString FadingName;
	bool bDecodeLogged = false;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UScctSoundWave>> Waves;

	TMap<FString, float> WaveSeconds;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Music;

	/** The previous track while it fades out. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Fading;

	float AppliedGain = -1.0f;

	/** The lobby's players seen since it was reached (by id), for the join sound. */
	TSet<int32> SeenPlayers;
	bool bLobbySeeded = false;
};
