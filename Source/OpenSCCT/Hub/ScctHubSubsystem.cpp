#include "Hub/ScctHubSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Hub/SScctHubLoadingScreen.h"
#include "Hub/ScctHubActivities.h"
#include "Hub/ScctHubAudio.h"
#include "Hub/ScctHubEmotes.h"
#include "Hub/ScctHubHud.h"
#include "Hub/ScctHubPads.h"
#include "Hub/ScctHubProps.h"
#include "Hub/ScctHubVoice.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Online/ScctTokenStore.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"

const TCHAR* const UScctHubSubsystem::HubLevel = TEXT("/Game/Imported/Maps/Hub/Hub");
const TCHAR* const UScctHubSubsystem::HubOption = TEXT("ScctHub");
const TCHAR* const UScctHubSubsystem::HubListedSetting = TEXT("Online.HubListed");

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

	/** The project's settings, where the master the game uses is set (the online pages read the same keys). */
	const TCHAR* const HubProjectSection = TEXT("OpenSCCT.Online");

	/** Frames the loading cover is drawn before the hub level is opened (the load blocks the frames after it). */
	constexpr int32 CoverFramesBeforeLoad = 3;

	/** Frames the player stands in the drawn hub before the loading cover fades (HubService.CoverFramesInHub). */
	constexpr int32 CoverFramesStanding = 10;

	/** The loading cover never stays longer than this, whatever the hub sends (HubService.CoverMaxSeconds). */
	constexpr double CoverMaxSeconds = 30.0;

	/** Above the front end (20) and the HUD: the cover hides everything while the hub loads. */
	constexpr int32 CoverZOrder = 1000;

	const TCHAR* StateName(EScctHubState State)
	{
		static const TCHAR* const Names[] = { TEXT("Idle"), TEXT("Connecting"), TEXT("Loading"), TEXT("InHub"), TEXT("Failed"), TEXT("InMatch") };
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
	FParse::Value(CommandLine, TEXT("ScctHubCoverHold="), CoverHoldSeconds);
	FParse::Value(CommandLine, TEXT("ScctHubBots="), AutoBots);
	FString PadRun;
	if (FParse::Value(CommandLine, TEXT("ScctHubPad="), PadRun))
	{
		FString Number, PadSide;
		if (!PadRun.Split(TEXT(":"), &Number, &PadSide))
		{
			Number = PadRun;
		}
		AutoPad = FCString::Atoi(*Number);
		AutoPadSide = PadSide.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : 0;
		if (Side.IsEmpty())
		{
			// He enters the hub on the side he queues for.
			AutoTeam = AutoPadSide;
		}
		AutoPadOptions = TEXT("?TimeLimit=1?SvmBots=2");
		FParse::Value(CommandLine, TEXT("ScctHubPadOptions="), AutoPadOptions);
	}
	Hud = MakeShared<FScctHubHud>(this);
	Pads = MakeShared<FScctHubPads>(this);
	Activities = MakeShared<FScctHubActivities>(this);
	Props = MakeShared<FScctHubProps>(Activities);
	Audio = MakeShared<FScctHubAudio>(Activities);
	Emotes = MakeShared<FScctHubEmotes>(Activities);
	Voice = MakeShared<FScctHubVoice>(this, Activities);

	FScctSettingDesc Listed;
	Listed.Key = HubListedSetting;
	Listed.Page = TEXT("Online");
	Listed.Group = NSLOCTEXT("ScctHub", "HubGroup", "Hub");
	Listed.Label = NSLOCTEXT("ScctHub", "HubListed", "Show a hub I host in the hub list");
	Listed.Tooltip = NSLOCTEXT("ScctHub", "HubListedTip",
		"A hub you host from the Hub page is listed on the master server, where others find and join it (through its relay when your router does not let them in). Off: others join it by your address.");
	Listed.Type = EScctSettingType::Bool;
	Listed.Default = TEXT("true");
	FScctSettings::Register(Listed);

	// The status lines are for unattended runs; a player's log is not filled with them.
	LogEvery = AutoJoin.IsEmpty() && !bAutoHost ? 0.0f : 2.0f;
	FParse::Value(CommandLine, TEXT("ScctHubLog="), LogEvery);

	LastTickSeconds = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctHubSubsystem::TickFromTicker));
}

void UScctHubSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	if (Hud.IsValid())
	{
		Hud->Stop();
		Hud.Reset();
	}
	if (Pads.IsValid())
	{
		Pads->Stop();
		Pads.Reset();
	}
	StopActivities();
	Voice.Reset();
	Emotes.Reset();
	Audio.Reset();
	Props.Reset();
	Activities.Reset();
	CloseCover();
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
	ConfigureMaster();
	DrainLog();

	// A pad's match is an online visit the bridge starts: the online subsystem must be up to follow it.
	if (UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>(); Online == nullptr || !Online->EnsureReady())
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: the online subsystem is not up: the pads' matches cannot be followed"));
	}
	return Api;
}

void UScctHubSubsystem::ConfigureMaster()
{
	// The same master and sign-in the online pages use; the bridge keeps what the online side set already.
	FString MasterUrl, InviteKey, Token;
	GConfig->GetString(HubProjectSection, TEXT("MasterUrl"), MasterUrl, GGameIni);
	GConfig->GetString(HubProjectSection, TEXT("InviteKey"), InviteKey, GGameIni);
	Token = ScctAccountToken::Load();
	Api->ConfigureMaster(*MasterUrl, *InviteKey, *Token);
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
	CloseCover();
	bCoverShown = false;
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
	CloseCover();
	bCoverShown = false;
	const FString Name = FindPlayerName();
	const bool bListed = !bLoopbackOnly && FScctSettings::GetBool(HubListedSetting);
	Api->SetHostListed(bListed ? 1 : 0);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: hosting a hub on this PC (UDP port %d, %s%s) and joining it as '%s'"), Port > 0 ? Port : HubDefaultPort,
		bLoopbackOnly ? TEXT("this PC only") : TEXT("LAN"), bListed ? TEXT(", listed on the master") : TEXT(", not listed"), *Name);
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

bool UScctHubSubsystem::JoinListed(int32 Index, int32 Team)
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
	CloseCover();
	bCoverShown = false;
	const FString Name = FindPlayerName();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: joining listed hub %d ('%s') as '%s' (%s)"), Index, HubList.IsValidIndex(Index) ? HubList[Index].Name : TEXT("?"), *Name,
		Team == 1 ? TEXT("merc") : TEXT("spy"));
	const bool bStarted = Api->JoinListed(Index, *Name, Team) != 0;
	DrainLog();
	if (!bStarted)
	{
		Notice = ReadText(EScctHubText::Error);
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: the listed join did not start: %s"), *Notice);
	}
	Api->GetStatus(&Status);
	Touch();
	return bStarted;
}

bool UScctHubSubsystem::JoinListedId(const FString& Id, const FString& ListedName, bool bRequireAccount, int32 Team)
{
	// The listed join by id lives in the social table (it came with the website join).
	const FScctSocialApi* Social = ScctFeature<FScctSocialApi>();
	if (Connect() == nullptr || Social == nullptr)
	{
		return false;
	}

	UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
	if (Online != nullptr && Online->GetState() != EScctOnlineState::Idle)
	{
		Online->Leave();
	}

	Notice.Reset();
	bWasPresented = false;
	ReportedSessionId = 0;
	CloseCover();
	bCoverShown = false;
	// A website join enters under the account the master's ticket is for.
	const FString Name = Online != nullptr && !Online->GetAccountName().IsEmpty() ? Online->GetAccountName() : FindPlayerName();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: joining the master's hub %s ('%s') as '%s' (%s)"), *Id, *ListedName, *Name, Team == 1 ? TEXT("merc") : TEXT("spy"));
	const bool bStarted = Social->JoinHubListed(*Id, *ListedName, *Name, Team, bRequireAccount ? 1 : 0) != 0;
	DrainLog();
	if (!bStarted)
	{
		Notice = ReadText(EScctHubText::Error);
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: the join by id did not start: %s"), *Notice);
	}
	Api->GetStatus(&Status);
	Touch();
	return bStarted;
}

void UScctHubSubsystem::RequestHubList()
{
	if (Connect() == nullptr)
	{
		ListState = EScctHubListState::Failed;
		ListMessage = Notice;
		Touch();
		return;
	}
	Api->RequestList();
	DrainLog();
	RefreshList();
	Touch();
}

void UScctHubSubsystem::RefreshList()
{
	if (Api == nullptr)
	{
		return;
	}
	ListState = static_cast<EScctHubListState>(Api->GetListState());
	ListMessage = ReadText(EScctHubText::ListMessage);
	const int32 Count = Api->GetList(nullptr, 0);
	HubList.SetNumZeroed(Count);
	if (Count > 0)
	{
		Api->GetList(HubList.GetData(), Count);
	}
}

void UScctHubSubsystem::Leave()
{
	CloseCover();
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
		Session->OnFrame.AddUObject(this, &UScctHubSubsystem::HandleFrame);
	}
}

