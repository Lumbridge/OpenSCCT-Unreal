#include "Menu/ScctTutorialPlay.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/ScctFrontEndWidget.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"

TWeakObjectPtr<UScctTutorialSubsystem> UScctTutorialSubsystem::Current;

namespace
{
	/** K.EndMenuDelay: seconds between the round's end and what follows. */
	constexpr float TutorialEndMenuDelay = 10.0f;

	/** The level URL options a tutorial play adds (read by UScctTutorialWatch). */
	const TCHAR* const TutorialOption = TEXT("Tutorial");
	const TCHAR* const TutorialVisitOption = TEXT("Visit");

	FString TutorialReadText(TFunctionRef<int32(TCHAR*, int32)> Read)
	{
		TCHAR Buffer[256];
		const int32 Length = Read(Buffer, UE_ARRAY_COUNT(Buffer));
		return Length > 0 ? FString(Buffer) : FString();
	}

	bool TutorialUrlSwitch(const FURL& Url, const TCHAR* Key)
	{
		if (!Url.HasOption(Key))
		{
			return false;
		}
		const FString Value = Url.GetOption(*(FString(Key) + TEXT("=")), TEXT(""));
		return !(Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("no"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("off"), ESearchCase::IgnoreCase));
	}
}

// ---- the subsystem ----------------------------------------------------------------------------------------------

UScctTutorialSubsystem* UScctTutorialSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctTutorialSubsystem>() : nullptr;
}

void UScctTutorialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Current = this;
	if (const FScctTutorialApi* Tutorials = Api())
	{
		const FString File = GetProgressFile();
		const int32 Read = Tutorials->Load(*File);
		if (FParse::Param(FCommandLine::Get(), TEXT("ScctTutorialReset")))
		{
			Tutorials->Reset();
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: progress reset (-ScctTutorialReset), saved to %s"), *File);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: %s %s; %s"), Read > 0 ? TEXT("progress read from") : Read == 0 ? TEXT("no progress saved yet at") : TEXT("progress could not be read from"),
			*File, *GetProgressLine());
	}
}

void UScctTutorialSubsystem::Deinitialize()
{
	if (Current.Get() == this)
	{
		Current.Reset();
	}
	Super::Deinitialize();
}

const FScctTutorialApi* UScctTutorialSubsystem::Api() const
{
	return ScctFeature<FScctTutorialApi>();
}

FString UScctTutorialSubsystem::GetProgressFile()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Profile") / TEXT("tutorials.json"));
}

FScctTutorialProgress UScctTutorialSubsystem::GetProgress() const
{
	FScctTutorialProgress Progress = {};
	if (const FScctTutorialApi* Tutorials = Api())
	{
		Tutorials->GetProgress(&Progress);
	}
	return Progress;
}

TArray<FString> UScctTutorialSubsystem::GetMaps(int32 Team) const
{
	TArray<FString> Maps;
	if (const FScctTutorialApi* Tutorials = Api())
	{
		for (int32 Index = 0;; ++Index)
		{
			const FString Map = TutorialReadText([&](TCHAR* Buffer, int32 Capacity) { return Tutorials->GetMap(Team, Index, Buffer, Capacity); });
			if (Map.IsEmpty())
			{
				break;
			}
			Maps.Add(Map);
		}
	}
	return Maps;
}

bool UScctTutorialSubsystem::IsDone(int32 Team, int32 Index) const
{
	const FScctTutorialProgress Progress = GetProgress();
	if (Team == 0 && Index == Progress.ExamIndex)
	{
		return Progress.ExamDone != 0;
	}
	return Index >= 0 && Index < 31 && ((Team == 0 ? Progress.SpyDone : Progress.MercDone) & (1 << Index)) != 0;
}

