#include "Online/ScctContentCatalogue.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctContentDownloads.h"
#include "Online/ScctHostSetup.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	FString CatalogueText(const FScctContentApi& Api, EScctContentText Kind)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(Api.GetText(static_cast<int32>(Kind), TEXT(""), nullptr, 0) + 1);
		Api.GetText(static_cast<int32>(Kind), TEXT(""), Buffer.GetData(), Buffer.Num());
		return FString(Buffer.GetData());
	}

	/** The version 2 table (the page's calls), or null with an older bridge. */
	const FScctContentApi* CatalogueApi()
	{
		const FScctContentApi* Api = ScctFeature<FScctContentApi>();
		return Api != nullptr && Api->Version >= 2 && Api->GetCatalogue != nullptr ? Api : nullptr;
	}

	/** One map of an unattended download, followed to its end. */
	struct FContentUnattendedJob
	{
		FString Map;
		bool bStarted = false;
		bool bFinished = false;
		int32 SeenTenth = -1;
		FString SeenLine;
		double CheckPlayableAt = 0.0;
	};

	struct FContentUnattended
	{
		TWeakObjectPtr<UScctOnlineSubsystem> Online;
		FTSTicker::FDelegateHandle Ticker;
		TArray<FString> Removals;
		TArray<FContentUnattendedJob> Downloads;
		bool bIndexFailedLogged = false;
	};

	FContentUnattended& ContentUnattended()
	{
		static FContentUnattended State;
		return State;
	}

	TArray<FString> MapList(const TCHAR* Switch)
	{
		FString Value;
		TArray<FString> Maps;
		if (FParse::Value(FCommandLine::Get(), Switch, Value, false))
		{
			Value.TrimQuotesInline();
			Value.ParseIntoArray(Maps, TEXT(","), true);
			for (FString& Map : Maps)
			{
				Map.TrimStartAndEndInline();
			}
		}
		return Maps;
	}

	/** The catalogue's state of a map, as the log reports it after a removal or a download. */
	FString DescribeCatalogueState(const FString& Map)
	{
		TArray<FScctContentRow> Rows;
		ScctContentCatalogue::GetRows(Rows);
		const FScctContentRow* Row = Rows.FindByPredicate([&Map](const FScctContentRow& R) { return !R.bShared && R.Key.Equals(Map, ESearchCase::IgnoreCase); });
		if (Row == nullptr)
		{
			return TEXT("not in the catalogue (not installed)");
		}
		const TCHAR* Shown = Row->State == TEXT("downloaded") ? TEXT("installed (downloaded)")
			: Row->State == TEXT("update") ? TEXT("installed, update available")
			: Row->State == TEXT("ingame") ? TEXT("installed (in the converted game files)")
			: Row->State == TEXT("unlisted") ? TEXT("installed (no longer listed)")
			: TEXT("not installed");
		return FString::Printf(TEXT("%s \"%s\" %s"), *Row->Key, *Row->Title, Shown);
	}

	bool InCreateSession(const FString& Map)
	{
		return ScctHostSetup::Maps().ContainsByPredicate([&Map](const FScctHostMap& M) { return M.Id.Equals(Map, ESearchCase::IgnoreCase); });
	}

	bool TickContentUnattended(float)
	{
		FContentUnattended& State = ContentUnattended();
		const FScctContentApi* Api = CatalogueApi();
		if (Api == nullptr)
		{
			return true;
		}

		// Removals need no index: the downloaded maps are known from the packs folder.
		for (const FString& Map : State.Removals)
		{
			FString Error;
			const int32 Result = ScctContentCatalogue::Remove(Map, Error);
			ScctContentCatalogue::RefreshMenuMaps();
			UE_LOG(LogOpenSCCT, Log, TEXT("Content: -ScctContentRemove %s: %s%s; the catalogue lists %s; playable here: %s"), *Map,
				Result == 1 ? TEXT("removed") : Result == 0 ? TEXT("nothing to remove") : Result == -1 ? TEXT("not removed") : TEXT("failed"),
				Error.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Error), *DescribeCatalogueState(Map),
				UScctOnlineSubsystem::IsPlayable(Map) ? TEXT("yes") : TEXT("no"));
		}
		State.Removals.Reset();

		bool bAllDone = true;
		for (FContentUnattendedJob& Job : State.Downloads)
		{
			if (Job.bFinished)
			{
				continue;
			}
			bAllDone = false;
			if (!Job.bStarted)
			{
				const int32 Index = ScctContentCatalogue::RefreshIndex();
				if (Index == 2)
				{
					if (!State.bIndexFailedLogged)
					{
						State.bIndexFailedLogged = true;
						UE_LOG(LogOpenSCCT, Warning, TEXT("Content: -ScctContentDownload cannot start: %s"), *ScctContentCatalogue::GetSummary());
					}
					continue;
				}
				if (Index != 1)
				{
					continue;
				}
				const int32 Started = Api->StartMapDownload(*Job.Map, nullptr);
				UE_LOG(LogOpenSCCT, Log, TEXT("Content: -ScctContentDownload %s: %s"), *Job.Map,
					Started == 1 ? TEXT("started") : Started == 0 ? TEXT("OpenSCCT-Content has no such map") : TEXT("could not start"));
				Job.bStarted = true;
				Job.bFinished = Started != 1;
				continue;
			}

			if (Job.CheckPlayableAt > 0.0)
			{
				// A second after the install the catalogs have been read again (the bridge's next update).
				if (FPlatformTime::Seconds() >= Job.CheckPlayableAt)
				{
					ScctContentCatalogue::RefreshMenuMaps();
					UE_LOG(LogOpenSCCT, Log, TEXT("Content: after the download the catalogue lists %s; playable here: %s (%s); offered by Create Session: %s"),
						*DescribeCatalogueState(Job.Map), UScctOnlineSubsystem::IsPlayable(Job.Map) ? TEXT("yes") : TEXT("no"),
						*UScctOnlineSubsystem::LevelOf(Job.Map), InCreateSession(Job.Map) ? TEXT("yes") : TEXT("no"));
					Job.bFinished = true;
				}
				continue;
			}

			FString Line;
			float Fraction = 0.0f;
			const int32 JobState = ScctContentDownloads::GetDownload(Job.Map, Line, Fraction);
			const int32 Tenth = FMath::FloorToInt32(Fraction * 10.0f);
			if (Line != Job.SeenLine && (Tenth != Job.SeenTenth || JobState != static_cast<int32>(EScctContentDownloadState::Running) || !Line.StartsWith(Job.SeenLine.Left(8))))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Content: job %s: %s"), *Job.Map, *Line);
				Job.SeenTenth = Tenth;
				Job.SeenLine = Line;
			}
			if (JobState == static_cast<int32>(EScctContentDownloadState::Done))
			{
				Job.CheckPlayableAt = FPlatformTime::Seconds() + 1.0;
			}
			else if (JobState == static_cast<int32>(EScctContentDownloadState::Failed) || JobState == static_cast<int32>(EScctContentDownloadState::Stopped))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Content: the download of %s did not finish: %s"), *Job.Map, *Line);
				Job.bFinished = true;
			}
		}

		if (bAllDone)
		{
			State.Ticker.Reset();
			return false;
		}
		return true;
	}
}

