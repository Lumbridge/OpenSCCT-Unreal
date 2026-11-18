#include "Social/ScctDashboardSubsystem.h"

#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Social/ScctSocialSubsystem.h"

TWeakObjectPtr<UScctDashboardSubsystem> UScctDashboardSubsystem::Current;

namespace
{
	/** Seconds without a paint of a dashboard page before the watch ends. */
	constexpr double ScctDashboardHideSeconds = 0.5;

	constexpr int32 ScctDashboardTextCount = static_cast<int32>(EScctDashboardText::AnnouncementStatus) + 1;

	/** A text of any length from a call that returns its full length. */
	template <typename TRead>
	FString ScctDashboardReadText(TRead&& Read)
	{
		TCHAR Short[512];
		const int32 Length = Read(Short, UE_ARRAY_COUNT(Short));
		if (Length <= 0)
		{
			return FString();
		}
		if (Length < UE_ARRAY_COUNT(Short))
		{
			return FString(Short);
		}
		TArray<TCHAR> Long;
		Long.SetNumZeroed(Length + 1);
		Read(Long.GetData(), Long.Num());
		return FString(Long.GetData());
	}
}

UScctDashboardSubsystem* UScctDashboardSubsystem::GetCurrent()
{
	return Current.Get();
}

void UScctDashboardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The social subsystem runs the bridge's social Update, which moves the dashboard.
	Collection.InitializeDependency<UScctSocialSubsystem>();
	Super::Initialize(Collection);
	Current = this;
	bPreview = FParse::Param(FCommandLine::Get(), TEXT("ScctDashboardPreview"));
	Texts.SetNum(ScctDashboardTextCount);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctDashboardSubsystem::Tick));
}

void UScctDashboardSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	if (Api != nullptr && bWatched)
	{
		Api->SetWatched(0);
	}
	Api = nullptr;
	if (Current.Get() == this)
	{
		Current.Reset();
	}
	Super::Deinitialize();
}

void UScctDashboardSubsystem::NoteShown()
{
	LastShown = FPlatformTime::Seconds();
}

const FString& UScctDashboardSubsystem::GetText(EScctDashboardText Kind) const
{
	static const FString Empty;
	const int32 Index = static_cast<int32>(Kind);
	return Texts.IsValidIndex(Index) ? Texts[Index] : Empty;
}

bool UScctDashboardSubsystem::Tick(float DeltaSeconds)
{
	if (Api == nullptr)
	{
		// The social table first: it brings the online bridge (the master) up and runs the dashboard.
		if (ScctFeature<FScctSocialApi>() == nullptr || (Api = ScctFeature<FScctDashboardApi>()) == nullptr)
		{
			return true;
		}
		if (bPreview)
		{
			Api->SetPreview(1);
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu dashboard: preview data (-ScctDashboardPreview), nothing is asked of the master"));
		}
	}

	const bool bShownNow = LastShown >= 0.0 && FPlatformTime::Seconds() - LastShown < ScctDashboardHideSeconds;
	if (bShownNow != bWatched)
	{
		bWatched = bShownNow;
		Api->SetWatched(bWatched ? 1 : 0);
		Api->GetInfo(&Info);
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu dashboard: %s (%d requests so far)"), bWatched ? TEXT("on screen, polling the master") : TEXT("off screen, polling stopped"), Info.Requests);
	}

	Api->GetInfo(&Info);
	if (Info.Revision != SeenRevision)
	{
		SeenRevision = Info.Revision;
		ReadAll();
	}
	return true;
}

void UScctDashboardSubsystem::ReadAll()
{
	const FString OldPlayers = Texts[static_cast<int32>(EScctDashboardText::PlayersLine)];
	const FString OldMaster = Texts[static_cast<int32>(EScctDashboardText::MasterLine)];
	for (int32 Kind = 0; Kind < ScctDashboardTextCount; ++Kind)
	{
		Texts[Kind] = ScctDashboardReadText([this, Kind](TCHAR* Buffer, int32 Capacity) { return Api->GetText(Kind, Buffer, Capacity); });
	}

	Releases.Reset();
	for (int32 Index = 0; Index < Info.Releases; ++Index)
	{
		FScctDashboardRelease Release;
		TCHAR Tag[128], Name[256], Date[64], Url[512];
		TCHAR ShortNotes[2048];
		const int32 NotesLength = Api->GetRelease(Index, Tag, UE_ARRAY_COUNT(Tag), Name, UE_ARRAY_COUNT(Name), Date, UE_ARRAY_COUNT(Date),
			ShortNotes, UE_ARRAY_COUNT(ShortNotes), Url, UE_ARRAY_COUNT(Url));
		if (NotesLength <= 0)
		{
			break;
		}
		Release.Tag = Tag;
		Release.Name = Name;
		Release.Date = Date;
		Release.Url = Url;
		if (NotesLength < UE_ARRAY_COUNT(ShortNotes))
		{
			Release.Notes = ShortNotes;
		}
		else
		{
			TArray<TCHAR> Long;
			Long.SetNumZeroed(NotesLength + 1);
			Api->GetRelease(Index, Tag, UE_ARRAY_COUNT(Tag), Name, UE_ARRAY_COUNT(Name), Date, UE_ARRAY_COUNT(Date), Long.GetData(), Long.Num(), Url, UE_ARRAY_COUNT(Url));
			Release.Notes = Long.GetData();
		}
		Releases.Add(MoveTemp(Release));
	}

	const FString& Players = Texts[static_cast<int32>(EScctDashboardText::PlayersLine)];
	const FString& Master = Texts[static_cast<int32>(EScctDashboardText::MasterLine)];
	if (Players != OldPlayers || Master != OldMaster)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu dashboard: %s · %s · %s · latest %s · %s"), *Players, *Texts[static_cast<int32>(EScctDashboardText::SessionsLine)],
			*Master, *Texts[static_cast<int32>(EScctDashboardText::LatestTitle)], *Texts[static_cast<int32>(EScctDashboardText::LatestState)]);
	}
}
