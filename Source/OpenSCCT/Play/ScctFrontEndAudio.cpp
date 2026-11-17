#include "Play/ScctFrontEndAudio.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/ScctFrontEndGameMode.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctAudioMix.h"
#include "Play/ScctSoundWave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

TWeakObjectPtr<UScctFrontEndAudio> UScctFrontEndAudio::Current;

namespace ScctFrontEndAudioLocal
{
	/** PlayMusic's minimum fade (Godot's AudioK.MusicMinFadeOut). */
	constexpr float CrossfadeSeconds = 2.0f;

	const TCHAR* const EntryLevel = TEXT("Entry");
	const TCHAR* const LobbyLevel = TEXT("Lobby");

	FString ShortName(const FString& File)
	{
		return File.IsEmpty() ? FString(TEXT("none")) : FPaths::GetBaseFilename(File);
	}
}

const TCHAR* UScctFrontEndAudio::PageName(EScctFrontEndPage InPage)
{
	switch (InPage)
	{
	case EScctFrontEndPage::Entry: return TEXT("Entry");
	case EScctFrontEndPage::Lobby: return TEXT("Lobby");
	case EScctFrontEndPage::Loading: return TEXT("Loading");
	case EScctFrontEndPage::Results: return TEXT("Results");
	default: return TEXT("None");
	}
}

void UScctFrontEndAudio::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Current = this;

	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("-ScctFrontEndPage="), Forced))
	{
		for (const EScctFrontEndPage Each : { EScctFrontEndPage::Entry, EScctFrontEndPage::Lobby, EScctFrontEndPage::Loading, EScctFrontEndPage::Results })
		{
			if (Forced.Equals(PageName(Each), ESearchCase::IgnoreCase))
			{
				ForcedPage = Each;
				bForced = true;
			}
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: -ScctFrontEndPage=%s %s"), *Forced, bForced ? TEXT("holds the menus' page for the music in the front end") : TEXT("is not Entry, Lobby, Loading or Results: ignored"));
	}

	PreLoadHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UScctFrontEndAudio::HandlePreLoadMap);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UScctFrontEndAudio::HandlePostLoadMap);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctFrontEndAudio::Tick), 0.0f);
}

void UScctFrontEndAudio::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	if (IsValid(Music))
	{
		Music->Stop();
	}
	if (IsValid(Fading))
	{
		Fading->Stop();
	}
	Music = nullptr;
	Fading = nullptr;
	Waves.Reset();
	Cache.Reset();
	Super::Deinitialize();
}

void UScctFrontEndAudio::SetMusicAllowed(bool bAllowed)
{
	bMusicAllowed = bAllowed;
}

float UScctFrontEndAudio::MusicGain() const
{
	return UScctUserSettings::Get()->GetGain(EScctSoundKind::MenuMusic);
}

void UScctFrontEndAudio::RefreshVolume()
{
	AppliedGain = MusicGain();
	if (IsValid(Music) && Music->IsPlaying())
	{
		// A fade in that is still running ends at the new level.
		Music->AdjustVolume(0.1f, FMath::Max(AppliedGain, 0.0001f));
	}
}