int32 ScctContentCatalogue::RefreshIndex(bool bForce)
{
	const FScctContentApi* Api = ScctFeature<FScctContentApi>();
	return Api != nullptr ? Api->RefreshIndex(bForce ? 1 : 0) : -1;
}

bool ScctContentCatalogue::GetRows(TArray<FScctContentRow>& Out)
{
	Out.Reset();
	const FScctContentApi* Api = CatalogueApi();
	if (Api == nullptr)
	{
		return false;
	}
	TArray<TCHAR> Buffer;
	Buffer.SetNumZeroed(Api->GetCatalogue(nullptr, 0) + 1);
	Api->GetCatalogue(Buffer.GetData(), Buffer.Num());
	TArray<FString> Lines;
	FString(Buffer.GetData()).ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Fields;
		Line.ParseIntoArray(Fields, TEXT("\t"), false);
		if (Fields.Num() < 13)
		{
			continue;
		}
		FScctContentRow& Row = Out.AddDefaulted_GetRef();
		Row.bShared = Fields[0] == TEXT("shared");
		Row.Key = Fields[1];
		Row.Title = Fields[2];
		Row.Category = Fields[3];
		LexFromString(Row.Size, *Fields[4]);
		Row.State = Fields[5];
		Row.Version = Fields[6];
		Row.Job = Fields[7];
		Row.JobLine = Fields[8];
		LexFromString(Row.Fraction, *Fields[9]);
		Row.Picture = Fields[10];
		Fields[11].ParseIntoArray(Row.Modes, TEXT(","), true);
		Row.Detail = Fields[12].Replace(TEXT("|"), TEXT("\n"));
	}
	return true;
}

