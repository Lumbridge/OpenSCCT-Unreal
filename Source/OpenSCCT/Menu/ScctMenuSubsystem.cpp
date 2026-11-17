#include "Menu/ScctMenuSubsystem.h"

#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctSoundWave.h"
#include "Runtime/ScctFeatures.h"

TWeakObjectPtr<UScctMenuSubsystem> UScctMenuSubsystem::Current;

namespace
{
	const TCHAR* const ModeKeys[] = { TEXT("Story"), TEXT("DiskHunt"), TEXT("Deathmatch"), TEXT("MercDeathMatch") };
	const TCHAR* const ModeNames[] = { TEXT("Story"), TEXT("Disk Hunt"), TEXT("Deathmatch"), TEXT("Merc Deathmatch") };
	const int32 ModeIds[] = { 5, 4, 6, 7 };

	// The content's own interface sounds (shared/audio/wav/Interface/Menu).
	const TCHAR* const SoundFiles[] = {
		TEXT("Menu_navig_1.wav"), TEXT("Menu_valide_1.wav"), TEXT("Menu_bipIcone.wav"), TEXT("Menu_OpenPage.wav"), TEXT("bip_message.wav"),
	};
	const float SoundLevels[] = { 0.55f, 0.8f, 0.6f, 0.6f, 0.7f };
	constexpr int32 MusicSlot = 100;

	template <typename TRead>
	FString ReadMenuSubsystemText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(1024);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	FTSTicker::FDelegateHandle MusicTicker;
}

UScctMenuSubsystem* UScctMenuSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctMenuSubsystem>() : nullptr;
}

void UScctMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Current = this;

	// The saved settings are in force from the first frame of the run.
	UScctUserSettings* Settings = UScctUserSettings::Get();
	Settings->ApplyAll();
	Settings->LogValues(TEXT("loaded"));

	MusicTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		if (bMusicWanted)
		{
			SetMusicPlaying(true);
		}
		return true;
	}), 0.5f);
}

void UScctMenuSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(MusicTicker);
	SoundCache.Reset();
	Brushes.Reset();
	Super::Deinitialize();
}

const FScctMenuApi* UScctMenuSubsystem::Connect()
{
	if (Api == nullptr)
	{
		// The table is only there once the bridge has its content folder.
		FString Error;
		if (FOpenSCCTModule::Get().GetBridge(Error) != nullptr)
		{
			Api = ScctFeature<FScctMenuApi>();
		}
		if (Api != nullptr)
		{
			SharedRoot = ReadMenuSubsystemText([this](TCHAR* Buffer, int32 Capacity) { return Api->GetText(static_cast<int32>(EScctMenuText::SharedRoot), Buffer, Capacity); });
		}
	}
	return Api;
}

const TCHAR* UScctMenuSubsystem::ModeKey(int32 ModeIndex)
{
	return ModeKeys[FMath::Clamp(ModeIndex, 0, 3)];
}

int32 UScctMenuSubsystem::ModeId(int32 ModeIndex)
{
	return ModeIds[FMath::Clamp(ModeIndex, 0, 3)];
}

FString UScctMenuSubsystem::GetModeName(int32 ModeIndex)
{
	return ModeNames[FMath::Clamp(ModeIndex, 0, 3)];
}

