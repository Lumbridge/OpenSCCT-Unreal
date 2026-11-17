#include "Hub/ScctHubSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

const TCHAR* const UScctHubSubsystem::HubLevel = TEXT("/Game/Imported/Maps/Hub/Hub");
const TCHAR* const UScctHubSubsystem::HubOption = TEXT("ScctHub");

namespace
{
	/** The player's own settings, where the online pages keep the name a guest goes by. */
	const TCHAR* const OnlineUserSection = TEXT("OpenSCCT.Online");

	/** The UDP port a hub listens on unless told otherwise (OpenSCCT's HubK.DefaultPort). */
	constexpr int32 HubDefaultPort = 28886;

	/** The moment a travel waits, so a screen can say where the game is going before the load blocks it. */
	constexpr float TravelNoticeSeconds = 0.25f;

	/** A level must have run this long before the hub is told the player is in: its first frames are drawn. */
	constexpr float LoadedAfterSeconds = 0.5f;

	const TCHAR* StateName(EScctHubState State)
	{
		static const TCHAR* const Names[] = { TEXT("Idle"), TEXT("Connecting"), TEXT("Loading"), TEXT("InHub"), TEXT("Failed") };
		const int32 Index = static_cast<int32>(State);
		return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("?");
	}

	bool IsUnderWay(EScctHubState State)
	{
		return State == EScctHubState::Connecting || State == EScctHubState::Loading || State == EScctHubState::InHub;
	}

	/** The package of the level a world was loaded from, without the prefix a play-in-editor copy gets. */
	FString LevelPackageOf(const UWorld& World)
	{
		return UWorld::RemovePIEPrefix(World.GetOutermost()->GetName());
	}
}

UScctHubSubsystem* UScctHubSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctHubSubsystem>() : nullptr;
}

void UScctHubSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("ScctHub="), AutoJoin);
	FString HostMode;
	if (FParse::Value(CommandLine, TEXT("ScctHostHub="), HostMode))
	{
		bAutoHost = true;
		bAutoHostLoopback = HostMode.Equals(TEXT("loopback"), ESearchCase::IgnoreCase);
	}
	else
	{
		bAutoHost = FParse::Param(CommandLine, TEXT("ScctHostHub"));
	}
	FParse::Value(CommandLine, TEXT("ScctHubPort="), AutoHostPort);
	FString Side;
	if (FParse::Value(CommandLine, TEXT("ScctSide="), Side))
	{
		AutoTeam = Side.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : 0;
	}
	// The status lines are for unattended runs; a player's log is not filled with them.
	LogEvery = AutoJoin.IsEmpty() && !bAutoHost ? 0.0f : 2.0f;
	FParse::Value(CommandLine, TEXT("ScctHubLog="), LogEvery);

	LastTickSeconds = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctHubSubsystem::TickFromTicker));
}

void UScctHubSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	if (Api != nullptr && GetState() != EScctHubState::Idle)
	{
		// The game is closing: the hub hears that the player left instead of timing them out.
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: leaving the hub (the game is closing, state %s)"), StateName(GetState()));
		Api->Leave();
		DrainLog();
	}
	Api = nullptr;
	Super::Deinitialize();
}

const FScctHubApi* UScctHubSubsystem::Connect()
{
	if (Api != nullptr)
	{
		return Api;
	}

	FString Error;
	Api = ScctFeature<FScctHubApi>(&Error);
	if (Api == nullptr)
	{
		Notice = Error;
		Touch();
		return nullptr;
	}

	// The hub map is not converted content: Import-HubMap.ps1 lays it out here for the importer and the bridge.
	const FString HubMaps = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ScctImport") / TEXT("HubContent") / TEXT("svmap"));
	Api->AddMapRoot(*HubMaps);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: ready, hub maps looked for first in %s%s"), *HubMaps,
		FPaths::DirectoryExists(HubMaps / TEXT("Hub")) ? TEXT("") : TEXT(" (missing: run Tools/Import/Import-HubMap.ps1)"));
	return Api;
}