FString ScctContentCatalogue::GetLibrary()
{
	const FScctContentApi* Api = CatalogueApi();
	return Api != nullptr ? CatalogueText(*Api, EScctContentText::Library) : FString(TEXT("The bridge has no content table: start the game with a converted content folder."));
}

FString ScctContentCatalogue::GetSummary()
{
	const FScctContentApi* Api = CatalogueApi();
	return Api != nullptr ? CatalogueText(*Api, EScctContentText::Summary) : FString();
}

FString ScctContentCatalogue::GetPacksFolder()
{
	const FScctContentApi* Api = CatalogueApi();
	return Api != nullptr ? CatalogueText(*Api, EScctContentText::PacksFolder) : FString();
}

int32 ScctContentCatalogue::Download(const FScctContentRow& Row)
{
	const FScctContentApi* Api = CatalogueApi();
	if (Api == nullptr)
	{
		return -2;
	}
	const int32 Result = Row.bShared ? Api->StartSharedDownload(*Row.Key) : Api->StartMapDownload(*Row.Key, nullptr);
	UE_LOG(LogOpenSCCT, Log, TEXT("Content: download of %s %s: %d"), Row.bShared ? TEXT("shared pack") : TEXT("map"), *Row.Key, Result);
	return Result;
}

void ScctContentCatalogue::Stop(const FScctContentRow& Row)
{
	ScctContentDownloads::CancelDownload(Row.JobKey());
	UE_LOG(LogOpenSCCT, Log, TEXT("Content: download of %s stopped"), *Row.Key);
}

int32 ScctContentCatalogue::Remove(const FString& Map, FString& OutError)
{
	OutError.Reset();
	const FScctContentApi* Api = CatalogueApi();
	if (Api == nullptr)
	{
		OutError = TEXT("the bridge has no content table");
		return -2;
	}
	TCHAR Error[512] = {};
	const int32 Result = Api->RemoveMap(*Map, Error, UE_ARRAY_COUNT(Error));
	OutError = Error;
	UE_LOG(LogOpenSCCT, Log, TEXT("Content: remove %s: %d%s"), *Map, Result, OutError.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *OutError));
	return Result;
}

bool ScctContentCatalogue::OpenFolder(FString& OutError)
{
	const FString Folder = GetPacksFolder();
	if (Folder.IsEmpty())
	{
		OutError = TEXT("the downloads folder is not known");
		return false;
	}
	if (!IFileManager::Get().DirectoryExists(*Folder) && !IFileManager::Get().MakeDirectory(*Folder, true))
	{
		OutError = FString::Printf(TEXT("%s could not be made"), *Folder);
		return false;
	}
	FPlatformProcess::ExploreFolder(*Folder);
	UE_LOG(LogOpenSCCT, Log, TEXT("Content: opened %s"), *Folder);
	return true;
}

void ScctContentCatalogue::RefreshMenuMaps()
{
	if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent())
	{
		Menu->RefreshMaps();
	}
}

void ScctContentCatalogue::StartUnattended(UScctOnlineSubsystem* Online)
{
	FContentUnattended& State = ContentUnattended();
	State.Online = Online;
	State.Removals.Append(MapList(TEXT("ScctContentRemove=")));
	for (const FString& Map : MapList(TEXT("ScctContentDownload=")))
	{
		State.Downloads.AddDefaulted_GetRef().Map = Map;
	}
	if ((State.Removals.Num() > 0 || State.Downloads.Num() > 0) && !State.Ticker.IsValid())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Content: unattended: remove [%s], download [%s]"), *FString::Join(State.Removals, TEXT(", ")),
			*FString::JoinBy(State.Downloads, TEXT(", "), [](const FContentUnattendedJob& Job) { return Job.Map; }));
		ScctContentCatalogue::RefreshIndex();
		State.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickContentUnattended), 0.25f);
	}
}