void UScctHubSubsystem::HandleFrame(float DeltaSeconds)
{
	// The session's frame, after the view and the pawns are this frame's: the hub HUD reads and projects from here.
	UWorld* World = PresentedWorld.IsValid() ? const_cast<UWorld*>(PresentedWorld.Get()) : nullptr;
	if (Hud.IsValid() && World != nullptr && GetState() == EScctHubState::InHub)
	{
		Hud->Tick(World, DeltaSeconds, LogEvery > 0.0f);
	}
	if (Pads.IsValid() && World != nullptr && GetState() == EScctHubState::InHub)
	{
		Pads->Tick(World, DeltaSeconds, LogEvery > 0.0f);
	}
	if (Activities.IsValid() && World != nullptr && GetState() == EScctHubState::InHub)
	{
		// The activities are read first: the props, the sounds, the emotes and the voice work from what was read.
		const bool bVerbose = LogEvery > 0.0f;
		Activities->Tick(World, DeltaSeconds, bVerbose);
		Props->Tick(World, DeltaSeconds, bVerbose);
		Audio->Tick(World, DeltaSeconds, bVerbose);
		Emotes->Tick(World, DeltaSeconds, bVerbose);
		Voice->Tick(World, DeltaSeconds, bVerbose);
	}
}

void UScctHubSubsystem::StopActivities()
{
	if (Activities.IsValid())
	{
		Voice->Stop();
		Emotes->Stop();
		Audio->Stop();
		Props->Stop();
		Activities->Stop();
	}
}

const TArray<FScctHubVoiceSpeaker>& UScctHubSubsystem::GetVoiceSpeakers() const
{
	static const TArray<FScctHubVoiceSpeaker> None;
	return Voice.IsValid() ? Voice->GetSpeakers() : None;
}

float UScctHubSubsystem::GetVoiceVolume(const FString& PlayerName) const
{
	const float* Volume = VoiceVolumes.Find(PlayerName);
	return Volume != nullptr ? *Volume : 1.0f;
}

void UScctHubSubsystem::SetVoiceVolume(const FString& PlayerName, float Volume)
{
	VoiceVolumes.Add(PlayerName, FMath::Clamp(Volume, 0.0f, 2.0f));
}

bool UScctHubSubsystem::IsVoiceMuted(const FString& PlayerName) const
{
	return VoiceMuted.Contains(PlayerName);
}

void UScctHubSubsystem::SetVoiceMuted(const FString& PlayerName, bool bMuted)
{
	if (bMuted)
	{
		VoiceMuted.Add(PlayerName);
	}
	else
	{
		VoiceMuted.Remove(PlayerName);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: %s's voice %s"), *PlayerName, bMuted ? TEXT("muted") : TEXT("unmuted"));
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

	// The hub HUD goes with the drawn hub (another level, the visit left or failed).
	if (Hud.IsValid() && Hud->IsRunning() && (!PresentedWorld.IsValid() || GetState() != EScctHubState::InHub))
	{
		Hud->Stop();
	}
	if (Pads.IsValid() && (!PresentedWorld.IsValid() || GetState() != EScctHubState::InHub))
	{
		Pads->Stop();
		StopActivities();
	}

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

	UpdateCover();
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
	RefreshList();
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
		CloseCover();
		Notice = ReadText(EScctHubText::Error);
		if (Notice.IsEmpty())
		{
			Notice = TEXT("The connection to the hub was lost.");
		}
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: the visit failed: %s"), *Notice);
	}
	if (New == EScctHubState::InMatch)
	{
		// The online subsystem opens the match's level; this level is left without leaving the hub.
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: away at a pad's match: the online visit plays it"));
	}
	if (Old == EScctHubState::InMatch && IsUnderWay(New))
	{
		// Back from the match: the hub level is opened now, before the online subsystem's way back to the front end,
		// and presents the visit again once the hub has welcomed the player.
		bWasPresented = false;
		ReportedSessionId = 0;
		PresentedWorld.Reset();
		if (FPackageName::DoesPackageExist(HubLevel))
		{
			OpenLevel(FString::Printf(TEXT("%s?%s=1"), HubLevel, HubOption));
		}
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
		if (Cover.IsValid() && CoverFramesUp < CoverFramesBeforeLoad)
		{
			// The cover is drawn first: the level load blocks the frames after it.
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
		if (HostAndJoin(AutoHostPort, AutoTeam, bAutoHostLoopback))
		{
			StartBots();
			if (AutoPad > 0 && Pads.IsValid())
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: -ScctHubPad: queueing on pad %d as a %s, the laptop taking '%s'"), AutoPad, AutoPadSide == 1 ? TEXT("merc") : TEXT("spy"), *AutoPadOptions);
				Pads->QueueOnPad(AutoPad, AutoPadSide, AutoPadOptions);
				DrainLog();
			}
		}
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

// ---- the loading cover ------------------------------------------------------------------------------------------

void UScctHubSubsystem::UpdateCover()
{
	const EScctHubState State = GetState();
	const bool bLocalPawn = HasFlag(EScctHubStatusFlags::LocalPawn);
	if (!Cover.IsValid())
	{
		// Up once the hub has welcomed the player and named its map (before the level is opened), until he stands in
		// the drawn hub; once a visit. The visit can go from Connecting straight to InHub within one update.
		if (!bCoverShown && IsUnderWay(State) && !MapToLoad.IsEmpty() && !bLocalPawn)
		{
			OpenCover();
		}
		return;
	}

	if (!IsUnderWay(State))
	{
		CloseCover();
		return;
	}

	++CoverFramesUp;
	const UWorld* World = GetGameInstance()->GetWorld();
	if (bLocalPawn && PendingLevel.IsEmpty() && IsPresenting(World))
	{
		++CoverFramesInHub;
	}

	const double Up = FPlatformTime::Seconds() - CoverSince;
	if (!Cover->IsFading())
	{
		const bool bTimedOut = Up > CoverMaxSeconds + CoverHoldSeconds;
		if (bTimedOut || (CoverFramesInHub >= CoverFramesStanding && Up >= CoverHoldSeconds))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: loading cover fades after %.1f s (%d frames standing in the hub%s)"), Up, CoverFramesInHub,
				bTimedOut ? TEXT(", timed out") : TEXT(""));
			Cover->FadeOut();
			CoverSince = FPlatformTime::Seconds();
		}
	}
	else if (Cover->IsGone() || Up > 2.0)
	{
		// Faded (or the screen is not drawn, and nothing ticks the fade): off the screen.
		CloseCover();
	}
}