FString UScctHubSubsystem::FindPlayerName() const
{
	FString Name;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctName="), Name) && !Name.IsEmpty())
	{
		return Name;
	}
	GConfig->GetString(OnlineUserSection, TEXT("PlayerName"), Name, GGameUserSettingsIni);
	return Name.IsEmpty() ? FString(TEXT("Player")) : Name;
}

FString UScctHubSubsystem::ReadText(EScctHubText Kind) const
{
	if (Api == nullptr)
	{
		return FString();
	}

	TCHAR Short[512];
	const int32 Length = Api->GetText(static_cast<int32>(Kind), Short, UE_ARRAY_COUNT(Short));
	if (Length < UE_ARRAY_COUNT(Short))
	{
		return FString(Short);
	}

	TArray<TCHAR> Long;
	Long.SetNumZeroed(Length + 1);
	Api->GetText(static_cast<int32>(Kind), Long.GetData(), Long.Num());
	return FString(Long.GetData());
}

// ---- joining, hosting and leaving -------------------------------------------------------------------------------

bool UScctHubSubsystem::Join(const FString& InAddress, int32 Team)
{
	if (Connect() == nullptr)
	{
		return false;
	}

	// A visit to a game server and a hub visit both want the bridge's session slot: one at a time.
	if (UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>(); Online != nullptr && Online->GetState() != EScctOnlineState::Idle)
	{
		Online->Leave();
	}

	Notice.Reset();
	bWasPresented = false;
	ReportedSessionId = 0;
	const FString Name = FindPlayerName();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: joining %s as '%s' (%s)"), *InAddress, *Name, Team == 1 ? TEXT("merc") : TEXT("spy"));
	const bool bStarted = Api->Connect(*InAddress, *Name, Team) != 0;
	DrainLog();
	if (!bStarted)
	{
		Notice = ReadText(EScctHubText::Error);
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: the join did not start: %s"), *Notice);
	}
	Touch();
	return bStarted;
}

bool UScctHubSubsystem::HostAndJoin(int32 Port, int32 Team, bool bLoopbackOnly)
{
	if (Connect() == nullptr)
	{
		return false;
	}

	if (UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>(); Online != nullptr && Online->GetState() != EScctOnlineState::Idle)
	{
		Online->Leave();
	}

	Notice.Reset();
	bWasPresented = false;
	ReportedSessionId = 0;
	const FString Name = FindPlayerName();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: hosting a hub on this PC (UDP port %d, %s) and joining it as '%s'"), Port > 0 ? Port : HubDefaultPort,
		bLoopbackOnly ? TEXT("this PC only") : TEXT("LAN"), *Name);
	const bool bStarted = Api->HostAndJoin(Port, *Name, Team, bLoopbackOnly ? 1 : 0) != 0;
	DrainLog();
	if (!bStarted)
	{
		Notice = ReadText(EScctHubText::Error);
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: could not host: %s"), *Notice);
	}
	Touch();
	return bStarted;
}

void UScctHubSubsystem::Leave()
{
	if (Api != nullptr && GetState() != EScctHubState::Idle)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: leaving (state %s)"), StateName(GetState()));
		Api->Leave();
		DrainLog();
		Api->GetStatus(&Status);
	}
	Notice.Reset();
	bWasPresented = false;
	Touch();
}

// ---- the frame --------------------------------------------------------------------------------------------------

bool UScctHubSubsystem::ShouldPresent(const UWorld* World) const
{
	if (World == nullptr)
	{
		return false;
	}
	if (World->URL.HasOption(HubOption))
	{
		return true;
	}
	const bool bVisitWanted = IsUnderWay(GetState()) || (!bAutoStarted && (bAutoHost || !AutoJoin.IsEmpty()));
	return bVisitWanted && LevelPackageOf(*World) == HubLevel;
}

void UScctHubSubsystem::PresentIn(UScctSessionSubsystem* Session)
{
	if (Session != nullptr && Session->FollowBridgeSessions(true))
	{
		PresentedWorld = Session->GetWorld();
		bWasPresented = true;
		Session->OnBeforeTicks.AddUObject(this, &UScctHubSubsystem::HandleBeforeTicks);
	}
}

void UScctHubSubsystem::HandleBeforeTicks(float DeltaSeconds)
{
	TickVisit();
}