FString UScctTutorialSubsystem::GetProgressLine() const
{
	const FScctTutorialProgress Progress = GetProgress();
	int32 Spy = 0;
	for (int32 Index = 0; Index < Progress.ExamIndex; ++Index)
	{
		Spy += (Progress.SpyDone & (1 << Index)) != 0 ? 1 : 0;
	}
	int32 Merc = 0;
	for (int32 Index = 0; Index < Progress.MercCount; ++Index)
	{
		Merc += (Progress.MercDone & (1 << Index)) != 0 ? 1 : 0;
	}
	return FString::Printf(TEXT("Spy tutorials %d of %d, exam %s. Mercenary tutorials %d of %d."),
		Spy, Progress.ExamIndex, Progress.ExamDone != 0 ? TEXT("passed") : TEXT("not passed"), Merc, Progress.MercCount);
}

bool UScctTutorialSubsystem::CanPlay(const FString& Map)
{
	// The tutorial maps are not in the host list the menus' catalog holds; the test is whether the map can be drawn
	// (an imported level, or render files the runtime level is built from: LevelOf can be that level's URL with
	// ?ScctMap=, which DoesPackageExist refuses). The bridge reports a map whose content is gone when the match starts.
	return UScctOnlineSubsystem::IsPlayable(Map);
}

bool UScctTutorialSubsystem::PlayTutorial(const FString& Map, int32 Team)
{
	return Start(Map, Team == 1, false);
}

bool UScctTutorialSubsystem::Visit(const FString& Map, bool bMerc)
{
	return Start(Map, bMerc, true);
}

bool UScctTutorialSubsystem::Start(const FString& Map, bool bMerc, bool bVisit)
{
	UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance != nullptr ? Instance->GetWorld() : nullptr;
	if (World == nullptr || GEngine == nullptr)
	{
		return false;
	}
	if (!CanPlay(Map))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctTutorial: %s cannot be played here (its level is not imported and its render files cannot build it)"), *Map);
		return false;
	}

	// TutorialPlay.Start: the map alone, Story, the offline launch rules; a visit with the help paths and unlimited gear.
	FString Url = FString::Printf(TEXT("%s?Mode=Story?Side=%s?Bots=0?Practice=1?%s=1"), *UScctOnlineSubsystem::LevelOf(Map),
		bMerc ? TEXT("merc") : TEXT("spy"), bVisit ? TutorialVisitOption : TutorialOption);
	if (bVisit)
	{
		Url += TEXT("?UnlimitedGear=1?Help=1");
	}
	if (UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(World))
	{
		const FString Name = Online->GetPlayerName().Replace(TEXT("?"), TEXT("")).Replace(TEXT("#"), TEXT(""));
		if (!Name.IsEmpty())
		{
			Url += TEXT("?Name=") + Name;
		}
	}
	bReturnToTutorials = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: %s %s as a %s: %s"), bVisit ? TEXT("visiting") : TEXT("playing tutorial"), *Map, bMerc ? TEXT("mercenary") : TEXT("spy"), *Url);
	GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
	return true;
}

bool UScctTutorialSubsystem::TakeReturnToTutorials()
{
	const bool bReturn = bReturnToTutorials;
	bReturnToTutorials = false;
	return bReturn;
}

void UScctTutorialSubsystem::RunCommandLine(UScctFrontEndWidget* Menu)
{
	if (bCommandLineDone || Menu == nullptr)
	{
		return;
	}
	bCommandLineDone = true;

	FString Tutorial;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctTutorial="), Tutorial))
	{
		const int32 Team = GetMaps(1).ContainsByPredicate([&Tutorial](const FString& Map) { return Map.Equals(Tutorial, ESearchCase::IgnoreCase); }) ? 1 : 0;
		Menu->ShowPageNamed(Team == 1 ? TEXT("tutorialmaps/merc") : TEXT("tutorialmaps/spy"));
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctTutorial: %s from the %s tutorial maps page"), *Tutorial, Team == 1 ? TEXT("mercenary") : TEXT("spy"));
		PlayTutorial(Tutorial, Team);
		return;
	}
	FString VisitText;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctVisit="), VisitText, false))
	{
		FString Map = VisitText, Side;
		VisitText.Split(TEXT(","), &Map, &Side);
		Menu->ShowPageNamed(TEXT("visitmap"));
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctVisit: %s from the Visit a Map page"), *Map);
		Visit(Map, Side.Equals(TEXT("merc"), ESearchCase::IgnoreCase));
	}
}