const UScctFrontEndAudio::FSong& UScctFrontEndAudio::SongOf(const TCHAR* Level)
{
	FSong& Song = Songs.FindOrAdd(Level);
	if (Song.bRead)
	{
		return Song;
	}
	UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
	const FString ContentRoot = Menu != nullptr ? Menu->GetContentRoot() : FString();
	if (ContentRoot.IsEmpty())
	{
		// Without the bridge there is no content folder yet: asked again next time.
		return Song;
	}
	Song.bRead = true;

	// The level's LevelInfo names its Song; the sound table says which stream that is.
	const FString AudioDir = ContentRoot / TEXT("svmap") / Level / TEXT("audio");
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *(AudioDir / TEXT("audio.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: the content has no audio table for the %s menu level (%s)"), Level, *(AudioDir / TEXT("audio.json")));
		return Song;
	}
	const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
	if (Root->TryGetArrayField(TEXT("actors"), Actors))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Actors)
		{
			const TSharedPtr<FJsonObject>* Actor = nullptr;
			const TSharedPtr<FJsonObject>* ActorSounds = nullptr;
			FString Class;
			if (Value.IsValid() && Value->TryGetObject(Actor) && (*Actor)->TryGetStringField(TEXT("class"), Class) && Class == TEXT("Engine.LevelInfo") &&
				(*Actor)->TryGetObjectField(TEXT("sounds"), ActorSounds) && (*ActorSounds)->TryGetStringField(TEXT("Song"), Song.Sound))
			{
				break;
			}
		}
	}
	const TSharedPtr<FJsonObject>* Table = nullptr;
	const TSharedPtr<FJsonObject>* Entry = nullptr;
	FString File;
	if (!Song.Sound.IsEmpty() && Root->TryGetObjectField(TEXT("sounds"), Table) && (*Table)->TryGetObjectField(Song.Sound, Entry) && (*Entry)->TryGetStringField(TEXT("file"), File))
	{
		const FString Full = AudioDir / File;
		if (IFileManager::Get().FileExists(*Full))
		{
			Song.File = Full;
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: the %s menu level's song is %s (%s)"), Level, Song.Sound.IsEmpty() ? TEXT("none") : *Song.Sound,
		Song.File.IsEmpty() ? TEXT("no stream") : *Song.File);
	return Song;
}

EScctFrontEndPage UScctFrontEndAudio::FindPage(UWorld* World) const
{
	if (bLoadingLevel)
	{
		return EScctFrontEndPage::Loading;
	}
	const bool bFrontEnd = World != nullptr && World->GetAuthGameMode() != nullptr && World->GetAuthGameMode()->IsA<AScctFrontEndGameMode>();
	if (bForced && bFrontEnd)
	{
		return ForcedPage;
	}
	const UScctOnlineSubsystem* Online = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>() : nullptr;
	if (Online != nullptr)
	{
		switch (Online->GetState())
		{
		case EScctOnlineState::Connecting:
		case EScctOnlineState::Travelling:
		case EScctOnlineState::Loading:
			return EScctFrontEndPage::Loading;
		case EScctOnlineState::Lobby:
			return bFrontEnd ? EScctFrontEndPage::Lobby : EScctFrontEndPage::Loading;
		case EScctOnlineState::Results:
			return EScctFrontEndPage::Results;
		case EScctOnlineState::Match:
			return EScctFrontEndPage::None;
		default:
			break; // idle, failed, left: the front end's own pages
		}
	}
	return bFrontEnd ? EScctFrontEndPage::Entry : EScctFrontEndPage::None;
}

bool UScctFrontEndAudio::Tick(float DeltaSeconds)
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const EScctFrontEndPage Wanted = FindPage(World);
	if (Wanted != Page)
	{
		ShowPage(Wanted, World);
	}
	if (!WantedFile.IsEmpty() && bMusicAllowed && (PlayingFile != WantedFile || !IsValid(Music) || Music->GetWorld() != World))
	{
		StartWanted(World);
	}
	else if (!bMusicAllowed && IsValid(Music))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: the menus' music is switched off: %s fades out"), *PlayingName);
		FadeOutCurrent(0.6f);
	}

	if (IsValid(Music) && !FMath::IsNearlyEqual(MusicGain(), AppliedGain, 0.001f))
	{
		RefreshVolume();
	}
	if (IsValid(Fading) && !Fading->IsPlaying())
	{
		Fading = nullptr;
	}
	NoteLobbyJoins();
	return true;
}