bool UScctHubSubsystem::TickFromTicker(float DeltaSeconds)
{
	// In the hub level the visit has already been run this frame, before the simulation's ticks.
	TickVisit();
	return true;
}

void UScctHubSubsystem::Touch()
{
	++Revision;
	OnChanged.Broadcast();
}

void UScctHubSubsystem::TickVisit()
{
	if (LastTickFrame == GFrameCounter)
	{
		return;
	}
	LastTickFrame = GFrameCounter;

	const double Now = FPlatformTime::Seconds();
	const float DeltaSeconds = static_cast<float>(FMath::Clamp(Now - LastTickSeconds, 0.0, 1.0));
	LastTickSeconds = Now;

	ForgetFinishedTravel();

	if (Api == nullptr)
	{
		// The bridge is brought up by the first join, not by an idle frame; only an unattended join asks here.
		if (!bAutoStarted && (bAutoHost || !AutoJoin.IsEmpty()))
		{
			RunUnattended();
		}
		return;
	}

	Api->Update();
	const EScctHubState OldState = GetState();
	Api->GetStatus(&Status);
	DrainLog();
	if (Status.Revision != BridgeRevision)
	{
		BridgeRevision = Status.Revision;
		RefreshTexts();
		Touch();
	}
	if (GetState() != OldState)
	{
		HandleStateChange(OldState, GetState());
	}

	FollowLevel();
	ReportLoadedWhenReady();
	RunUnattended();
	LogStatus(DeltaSeconds);
}

void UScctHubSubsystem::RefreshTexts()
{
	HubName = ReadText(EScctHubText::HubName);
	StatusText = ReadText(EScctHubText::Status);
	Address = ReadText(EScctHubText::Address);
	MapToLoad = ReadText(EScctHubText::MapToLoad);
}

void UScctHubSubsystem::DrainLog()
{
	if (Api == nullptr)
	{
		return;
	}
	TCHAR Line[1024];
	for (int32 Taken = 0; Taken < 64 && Api->TakeLog(Line, UE_ARRAY_COUNT(Line)) >= 0; ++Taken)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: %s"), Line);
	}
}

void UScctHubSubsystem::HandleStateChange(EScctHubState Old, EScctHubState New)
{
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: state %s players %d (was %s, map to load '%s', flags 0x%x)"), StateName(New), Status.PlayerCount, StateName(Old), *MapToLoad, Status.Flags);
	if (New == EScctHubState::Failed)
	{
		Notice = ReadText(EScctHubText::Error);
		if (Notice.IsEmpty())
		{
			Notice = TEXT("The connection to the hub was lost.");
		}
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: the visit failed: %s"), *Notice);
	}
	Touch();
}

// ---- the levels -------------------------------------------------------------------------------------------------

void UScctHubSubsystem::OpenLevel(const FString& LevelUrl)
{
	UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: opening %s"), *LevelUrl);
	PendingLevel = LevelUrl;
	PendingFrom = World;
	GEngine->SetClientTravel(World, *LevelUrl, TRAVEL_Absolute);
}

void UScctHubSubsystem::ForgetFinishedTravel()
{
	const UWorld* World = GetGameInstance()->GetWorld();
	if (!PendingLevel.IsEmpty() && World != nullptr && World->HasBegunPlay() && PendingFrom.Get() != World)
	{
		PendingLevel.Reset();
	}
}

void UScctHubSubsystem::FollowLevel()
{
	const UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || !World->HasBegunPlay() || !PendingLevel.IsEmpty())
	{
		return;
	}

	const bool bPresenting = IsPresenting(World);
	const EScctHubState State = GetState();
	if (IsUnderWay(State) && bWasPresented && !bPresenting)
	{
		// The hub level was left another way (the in-match menu opens the front end): so is the hub.
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: the hub level was left (now in %s): leaving the hub"), *LevelPackageOf(*World));
		Leave();
		return;
	}

	if (IsUnderWay(State) && !MapToLoad.IsEmpty() && !bPresenting)
	{
		if (!FPackageName::DoesPackageExist(HubLevel))
		{
			// The bridge can play the hub, but there is nothing to draw it with.
			UE_LOG(LogOpenSCCT, Error, TEXT("ScctHub: the hub level %s has not been imported (Tools/Import/Import-HubMap.ps1)"), HubLevel);
			Api->Leave();
			DrainLog();
			Notice = TEXT("The hub's level has not been imported into this game yet.");
			Touch();
			return;
		}
		OpenLevel(FString::Printf(TEXT("%s?%s=1"), HubLevel, HubOption));
	}
	else if (State == EScctHubState::Failed && bPresenting)
	{
		// Back to the front end, which says why (GetNotice).
		OpenLevel(UScctOnlineSubsystem::FrontEndLevel);
	}
	else if (State == EScctHubState::Idle && bPresenting && World->URL.HasOption(HubOption))
	{
		OpenLevel(UScctOnlineSubsystem::FrontEndLevel);
	}
}

