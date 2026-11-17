#include "Online/ScctContentDownloads.h"
#include "Online/ScctContentCatalogue.h"

#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	FString ContentDownloadText(const FScctContentApi& Api, EScctContentText Kind, const FString& Key)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(Api.GetText(static_cast<int32>(Kind), *Key, nullptr, 0) + 1);
		Api.GetText(static_cast<int32>(Kind), *Key, Buffer.GetData(), Buffer.Num());
		return FString(Buffer.GetData());
	}

	/** What the watcher of Start() keeps between its ticks. */
	struct FContentDownloadWatch
	{
		TWeakObjectPtr<UScctOnlineSubsystem> Online;
		FTSTicker::FDelegateHandle Ticker;
		bool bAutoDownload = false;
		FString AutoTriedMap;
		int32 SeenGeneration = -1;
		FString SeenLine;
		int32 SeenTenth = -1;
		FString WatchMap;
		int32 SeenMapState = -1;
	};

	FContentDownloadWatch& ContentWatch()
	{
		static FContentDownloadWatch Watch;
		return Watch;
	}

	bool TickContentWatch(float)
	{
		FContentDownloadWatch& Watch = ContentWatch();
		const FScctContentApi* Api = ScctFeature<FScctContentApi>();
		UScctOnlineSubsystem* Visit = Watch.Online.Get();
		if (Api == nullptr || Visit == nullptr)
		{
			return true;
		}

		// A map that came in: logged, and handed to the hook.
		const int32 Generation = Api->GetGeneration();
		if (Watch.SeenGeneration >= 0 && Generation != Watch.SeenGeneration)
		{
			const FString Map = Visit->GetSetupMap();
			const FString Folder = Map.IsEmpty() ? FString() : ContentDownloadText(*Api, EScctContentText::InstalledFolder, Map);
			UE_LOG(LogOpenSCCT, Log, TEXT("Content: a map or pack was installed or removed (generation %d)%s"), Generation,
				Folder.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s in %s; it plays through %s"), *Map, *Folder, *UScctOnlineSubsystem::LevelOf(Map)));
			if (!Folder.IsEmpty())
			{
				ScctContentDownloads::OnMapInstalled().Broadcast(Map, Folder);
				Watch.WatchMap = Map;
				Watch.SeenMapState = -1;
			}
		}
		Watch.SeenGeneration = Generation;

		if (Visit->GetState() != EScctOnlineState::Lobby && Visit->GetState() != EScctOnlineState::Results)
		{
			return true;
		}

		// The lobby's line: its progress logged as it moves on (every tenth), and -ScctAutoDownload's press.
		FScctLobbyMapFetch Fetch;
		if (ScctContentDownloads::GetLobbyFetch(Fetch) && Fetch.IsShown())
		{
			const int32 Tenth = FMath::FloorToInt32(Fetch.Fraction * 10.0f);
			const FString Step = Fetch.Line.Left(Fetch.Line.Find(TEXT(":")));
			if (Fetch.Line != Watch.SeenLine && (Tenth != Watch.SeenTenth || !Watch.SeenLine.StartsWith(Step) || !Fetch.IsRunning()))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Content: lobby map line: %s%s"), *Fetch.Line,
					Fetch.CanDownload() ? TEXT(" [Download]") : Fetch.IsRunning() ? TEXT(" [Stop]") : TEXT(""));
				Watch.SeenTenth = Tenth;
				Watch.SeenLine = Fetch.Line;
			}
			const FString Map = Visit->GetSetupMap();
			if (Watch.bAutoDownload && Fetch.CanDownload() && !Fetch.IsProblem() && Watch.AutoTriedMap != Map)
			{
				Watch.AutoTriedMap = Map;
				const bool bStarted = ScctContentDownloads::StartLobbyFetch();
				UE_LOG(LogOpenSCCT, Log, TEXT("Content: -ScctAutoDownload presses Download for %s: %s"), *Map, bStarted ? TEXT("started") : TEXT("nothing to download"));
				Watch.WatchMap = Map;
				Watch.SeenMapState = -1;
			}
		}

		// After a download, the map state the server is told for this player.
		if (!Watch.WatchMap.IsEmpty() && Watch.WatchMap.Equals(Visit->GetSetupMap(), ESearchCase::IgnoreCase))
		{
			if (const FScctOnlinePlayer* Local = Visit->FindLocalPlayer())
			{
				if (Local->MapState != Watch.SeenMapState)
				{
					Watch.SeenMapState = Local->MapState;
					UE_LOG(LogOpenSCCT, Log, TEXT("Content: %s MapState=%d (%s)"), *Watch.WatchMap, Local->MapState,
						Local->MapState == 1 ? TEXT("has the host's map") : Local->MapState == 2 ? TEXT("missing") : Local->MapState == 3 ? TEXT("another version") : TEXT("checking"));
				}
			}
		}
		return true;
	}
}