void UScctFrontEndAudio::ShowPage(EScctFrontEndPage NewPage, UWorld* World)
{
	using namespace ScctFrontEndAudioLocal;
	Page = NewPage;
	const TCHAR* Level = NewPage == EScctFrontEndPage::Lobby ? LobbyLevel : NewPage == EScctFrontEndPage::Entry ? EntryLevel : nullptr;
	FString File;
	FString Sound;
	if (Level != nullptr)
	{
		const FSong& Song = SongOf(Level);
		File = Song.File;
		Sound = Song.Sound;
		if (File.IsEmpty() && NewPage == EScctFrontEndPage::Lobby)
		{
			// A lobby level without a song of its own (classic content) plays the Entry's.
			const FSong& Entry = SongOf(EntryLevel);
			File = Entry.File;
			Sound = Entry.Sound;
		}
	}

	const bool bDevice = World != nullptr && World->GetAudioDeviceRaw() != nullptr;
	const TCHAR* const Heard = bDevice ? TEXT("") : TEXT(" (no audio device: not heard)");
	if (File.IsEmpty())
	{
		if (IsValid(Music))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: page=%s silent (%s fades out over %.0f s)%s"), PageName(NewPage), *PlayingName, CrossfadeSeconds, Heard);
			FadeOutCurrent(CrossfadeSeconds);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: page=%s silent%s%s"), PageName(NewPage), Level != nullptr ? TEXT(" (the content has no menu song)") : TEXT(""), Heard);
		}
		WantedFile.Reset();
		WantedName.Reset();
		return;
	}

	if (File == WantedFile && File == PlayingFile && IsValid(Music))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: page=%s track=%s plays on (the same song)%s"), PageName(NewPage), *ShortName(File), Heard);
		return;
	}
	WantedFile = File;
	WantedName = ShortName(File);
	bDecodeLogged = false;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: page=%s track=%s %s (%s, the %s level's Song)%s"), PageName(NewPage), *WantedName,
		IsValid(Music) ? *FString::Printf(TEXT("crossfading from %s over %.0f s"), *PlayingName, CrossfadeSeconds) : *FString::Printf(TEXT("fading in over %.0f s"), CrossfadeSeconds),
		*Sound, Level, Heard);
}

void UScctFrontEndAudio::StartWanted(UWorld* World)
{
	using namespace ScctFrontEndAudioLocal;
	if (World == nullptr || World->GetAudioDeviceRaw() == nullptr)
	{
		return;
	}

	UScctSoundWave* Wave = nullptr;
	if (const TObjectPtr<UScctSoundWave>* Known = Waves.Find(WantedFile))
	{
		Wave = *Known;
	}
	else
	{
		// The stream is decoded on a worker; it starts on the first frame it is ready.
		const int32 FileId = FileIds.FindOrAdd(WantedFile, FileIds.Num() + 1);
		FScctPcmPtr Pcm;
		const FScctSoundCache::EState State = Cache.Request(FileId, WantedFile, Pcm);
		if (State == FScctSoundCache::EState::Failed)
		{
			if (!bDecodeLogged)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctFrontEndAudio: %s could not be decoded: the menus stay silent"), *WantedFile);
				bDecodeLogged = true;
			}
			WantedFile.Reset();
			return;
		}
		if (State != FScctSoundCache::EState::Ready || !Pcm.IsValid())
		{
			return;
		}
		Wave = NewObject<UScctSoundWave>(this);
		Wave->Init(Pcm, true, INDEX_NONE, INDEX_NONE);
		Waves.Add(WantedFile, Wave);
		WaveSeconds.Add(WantedFile, Pcm->Seconds());
	}

	// The old track fades out where it is; the new one fades in over the same time.
	if (IsValid(Music) && Music->GetWorld() == World)
	{
		FadeOutCurrent(CrossfadeSeconds);
	}
	Music = UGameplayStatics::CreateSound2D(World, Wave, 1.0f, 1.0f, 0.0f, nullptr, false, false);
	if (!IsValid(Music))
	{
		return;
	}
	const UScctAudioMix* Mix = GetGameInstance()->GetSubsystem<UScctAudioMix>();
	Music->SoundClassOverride = Mix != nullptr ? Mix->GetClass(EScctAudioCategory::MenuMusic) : nullptr;
	Music->bIsMusic = true;
	Music->bIsUISound = true;
	AppliedGain = MusicGain();
	Music->FadeIn(CrossfadeSeconds, FMath::Max(AppliedGain, 0.0001f));
	PlayingFile = WantedFile;
	PlayingName = WantedName;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: playing %s (%.1f s loop) at menu music volume %.2f on the MenuMusic class%s"), *PlayingName, WaveSeconds.FindRef(PlayingFile), AppliedGain,
		IsValid(Fading) && Fading->IsPlaying() ? *FString::Printf(TEXT(", over the fading %s"), *FadingName) : TEXT(""));
}