void UScctMenuSubsystem::LoadMaps()
{
	if (bMapsLoaded)
	{
		return;
	}
	const FScctMenuApi* Menu = Connect();
	const TArray<FString> Imported = UScctOnlineSubsystem::FindImportedMaps();
	if (Menu == nullptr)
	{
		// Without the bridge the imported levels are still listed by their folder names; nothing is cached, so the
		// catalog is read as soon as the bridge is there.
		PlayableMaps.Reset();
		for (const FString& Id : Imported)
		{
			FScctMenuMapInfo& Map = PlayableMaps.AddDefaulted_GetRef();
			Map.Id = Id;
			Map.Title = Id.ToUpper();
			Map.bImported = true;
			Map.Data.Modes[0].Playable = 1;
		}
		return;
	}

	bMapsLoaded = true;
	TArray<FScctMenuMap> Raw;
	Raw.SetNumZeroed(FMath::Max(Menu->GetMaps(nullptr, 0), 0));
	if (Raw.Num() > 0)
	{
		Raw.SetNum(FMath::Min(Menu->GetMaps(Raw.GetData(), Raw.Num()), Raw.Num()));
	}
	AllMaps.Reset();
	PlayableMaps.Reset();
	for (const FScctMenuMap& Entry : Raw)
	{
		FScctMenuMapInfo& Map = AllMaps.AddDefaulted_GetRef();
		Map.Id = Entry.Id;
		Map.Title = Entry.Title;
		Map.Data = Entry;
		Map.bImported = Imported.ContainsByPredicate([&Map](const FString& Id) { return Id.Equals(Map.Id, ESearchCase::IgnoreCase); });
		if (Map.bImported && (Entry.Flags & EScctMenuMapFlags::CanPlay) != 0)
		{
			PlayableMaps.Add(Map);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: %d maps in the content's host list, %d imported levels, %d playable here"), AllMaps.Num(), Imported.Num(), PlayableMaps.Num());
}

const TArray<FScctMenuMapInfo>& UScctMenuSubsystem::GetMaps()
{
	LoadMaps();
	return PlayableMaps;
}

const FScctMenuMapInfo* UScctMenuSubsystem::FindMap(const FString& Id)
{
	LoadMaps();
	const auto Same = [&Id](const FScctMenuMapInfo& Map) { return Map.Id.Equals(Id, ESearchCase::IgnoreCase); };
	const FScctMenuMapInfo* Found = AllMaps.FindByPredicate(Same);
	return Found != nullptr ? Found : PlayableMaps.FindByPredicate(Same);
}

FString UScctMenuSubsystem::GetMapTitle(const FString& Id)
{
	const FScctMenuMapInfo* Map = FindMap(Id);
	return Map != nullptr && !Map->Title.IsEmpty() ? Map->Title : Id.ToUpper();
}

FString UScctMenuSubsystem::GetMapText(const FString& Id, EScctMenuMapText Kind)
{
	const FScctMenuApi* Menu = Connect();
	return Menu == nullptr ? FString() : ReadMenuSubsystemText([&](TCHAR* Buffer, int32 Capacity) { return Menu->GetMapText(*Id, static_cast<int32>(Kind), Buffer, Capacity); });
}

FString UScctMenuSubsystem::GetModeText(const FString& Id, int32 ModeIndex, bool bMerc, int32 Kind)
{
	const FScctMenuApi* Menu = Connect();
	return Menu == nullptr ? FString() : ReadMenuSubsystemText([&](TCHAR* Buffer, int32 Capacity) { return Menu->GetModeText(*Id, ModeId(ModeIndex), bMerc ? 1 : 0, Kind, Buffer, Capacity); });
}

FString UScctMenuSubsystem::Text(const TCHAR* InSection, const TCHAR* Key, const TCHAR* Fallback)
{
	const FScctMenuApi* Menu = Connect();
	if (Menu == nullptr)
	{
		return Fallback;
	}
	const FString Name = FString(InSection) + TEXT('/') + Key;
	if (const FString* Known = Strings.Find(Name))
	{
		return Known->IsEmpty() ? FString(Fallback) : *Known;
	}
	FString Found = ReadMenuSubsystemText([&](TCHAR* Buffer, int32 Capacity) { return Menu->Localize(TEXT("GUI"), InSection, Key, Buffer, Capacity); });
	// The original breaks its lines with '|'; the pages wrap their own.
	Found.ReplaceInline(TEXT("||"), TEXT(" "));
	Found.ReplaceInline(TEXT("|"), TEXT(" "));
	Found.TrimStartAndEndInline();
	Strings.Add(Name, Found);
	return Found.IsEmpty() ? FString(Fallback) : Found;
}

FString UScctMenuSubsystem::GetGameVersion()
{
	const FScctMenuApi* Menu = Connect();
	return Menu == nullptr ? FString() : ReadMenuSubsystemText([&](TCHAR* Buffer, int32 Capacity) { return Menu->GetText(static_cast<int32>(EScctMenuText::GameVersion), Buffer, Capacity); });
}

const TArray<FScctMenuAction>& UScctMenuSubsystem::GetActions()
{
	if (Actions.IsEmpty())
	{
		if (const FScctMenuApi* Menu = Connect())
		{
			Actions.SetNumZeroed(FMath::Max(Menu->GetActions(nullptr, 0), 0));
			if (Actions.Num() > 0)
			{
				Actions.SetNum(FMath::Min(Menu->GetActions(Actions.GetData(), Actions.Num()), Actions.Num()));
			}
		}
	}
	return Actions;
}

const FSlateBrush* UScctMenuSubsystem::GetPicture(const FString& File)
{
	if (File.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<FSlateBrush>* Known = Brushes.Find(File))
	{
		return Known->Get();
	}

	TSharedPtr<FSlateBrush> Brush;
	if (IFileManager::Get().FileExists(*File))
	{
		if (UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(File))
		{
			Texture->SRGB = true;
			Texture->UpdateResource();
			Textures.Add(File, Texture);
			Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Texture);
			Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush->DrawAs = ESlateBrushDrawType::Image;
		}
	}
	Brushes.Add(File, Brush);
	return Brush.Get();
}

const FSlateBrush* UScctMenuSubsystem::GetMapPicture(const FString& Id, bool bMerc)
{
	const EScctMenuMapText Order[] = {
		EScctMenuMapText::BannerImage,
		bMerc ? EScctMenuMapText::LoadingMercImage : EScctMenuMapText::LoadingSpyImage,
		bMerc ? EScctMenuMapText::LoadingSpyImage : EScctMenuMapText::LoadingMercImage,
		EScctMenuMapText::PreviewImage,
	};
	for (const EScctMenuMapText Kind : Order)
	{
		if (const FSlateBrush* Brush = GetPicture(GetMapText(Id, Kind)))
		{
			return Brush;
		}
	}
	return nullptr;
}

UScctSoundWave* UScctMenuSubsystem::FindWave(int32 Slot, const FString& File, bool bLoop)
{
	if (const TObjectPtr<UScctSoundWave>* Known = Waves.Find(Slot))
	{
		return *Known;
	}
	FScctPcmPtr Pcm;
	if (File.IsEmpty() || SoundCache.Request(Slot + 1, File, Pcm) != FScctSoundCache::EState::Ready || !Pcm.IsValid())
	{
		return nullptr;
	}
	UScctSoundWave* Wave = NewObject<UScctSoundWave>(this);
	Wave->Init(Pcm, bLoop, INDEX_NONE, INDEX_NONE);
	Waves.Add(Slot, Wave);
	return Wave;
}

void UScctMenuSubsystem::PlaySound(EScctUiSound Sound)
{
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || World->GetAudioDeviceRaw() == nullptr || Connect() == nullptr || SharedRoot.IsEmpty())
	{
		return;
	}
	// A held arrow key walks a list fast: the tick is not played more than about fifteen times a second.
	const double Now = FPlatformTime::Seconds();
	if (Sound == EScctUiSound::Move)
	{
		if (Now - LastMoveSound < 0.06)
		{
			return;
		}
		LastMoveSound = Now;
	}

	const int32 Slot = static_cast<int32>(Sound);
	const float Gain = UScctUserSettings::Get()->GetGain(EScctSoundKind::MenuSounds) * SoundLevels[Slot];
	if (Gain <= 0.0f)
	{
		return;
	}
	// The first use of a sound only starts its decode; it is heard from the next press on.
	if (UScctSoundWave* Wave = FindWave(Slot, SharedRoot / TEXT("audio/wav/Interface/Menu") / SoundFiles[Slot], false))
	{
		UGameplayStatics::PlaySound2D(World, Wave, Gain);
	}
}