bool FScctLobbyMapFetch::IsShown() const { return (Flags & EScctContentLobbyFlags::Shown) != 0; }
bool FScctLobbyMapFetch::CanDownload() const { return (Flags & EScctContentLobbyFlags::CanDownload) != 0; }
bool FScctLobbyMapFetch::IsRunning() const { return (Flags & EScctContentLobbyFlags::Running) != 0; }
bool FScctLobbyMapFetch::IsProblem() const { return (Flags & EScctContentLobbyFlags::Problem) != 0; }

bool ScctContentDownloads::GetLobbyFetch(FScctLobbyMapFetch& Out)
{
	Out = FScctLobbyMapFetch();
	const FScctContentApi* Api = ScctFeature<FScctContentApi>();
	if (Api == nullptr)
	{
		return false;
	}
	FScctContentDownload Info = {};
	Out.Flags = Api->GetLobbyFetch(&Info);
	Out.State = Info.State;
	Out.Fraction = Info.Fraction;
	if (Out.IsShown())
	{
		Out.Line = ContentDownloadText(*Api, EScctContentText::LobbyLine, FString());
	}
	return true;
}

bool ScctContentDownloads::StartLobbyFetch()
{
	const FScctContentApi* Api = ScctFeature<FScctContentApi>();
	const bool bStarted = Api != nullptr && Api->StartLobbyFetch() == 1;
	UE_LOG(LogOpenSCCT, Log, TEXT("Content: lobby Download %s"), bStarted ? TEXT("started") : TEXT("had nothing to download"));
	return bStarted;
}

void ScctContentDownloads::CancelLobbyFetch()
{
	if (const FScctContentApi* Api = ScctFeature<FScctContentApi>())
	{
		Api->CancelLobbyFetch();
		UE_LOG(LogOpenSCCT, Log, TEXT("Content: lobby download stopped"));
	}
}

int32 ScctContentDownloads::Generation()
{
	const FScctContentApi* Api = ScctFeature<FScctContentApi>();
	return Api != nullptr ? Api->GetGeneration() : -1;
}

int32 ScctContentDownloads::CheckBotNavigation(const FString& Map, FString& OutText)
{
	OutText.Reset();
	const FScctContentApi* Api = ScctFeature<FScctContentApi>();
	if (Api == nullptr || Map.IsEmpty())
	{
		return static_cast<int32>(EScctBotNavigationState::None);
	}
	const int32 State = Api->CheckBotNavigation(*Map);
	OutText = ContentDownloadText(*Api, EScctContentText::BotNavigation, Map);
	return State;
}

bool ScctContentDownloads::StartBotNavigation(const FString& Map)
{
	const FScctContentApi* Api = ScctFeature<FScctContentApi>();
	return Api != nullptr && Api->StartBotNavigation(*Map) == 1;
}

int32 ScctContentDownloads::GetDownload(const FString& Key, FString& OutText, float& OutFraction)
{
	OutText.Reset();
	OutFraction = 0.0f;
	const FScctContentApi* Api = ScctFeature<FScctContentApi>();
	if (Api == nullptr)
	{
		return static_cast<int32>(EScctContentDownloadState::None);
	}
	FScctContentDownload Info = {};
	const int32 State = Api->GetDownload(*Key, &Info);
	OutFraction = Info.Fraction;
	OutText = ContentDownloadText(*Api, EScctContentText::Download, Key);
	if (State == static_cast<int32>(EScctContentDownloadState::Done))
	{
		const FString Folder = ContentDownloadText(*Api, EScctContentText::InstalledFolder, Key);
		if (!Folder.IsEmpty())
		{
			OutText += TEXT(" (") + Folder + TEXT(")");
		}
	}
	return State;
}

void ScctContentDownloads::CancelDownload(const FString& Key)
{
	if (const FScctContentApi* Api = ScctFeature<FScctContentApi>())
	{
		Api->CancelDownload(*Key);
	}
}

void ScctContentDownloads::Start(UScctOnlineSubsystem* Online)
{
	FContentDownloadWatch& Watch = ContentWatch();
	Watch.Online = Online;
	Watch.bAutoDownload = FParse::Param(FCommandLine::Get(), TEXT("ScctAutoDownload"));
	if (!Watch.Ticker.IsValid())
	{
		Watch.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickContentWatch), 0.25f);
		// The Maps & Content page's unattended switches (-ScctContentDownload, -ScctContentRemove).
		ScctContentCatalogue::StartUnattended(Online);
	}
}

ScctContentDownloads::FOnMapInstalled& ScctContentDownloads::OnMapInstalled()
{
	static FOnMapInstalled Delegate;
	return Delegate;
}