void UScctFrontEndAudio::FadeOutCurrent(float Seconds)
{
	if (IsValid(Fading))
	{
		Fading->Stop();
	}
	Fading = nullptr;
	if (IsValid(Music))
	{
		if (Music->IsPlaying())
		{
			Music->FadeOut(Seconds, 0.0f);
			Fading = Music;
			FadingName = PlayingName;
		}
	}
	Music = nullptr;
	PlayingFile.Reset();
}

void UScctFrontEndAudio::HandlePreLoadMap(const FString& MapName)
{
	// The load blocks the frames a fade needs: without this the menu song plays on through the load.
	bLoadingLevel = true;
	if (IsValid(Music) || IsValid(Fading))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: page=Loading silent (loading %s: %s stopped)"), *MapName, *PlayingName);
	}
	if (IsValid(Music))
	{
		Music->Stop();
	}
	if (IsValid(Fading))
	{
		Fading->Stop();
	}
	Music = nullptr;
	Fading = nullptr;
	PlayingFile.Reset();
	WantedFile.Reset();
	Page = EScctFrontEndPage::Loading;
}

void UScctFrontEndAudio::HandlePostLoadMap(UWorld* World)
{
	bLoadingLevel = false;
}

void UScctFrontEndAudio::NoteLobbyJoins()
{
	const UScctOnlineSubsystem* Online = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>() : nullptr;
	if (Online == nullptr || Online->GetState() != EScctOnlineState::Lobby)
	{
		SeenPlayers.Reset();
		bLobbySeeded = false;
		return;
	}
	const int32 LocalId = Online->GetStatus().LocalId;
	if (LocalId < 0)
	{
		return;
	}
	// Who is there when the lobby is reached makes no sound; a person who comes in after that does.
	if (!bLobbySeeded)
	{
		if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent())
		{
			Menu->PrepareSound(EScctUiSound::Notice);
		}
	}
	FString Joined;
	for (const FScctOnlinePlayer& Player : Online->GetPlayers())
	{
		bool bAlreadySeen = false;
		SeenPlayers.Add(Player.Id, &bAlreadySeen);
		if (!bAlreadySeen && bLobbySeeded && Player.Id != LocalId && (Player.Flags & EScctOnlinePlayerFlags::Bot) == 0)
		{
			Joined = Player.Name;
		}
	}
	bLobbySeeded = true;
	if (!Joined.IsEmpty())
	{
		PlayJoin(Joined);
	}
}

void UScctFrontEndAudio::PlayJoin(const FString& Who)
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	const bool bDevice = World != nullptr && World->GetAudioDeviceRaw() != nullptr;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctFrontEndAudio: %s joined the lobby: bip_message at menu sounds volume %.2f%s"), *Who,
		UScctUserSettings::Get()->GetGain(EScctSoundKind::MenuSounds), bDevice ? TEXT("") : TEXT(" (no audio device: not heard)"));
	if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent())
	{
		Menu->PlaySound(EScctUiSound::Notice);
	}
}