void UScctHubSubsystem::OpenCover()
{
	UGameViewportClient* Viewport = GEngine != nullptr ? GEngine->GameViewport.Get() : nullptr;
	if (Viewport == nullptr)
	{
		return;
	}

	const TWeakObjectPtr<UScctHubSubsystem> Weak(this);
	Cover = SNew(SScctHubLoadingScreen)
		.bMerc(Status.Team == 1)
		.HubName_Lambda([Weak] { return Weak.IsValid() ? FText::FromString(Weak->HubName) : FText::GetEmpty(); })
		.Status_Lambda([Weak]
		{
			const UScctHubSubsystem* Hub = Weak.Get();
			if (Hub == nullptr)
			{
				return FText::GetEmpty();
			}
			if (!Hub->HasFlag(EScctHubStatusFlags::RoundActive))
			{
				return FText::FromString(TEXT("Loading the hub map..."));
			}
			return FText::FromString(Hub->HasFlag(EScctHubStatusFlags::LocalPawn) ? TEXT("Entering the hub...") : TEXT("Waiting for the hub to place you..."));
		});
	Viewport->AddViewportWidgetContent(Cover.ToSharedRef(), CoverZOrder);
	bCoverShown = true;
	CoverFramesUp = 0;
	CoverFramesInHub = 0;
	CoverSince = FPlatformTime::Seconds();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: loading cover up for '%s' (state %s)"), *HubName, StateName(GetState()));
}

void UScctHubSubsystem::CloseCover()
{
	if (!Cover.IsValid())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GEngine != nullptr ? GEngine->GameViewport.Get() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(Cover.ToSharedRef());
	}
	Cover.Reset();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: loading cover down"));
}
void UScctHubSubsystem::StartBots()
{
	// -ScctHubBots=N, or the level URL's ?Bots=N (Run-Offscreen -Options "?Bots=3").
	int32 Count = AutoBots;
	const UWorld* World = GetGameInstance()->GetWorld();
	if (Count < 0 && World != nullptr && World->URL.HasOption(TEXT("Bots")))
	{
		Count = FCString::Atoi(World->URL.GetOption(TEXT("Bots="), TEXT("0")));
	}
	if (Count <= 0)
	{
		return;
	}
	const FScctHubSocialApi* Social = ScctFeature<FScctHubSocialApi>();
	if (Social == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHub: no hub bots: the bridge has no hub social table"));
		return;
	}
	const bool bStarted = Social->StartBots(Count) != 0;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHub: %d hub bot(s) %s"), Count, bStarted ? TEXT("join the hosted hub") : TEXT("could not be started (see the bridge's lines)"));
}