void UScctHubSubsystem::ReportLoadedWhenReady()
{
	if (!HasFlag(EScctHubStatusFlags::RoundActive) || !HasFlag(EScctHubStatusFlags::NeedsLoadReport) || !PendingLevel.IsEmpty())
	{
		return;
	}

	const UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || !World->HasBegunPlay() || World->GetTimeSeconds() < LoadedAfterSeconds || !IsPresenting(World))
	{
		return;
	}

	// The level's session subsystem must be presenting the visit's session: then the player sees it.
	const UScctSessionSubsystem* Session = World->GetSubsystem<UScctSessionSubsystem>();
	if (Session == nullptr || !Session->IsRemoteSession() || Session->GetSessionId() == ReportedSessionId)
	{
		return;
	}

	ReportedSessionId = Session->GetSessionId();
	Api->ReportLoaded();
	DrainLog();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: level %s is up and presents remote session %d: told the hub this player is in (ReportLoaded)"), *LevelPackageOf(*World), ReportedSessionId);
}

// ---- unattended -------------------------------------------------------------------------------------------------

void UScctHubSubsystem::RunUnattended()
{
	if (bAutoStarted || (!bAutoHost && AutoJoin.IsEmpty()))
	{
		return;
	}
	const UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || !World->HasBegunPlay())
	{
		return;
	}

	bAutoStarted = true;
	if (bAutoHost)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: -ScctHostHub: hosting a hub on this PC and joining it"));
		HostAndJoin(AutoHostPort, AutoTeam, bAutoHostLoopback);
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: -ScctHub: joining %s"), *AutoJoin);
		Join(AutoJoin, AutoTeam);
	}
}

void UScctHubSubsystem::LogStatus(float DeltaSeconds)
{
	if (LogEvery <= 0.0f || GetState() == EScctHubState::Idle)
	{
		return;
	}
	LogTimer -= DeltaSeconds;
	if (LogTimer > 0.0f)
	{
		return;
	}
	LogTimer = LogEvery;

	FString Line = FString::Printf(TEXT("ScctHub: state %s players %d | '%s' player %d team %d flags 0x%x%s | %s"), StateName(GetState()), Status.PlayerCount,
		*HubName, Status.PlayerId, Status.Team, Status.Flags, Status.HostPort > 0 ? *FString::Printf(TEXT(" hosting on UDP %d"), Status.HostPort) : TEXT(""), *StatusText);

	// In the hub: where the simulation's pawns are, so the log shows the local pawn standing in the hall.
	const UWorld* World = GetGameInstance()->GetWorld();
	const UScctSessionSubsystem* Session = World != nullptr ? World->GetSubsystem<UScctSessionSubsystem>() : nullptr;
	if (Session != nullptr && Session->IsRemoteSession() && IsPresenting(World))
	{
		Line += FString::Printf(TEXT(" | session %d, %d pawns:"), Session->GetSessionId(), Session->GetPawns().Num());
		for (const FScctBridgePawn& Pawn : Session->GetPawns())
		{
			Line += FString::Printf(TEXT(" #%d %s%s at (%.0f, %.0f, %.0f)"), Pawn.ActorId, Pawn.Kind == 1 ? TEXT("merc") : TEXT("spy"),
				(Pawn.Flags & EScctPawnFlags::Local) != 0 ? TEXT(" LOCAL") : TEXT(""), Pawn.X, Pawn.Y, Pawn.Z);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("%s"), *Line);
}