void UScctMenuSubsystem::SetMusicPlaying(bool bPlaying)
{
	bMusicWanted = bPlaying;
	UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	if (!bPlaying)
	{
		if (IsValid(Music))
		{
			Music->FadeOut(0.6f, 0.0f);
		}
		Music = nullptr;
		return;
	}
	if (World == nullptr || World->GetAudioDeviceRaw() == nullptr || Connect() == nullptr)
	{
		return;
	}
	if (IsValid(Music) && Music->GetWorld() == World && Music->IsPlaying())
	{
		return;
	}

	if (MusicFile.IsEmpty())
	{
		// The front end level's own song (the Entry map's), as the original's menus play it.
		const FString ContentRoot = ReadMenuSubsystemText([this](TCHAR* Buffer, int32 Capacity) { return Api->GetText(static_cast<int32>(EScctMenuText::ContentRoot), Buffer, Capacity); });
		const FString Candidates[] = {
			ContentRoot / TEXT("svmap/Entry/audio/streams/Interface/Menu_music1.ogg"),
			ContentRoot / TEXT("svmap/Lobby/audio/streams/Interface/Menu_music1.ogg"),
		};
		for (const FString& Candidate : Candidates)
		{
			if (IFileManager::Get().FileExists(*Candidate))
			{
				MusicFile = Candidate;
				break;
			}
		}
		if (MusicFile.IsEmpty())
		{
			bMusicWanted = false;
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: the content has no menu music (svmap/Entry/audio/streams/Interface/Menu_music1.ogg)"));
			return;
		}
	}

	if (UScctSoundWave* Wave = FindWave(MusicSlot, MusicFile, true))
	{
		Music = UGameplayStatics::CreateSound2D(World, Wave, 1.0f, 1.0f, 0.0f, nullptr, false, false);
		if (IsValid(Music))
		{
			Music->bIsMusic = true;
			Music->bIsUISound = true;
			Music->FadeIn(1.5f, FMath::Max(UScctUserSettings::Get()->GetGain(EScctSoundKind::MenuMusic), 0.0001f));
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: music %s"), *MusicFile);
		}
	}
}

void UScctMenuSubsystem::RefreshVolumes()
{
	if (IsValid(Music))
	{
		Music->SetVolumeMultiplier(FMath::Max(UScctUserSettings::Get()->GetGain(EScctSoundKind::MenuMusic), 0.0001f));
	}
}