// ---- the round watch ----------------------------------------------------------------------------------------------

bool UScctTutorialWatch::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UScctTutorialWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const bool bTutorial = TutorialUrlSwitch(InWorld.URL, TutorialOption);
	bVisit = TutorialUrlSwitch(InWorld.URL, TutorialVisitOption);
	if (!bTutorial && !bVisit)
	{
		return;
	}
	Map = InWorld.URL.GetOption(TEXT("ScctMap="), TEXT(""));
	if (Map.IsEmpty())
	{
		Map = FPackageName::GetShortName(InWorld.URL.Map);
	}
	bMerc = FString(InWorld.URL.GetOption(TEXT("Side="), TEXT(""))).Equals(TEXT("merc"), ESearchCase::IgnoreCase);
	bSeenRound = false;
	bEnded = false;
	bLeft = false;
	FParse::Value(FCommandLine::Get(), TEXT("ScctTutorialWin="), ForceWinAt);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: watching the round of %s (%s)"), *Map, bVisit ? TEXT("a visit") : TEXT("a tutorial"));
}

void UScctTutorialWatch::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Map.IsEmpty() || bLeft)
	{
		return;
	}
	const FScctTutorialApi* Tutorials = ScctFeature<FScctTutorialApi>();
	FScctTutorialRound Round = {};
	if (Tutorials == nullptr || Tutorials->GetRound(&Round) == 0 || Round.Remote != 0)
	{
		return;
	}
	bSeenRound = true;
	RoundSeconds += DeltaTime;

	if (!bEnded)
	{
		if (ForceWinAt >= 0.0f && RoundSeconds >= ForceWinAt)
		{
			ForceWinAt = -1.0f;
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctTutorialWin: ending the round of %s with the local side winning"), *Map);
			Tutorials->ForceWin(-1);
		}
		if (Round.Ended == 0)
		{
			return;
		}
		bEnded = true;
		const bool bWon = Round.WinningTeam == Round.LocalTeam;
		const bool bMarked = !bVisit && Round.Tutorial != 0 && Tutorials->FinishRound(*Map, bWon ? 1 : 0) != 0;
		TCHAR Buffer[64];
		Next = Tutorials->NextAfter(*Map, bVisit ? 1 : 0, Buffer, UE_ARRAY_COUNT(Buffer)) > 0 ? FString(Buffer) : FString();
		const FString Then = Next.IsEmpty() ? FString(TEXT("the Tutorials page")) : Next;
		if (bVisit)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: visit of %s over, next %s"), *Map, *Then);
		}
		else if (bMarked)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: %s done, next %s (saved to %s)"), *Map, *Then, *UScctTutorialSubsystem::GetProgressFile());
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: %s ended %s, next %s"), *Map, bWon ? TEXT("(not a tutorial map)") : TEXT("without a win"), *Then);
		}
		if (UScctTutorialSubsystem* Subsystem = UScctTutorialSubsystem::Get(GetWorld()))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: %s"), *Subsystem->GetProgressLine());
		}
		return;
	}

	EndedFor += DeltaTime;
	if (EndedFor > TutorialEndMenuDelay)
	{
		Leave();
	}
}

void UScctTutorialWatch::Leave()
{
	bLeft = true;
	UScctTutorialSubsystem* Subsystem = UScctTutorialSubsystem::Get(GetWorld());
	const int32 Team = Next.StartsWith(TEXT("TME")) ? 1 : 0;
	if (Subsystem != nullptr && !Next.IsEmpty() && UScctTutorialSubsystem::CanPlay(Next) && Subsystem->PlayTutorial(Next, Team))
	{
		return;
	}
	if (!Next.IsEmpty())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTutorial: %s is not imported here; back to the Tutorials page"), *Next);
	}
	// The front end shows the Tutorials page again (UScctTutorialSubsystem::TakeReturnToTutorials).
	if (UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(GetWorld()))
	{
		Online->OpenFrontEnd();
	}
}
