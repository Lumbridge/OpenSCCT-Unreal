#include "Online/ScctOnlineSubsystem.h"

#include "Demo/ScctDemoSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Online/ScctContentDownloads.h"
#include "Online/ScctMapFolders.h"
#include "OpenSCCT.h"
#include "Online/ScctTokenStore.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"

const TCHAR* const UScctOnlineSubsystem::FrontEndLevel = TEXT("/Game/OpenSCCT/Maps/L_FrontEnd");
const TCHAR* const UScctOnlineSubsystem::OnlineOption = TEXT("ScctOnline");
const TCHAR* const UScctOnlineSubsystem::RuntimeLevel = TEXT("/Game/OpenSCCT/RuntimeMap/L_RuntimeMap");

namespace
{
	// The player's own settings (Saved/Config, never the project's): who they are online.
	const TCHAR* const UserSection = TEXT("OpenSCCT.Online");

	// The master server and the invite key come from the Online settings page, which defaults to the project's
	// Config/DefaultGame.ini (ScctOnlineHost.cpp, ReadMasterSettings).

	constexpr int32 MaxPlayers = 64;
	constexpr int32 MaxServers = 64;
	constexpr int32 MaxChatLines = 60;

	/** The moment a travel waits, so the screen can say where the game is going before the load blocks it. */
	constexpr float TravelNoticeSeconds = 0.25f;

	/** A level must have run this long before the server is told the player is in: its first frames are drawn. */
	constexpr float LoadedAfterSeconds = 0.5f;

	const TCHAR* StateName(EScctOnlineState State)
	{
		static const TCHAR* const Names[] = { TEXT("Idle"), TEXT("Connecting"), TEXT("Lobby"), TEXT("Travelling"), TEXT("Loading"),
			TEXT("Match"), TEXT("Results"), TEXT("Failed"), TEXT("Left") };
		const int32 Index = static_cast<int32>(State);
		return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("?");
	}

	bool IsOnARoundsMap(EScctOnlineState State)
	{
		return State == EScctOnlineState::Travelling || State == EScctOnlineState::Loading
			|| State == EScctOnlineState::Match || State == EScctOnlineState::Results;
	}

	/** The package of the level a world was loaded from, without the prefix a play-in-editor copy gets. */
	FString LevelPackageOf(const UWorld& World)
	{
		return UWorld::RemovePIEPrefix(World.GetOutermost()->GetName());
	}
}

UScctOnlineSubsystem* UScctOnlineSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctOnlineSubsystem>() : nullptr;
}

FString UScctOnlineSubsystem::ImportedLevelOf(const FString& Map)
{
	return FString::Printf(TEXT("/Game/Imported/Maps/%s/%s"), *Map, *Map);
}

FString UScctOnlineSubsystem::LevelOf(const FString& Map)
{
	const FString Imported = ImportedLevelOf(Map);
	if (Map.IsEmpty() || FPackageName::DoesPackageExist(Imported) || !FPackageName::DoesPackageExist(RuntimeLevel))
	{
		return Imported;
	}
	return FString::Printf(TEXT("%s?ScctMap=%s"), RuntimeLevel, *Map);
}

FString UScctOnlineSubsystem::LevelPackageOfMap(const FString& Map)
{
	FString Level = LevelOf(Map);
	int32 Options = INDEX_NONE;
	if (Level.FindChar(TEXT('?'), Options))
	{
		Level.LeftInline(Options);
	}
	return Level;
}

bool UScctOnlineSubsystem::IsPlayable(const FString& Map)
{
	if (Map.IsEmpty())
	{
		return false;
	}
	if (FPackageName::DoesPackageExist(ImportedLevelOf(Map)))
	{
		return true;
	}
	return FPackageName::DoesPackageExist(RuntimeLevel) && ScctMapFolders::CanBuild(Map);
}

bool UScctOnlineSubsystem::IsLevelOf(const UWorld& World, const FString& Map)
{
	if (LevelPackageOf(World) != LevelPackageOfMap(Map))
	{
		return false;
	}
	// The runtime level plays whichever map its URL names.
	return LevelPackageOf(World) != RuntimeLevel || World.URL.GetOption(TEXT("ScctMap="), TEXT("")) == Map;
}

TArray<FString> UScctOnlineSubsystem::FindPlayableMaps()
{
	// The maps a match can be played on are the ones the bridge has converted content for (in any of its map
	// folders); of those, the ones with an imported level, or with the render files the runtime level is built
	// from, can be shown.
	TArray<FString> Maps;
	const FScctOnlineApi* Online = ScctFeature<FScctOnlineApi>();
	if (Online == nullptr)
	{
		return Maps;
	}
	ScctMapFolders::Apply();

	TArray<TCHAR> Buffer;
	Buffer.SetNumZeroed(Online->GetText(static_cast<int32>(EScctOnlineText::InstalledMaps), nullptr, 0) + 1);
	Online->GetText(static_cast<int32>(EScctOnlineText::InstalledMaps), Buffer.GetData(), Buffer.Num());
	TArray<FString> Installed;
	FString(Buffer.GetData()).ParseIntoArrayLines(Installed);
	const bool bRuntimeLevel = FPackageName::DoesPackageExist(RuntimeLevel);
	TSet<FString> Renderable;
	if (bRuntimeLevel)
	{
		Renderable.Append(ScctMapFolders::RenderableMaps());
	}
	for (const FString& Map : Installed)
	{
		if (FPackageName::DoesPackageExist(ImportedLevelOf(Map)) || Renderable.Contains(Map))
		{
			Maps.Add(Map);
		}
	}
	Maps.Sort();
	return Maps;
}

void UScctOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("ScctJoin="), AutoJoin);
	FString Side;
	if (FParse::Value(CommandLine, TEXT("ScctSide="), Side))
	{
		AutoSide = Side.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1
			: Side.Equals(TEXT("spy"), ESearchCase::IgnoreCase) ? 0
			: Side.Equals(TEXT("watch"), ESearchCase::IgnoreCase) ? 255 : -1;
	}
	bAutoReady = FParse::Param(CommandLine, TEXT("ScctAutoReady"));
	bAutoWalk = FParse::Param(CommandLine, TEXT("ScctAutoWalk"));
	FParse::Value(CommandLine, TEXT("ScctLeaveAfter="), LeaveAfter);
	FParse::Value(CommandLine, TEXT("ScctChat="), AutoChat, false);
	ReadHostSwitches();
	ReadModerationSwitches();
	// The status lines are for unattended runs; a player's log is not filled with them.
	LogEvery = AutoJoin.IsEmpty() && AutoHostPort < 0 ? 0.0f : 2.0f;
	FParse::Value(CommandLine, TEXT("ScctOnlineLog="), LogEvery);

	RegisterOnlineSettings();
	SettingsChangedHandle = FScctSettings::OnChanged().AddUObject(this, &UScctOnlineSubsystem::HandleSettingChanged);
	// Missing-map downloads: -ScctAutoDownload and the log of maps that come in (Online/ScctContentDownloads.h).
	ScctContentDownloads::Start(this);

	LastTickSeconds = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctOnlineSubsystem::TickFromTicker));
}

void UScctOnlineSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	FScctSettings::OnChanged().Remove(SettingsChangedHandle);
	if (Api != nullptr && GetState() != EScctOnlineState::Idle)
	{
		// The game is closing: the server hears that the player left instead of timing them out.
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: leaving the server (the game is closing, state %s)"), StateName(GetState()));
		Api->Leave();
	}
	Api = nullptr;
	Super::Deinitialize();
}

const FScctOnlineApi* UScctOnlineSubsystem::Connect()
{
	if (Api != nullptr)
	{
		return Api;
	}

	FString Error;
	Api = ScctFeature<FScctOnlineApi>(&Error);
	if (Api == nullptr)
	{
		Notice = Error;
		Touch();
		return nullptr;
	}

	LoadIdentity();
	// Every visit is made with the demo-saving choice (Demo/ScctDemoSubsystem.h), so it is in the bridge before any join.
	UScctDemoSubsystem::PushChoice();

	FString MasterUrl, InviteKey;
	ReadMasterSettings(MasterUrl, InviteKey);
	ApplyConnectMode();
	if (!MasterUrl.IsEmpty())
	{
		Api->SetMaster(*MasterUrl, *InviteKey);
		bMasterSet = true;

		// A sign-in of an earlier run: the master answers with the account.
		// Sealed for this Windows user (Online/ScctTokenStore.h), or -ScctAccountToken for this run only.
		const FString Token = ScctAccountToken::Load();
		if (!Token.IsEmpty())
		{
			Api->SetAccountToken(*Token);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: ready as '%s'%s"), *PlayerName, bMasterSet ? *FString::Printf(TEXT(", master %s"), *MasterUrl) : TEXT(", no master server set"));
	return Api;
}

void UScctOnlineSubsystem::LoadIdentity()
{
	FString InstallId, PrivateKey;
	GConfig->GetString(UserSection, TEXT("PlayerName"), PlayerName, GGameUserSettingsIni);
	GConfig->GetString(UserSection, TEXT("InstallId"), InstallId, GGameUserSettingsIni);
	GConfig->GetString(UserSection, TEXT("PrivateKey"), PrivateKey, GGameUserSettingsIni);

	// -ScctName: for this run only.
	FString RunName;
	const bool bRunName = FParse::Value(FCommandLine::Get(), TEXT("ScctName="), RunName) && !RunName.IsEmpty();
	if (PlayerName.IsEmpty())
	{
		PlayerName = TEXT("Player");
	}

	// Empty id or key: the bridge makes new ones, which are saved so bans and a kept place follow this install.
	Api->SetIdentity(bRunName ? *RunName : *PlayerName, InstallId.IsEmpty() ? nullptr : *InstallId, PrivateKey.IsEmpty() ? nullptr : *PrivateKey);
	if (bRunName)
	{
		PlayerName = RunName;
	}
	if (InstallId.IsEmpty() || PrivateKey.IsEmpty())
	{
		GConfig->SetString(UserSection, TEXT("InstallId"), *ReadText(EScctOnlineText::InstallId), GGameUserSettingsIni);
		GConfig->SetString(UserSection, TEXT("PrivateKey"), *ReadText(EScctOnlineText::PrivateKey), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UScctOnlineSubsystem::SaveIdentity()
{
	GConfig->SetString(UserSection, TEXT("PlayerName"), *PlayerName, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UScctOnlineSubsystem::SetPlayerName(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd().Left(31);
	if (Trimmed.IsEmpty() || Trimmed == PlayerName)
	{
		return;
	}
	PlayerName = Trimmed;
	SaveIdentity();
	if (Connect() != nullptr)
	{
		// Null id and key: the bridge keeps the ones it has.
		Api->SetIdentity(*PlayerName, nullptr, nullptr);
	}
}

FString UScctOnlineSubsystem::ReadText(EScctOnlineText Kind) const
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

// ---- joining and leaving ------------------------------------------------------------------------------------------

bool UScctOnlineSubsystem::Join(const FString& Address, const FString& Password, int32 PreferredTeam)
{
	if (Connect() == nullptr)
	{
		return false;
	}

	Notice.Reset();
	Chat.Reset();
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: joining %s (side asked for: %d)"), *Address, PreferredTeam);
	const bool bStarted = Api->Join(*Address, Password.IsEmpty() ? nullptr : *Password, PreferredTeam) != 0;
	if (!bStarted)
	{
		Notice = ReadText(EScctOnlineText::Error);
		UE_LOG(LogOpenSCCT, Warning, TEXT("Online: the join did not start: %s"), *Notice);
	}
	Touch();
	return bStarted;
}

void UScctOnlineSubsystem::JoinListed(int32 Index, const FString& Password, int32 PreferredTeam)
{
	if (Connect() == nullptr || !Servers.IsValidIndex(Index))
	{
		return;
	}

	Notice.Reset();
	Chat.Reset();
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: joining listed server %d '%s'"), Index, Servers[Index].Name);
	Api->JoinListed(Index, Password.IsEmpty() ? nullptr : *Password, PreferredTeam);
	Touch();
}

void UScctOnlineSubsystem::Leave()
{
	if (Api != nullptr && GetState() != EScctOnlineState::Idle)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: leaving (state %s)"), StateName(GetState()));
		Api->Leave();
	}
	// The bridge's Leave closes the player's own server with the visit to it.
	HostPort = 0;
	Notice.Reset();
	Touch();
}

// ---- the lobby ----------------------------------------------------------------------------------------------------

void UScctOnlineSubsystem::SetTeam(int32 Team)
{
	if (Api != nullptr)
	{
		Api->SetTeam(Team);
	}
}

void UScctOnlineSubsystem::SetReady(bool bReady)
{
	if (Api != nullptr)
	{
		Api->SetReady(bReady ? 1 : 0);
	}
}

void UScctOnlineSubsystem::Launch()
{
	if (Api != nullptr)
	{
		Api->Launch();
	}
}

void UScctOnlineSubsystem::SendChat(const FString& Text, bool bTeamOnly)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Api != nullptr && !Trimmed.IsEmpty())
	{
		Api->SendChat(*Trimmed, bTeamOnly ? 1 : 0);
	}
}

bool UScctOnlineSubsystem::SetMatch(const FString& Map, int32 Mode)
{
	if (Api == nullptr || Api->SetMatch(*Map, Mode) == 0)
	{
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: asked the server for the next match: map %s, mode %d, the map's own settings"), Map.IsEmpty() ? *SetupMap : *Map, Mode);
	return true;
}

bool UScctOnlineSubsystem::SetSetupOptions(const FString& Options)
{
	if (Api == nullptr || Options.IsEmpty() || Api->SetSetupOptions(*Options) == 0)
	{
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: asked the server to change the setup: %s"), *Options);
	return true;
}

int32 UScctOnlineSubsystem::GetSetupOption(const TCHAR* Name, int32 Fallback) const
{
	// "?MapName=AquaE?TimeLimit=20...": the value runs to the next '?'.
	const FString Key = FString(TEXT("?")) + Name + TEXT("=");
	const int32 At = SetupOptions.Find(Key, ESearchCase::IgnoreCase);
	if (At == INDEX_NONE)
	{
		return Fallback;
	}
	const FString Value = SetupOptions.Mid(At + Key.Len());
	return Value.IsEmpty() ? Fallback : FCString::Atoi(*Value);
}

bool UScctOnlineSubsystem::GetResults(FScctOnlineResults& OutResults, TArray<FScctOnlineResultRow>& OutRows) const
{
	OutRows.Reset();
	if (Api == nullptr)
	{
		return false;
	}

	OutRows.SetNumZeroed(MaxPlayers);
	OutResults = {};
	const int32 Count = Api->GetResults(&OutResults, OutRows.GetData(), OutRows.Num());
	OutRows.SetNum(FMath::Clamp(Count, 0, MaxPlayers));
	return Count >= 0;
}

// ---- master and account -------------------------------------------------------------------------------------------

void UScctOnlineSubsystem::RequestServerList()
{
	if (Connect() != nullptr && bMasterSet)
	{
		Api->RequestServerList();
	}
}

void UScctOnlineSubsystem::BeginSignIn()
{
	if (Connect() != nullptr && bMasterSet)
	{
		bSignInPageOpened = false;
		Api->BeginSignIn();
	}
}

void UScctOnlineSubsystem::SignOut()
{
	if (Api != nullptr)
	{
		Api->SignOut();
		ScctAccountToken::Clear();
	}
}

// ---- the frame ----------------------------------------------------------------------------------------------------

void UScctOnlineSubsystem::PresentIn(UScctSessionSubsystem* Session)
{
	if (Session != nullptr && Session->FollowBridgeSessions(true))
	{
		Session->OnBeforeTicks.AddUObject(this, &UScctOnlineSubsystem::HandleBeforeTicks);
	}
}

void UScctOnlineSubsystem::HandleBeforeTicks(float DeltaSeconds)
{
	// -ScctAutoWalk: the local player's "forward" key goes down while a round is played, on the player controller
	// as under a finger, so it takes the path a real key takes through the bridge's bindings.
	if (bAutoWalk)
	{
		const UWorld* World = GetGameInstance()->GetWorld();
		AScctPlayerController* Player = World != nullptr ? Cast<AScctPlayerController>(World->GetFirstPlayerController()) : nullptr;
		const bool bWalk = GetState() == EScctOnlineState::Match;
		if (Player != nullptr && (bWalk != bAutoWalkHeld || AutoWalkPlayer.Get() != Player))
		{
			// A new level has a new controller: the key is pressed again there.
			AutoWalkPlayer = Player;
			bAutoWalkHeld = bWalk;
			Player->HoldScriptedAction(TEXT("forward"), bWalk);
		}
	}

	TickVisit();
}

bool UScctOnlineSubsystem::TickFromTicker(float DeltaSeconds)
{
	// In a level that presents a round the visit has already been run this frame, before the simulation's ticks.
	TickVisit();
	return true;
}

void UScctOnlineSubsystem::Touch()
{
	++Revision;
	OnChanged.Broadcast();
}

void UScctOnlineSubsystem::TickVisit()
{
	if (LastTickFrame == GFrameCounter)
	{
		return;
	}
	LastTickFrame = GFrameCounter;

	const double Now = FPlatformTime::Seconds();
	const float DeltaSeconds = static_cast<float>(FMath::Clamp(Now - LastTickSeconds, 0.0, 1.0));
	LastTickSeconds = Now;

	// Before anything that needs the bridge: a game that never went online travels too (main menu from a local
	// match), and its next travel must not find this one still pending.
	ForgetFinishedTravel();

	if (Api == nullptr)
	{
		// The bridge is brought up by the first thing that needs it (a join, the server list, the map list), not
		// by an idle frame; only an unattended join asks for it here.
		if (!AutoJoin.IsEmpty() || AutoHostPort >= 0 || FParse::Param(FCommandLine::Get(), TEXT("ScctListServers")))
		{
			RunUnattended(DeltaSeconds);
		}
		return;
	}

	Api->Update();
	TickHost();

	const EScctOnlineState OldState = GetState();
	const bool bHadResults = HasFlag(EScctOnlineStatusFlags::ResultsReady);
	Api->GetStatus(&Status);
	if (Status.Revision != BridgeRevision)
	{
		BridgeRevision = Status.Revision;
		RefreshTexts();
		Touch();
	}
	DrainChat();
	if (GetState() != OldState)
	{
		HandleStateChange(OldState, GetState());
	}
	if (!bHadResults && HasFlag(EScctOnlineStatusFlags::ResultsReady))
	{
		// The rows arrive a little after the state turns to Results.
		FScctOnlineResults Results;
		TArray<FScctOnlineResultRow> Rows;
		if (GetResults(Results, Rows))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: results are in: winning team %d, reason %d, %d players, %d game(s) left, next step in %.0f s"),
				Results.WinningTeam, Results.WinningReason, Rows.Num(), Results.GamesLeft, Status.Countdown);
		}
	}

	FollowLevel();
	ReportLoadedWhenReady();
	RunUnattended(DeltaSeconds);
	LogStatus(DeltaSeconds);
}

void UScctOnlineSubsystem::RefreshTexts()
{
	Players.SetNumZeroed(MaxPlayers, EAllowShrinking::No);
	Players.SetNum(FMath::Clamp(Api->GetPlayers(Players.GetData(), MaxPlayers), 0, MaxPlayers), EAllowShrinking::No);

	ServerName = ReadText(EScctOnlineText::ServerName);
	SetupMap = ReadText(EScctOnlineText::SetupMap);
	ModeName = ReadText(EScctOnlineText::ModeName);
	SetupDescription = ReadText(EScctOnlineText::SetupDescription);
	ConnectLog = ReadText(EScctOnlineText::ConnectLog);
	MapToLoad = ReadText(EScctOnlineText::MapToLoad);
	// A name claimed or changed on the Account page is the name signed-in joins use from now on.
	AccountName = ReadText(EScctOnlineText::AccountName);
	SetupOptions = ReadText(EScctOnlineText::SetupOptions);
	Motd = ReadText(EScctOnlineText::MasterMotd);

	// The setup and the lobby's players into the log when they change: what the host set up, and who it un-readied.
	const FString NewSetup = ReadText(EScctOnlineText::SetupOptions);
	if (NewSetup != SetupOptions)
	{
		SetupOptions = NewSetup;
		if (!SetupOptions.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: the lobby's setup is now %s (%s)"), *SetupOptions, *SetupDescription);
		}
	}
	FString Roster;
	if (GetState() == EScctOnlineState::Lobby)
	{
		for (const FScctOnlinePlayer& Player : Players)
		{
			static const TCHAR* const MapStates[] = { TEXT("map ?"), TEXT("map ok"), TEXT("map MISSING"), TEXT("map OTHER VERSION") };
			Roster += FString::Printf(TEXT("%s%s (%s, %s, %s)"), Roster.IsEmpty() ? TEXT("") : TEXT(", "), Player.Name,
				Player.Team == 0 ? TEXT("spy") : Player.Team == 1 ? TEXT("merc") : TEXT("watching"),
				(Player.Flags & EScctOnlinePlayerFlags::Ready) != 0 ? TEXT("ready") : TEXT("not ready"), MapStates[FMath::Clamp(Player.MapState, 0, 3)]);
		}
	}
	if (Roster != LoggedRoster)
	{
		LoggedRoster = Roster;
		if (!Roster.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: lobby players: %s"), *Roster);
		}
	}

	if (Status.MasterState != LastMasterState)
	{
		LastMasterState = Status.MasterState;
		MasterError = ReadText(EScctOnlineText::MasterError);
		Servers.SetNumZeroed(MaxServers, EAllowShrinking::No);
		Servers.SetNum(FMath::Clamp(Api->GetServers(Servers.GetData(), MaxServers), 0, MaxServers), EAllowShrinking::No);
		if (LastMasterState == static_cast<int32>(EScctOnlineMasterState::Ready))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: the master lists %d server(s)"), Servers.Num());
			for (const FScctOnlineServerEntry& Server : Servers)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Online:   '%s' map %s mode %d phase %d players %d/%d flags 0x%x"),
					Server.Name, Server.Map, Server.Mode, Server.Phase, Server.Players, Server.MaxPlayers, Server.Flags);
			}
		}
		else if (LastMasterState == static_cast<int32>(EScctOnlineMasterState::Failed))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Online: the master did not answer: %s"), *MasterError);
		}
	}

	if (Status.AccountState != LastAccountState)
	{
		LastAccountState = Status.AccountState;
		AccountName = ReadText(EScctOnlineText::AccountName);
		AccountError = ReadText(EScctOnlineText::AccountError);
		if (LastAccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn))
		{
			// Kept for the next run; never shown and never logged.
			ScctAccountToken::Save(ReadText(EScctOnlineText::AccountToken));
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: signed in as '%s'"), *AccountName);
		}
	}
	if (LastAccountState == static_cast<int32>(EScctOnlineAccountState::SigningIn))
	{
		SignInCode = ReadText(EScctOnlineText::SignInCode);
		const FString Url = ReadText(EScctOnlineText::SignInUrl);
		if (!bSignInPageOpened && !Url.IsEmpty())
		{
			// Only ever after the player pressed "sign in" (BeginSignIn).
			bSignInPageOpened = true;
			FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
		}
	}
	else
	{
		SignInCode.Reset();
	}
}

void UScctOnlineSubsystem::DrainChat()
{
	FScctOnlineChatLine Line;
	bool bAny = false;
	for (int32 Taken = 0; Taken < 32 && Api->TakeChat(&Line) == 1; ++Taken)
	{
		FScctOnlineChatEntry& Entry = Chat.AddDefaulted_GetRef();
		Entry.bSystem = Line.System != 0;
		Entry.bTeamOnly = Line.TeamOnly != 0;
		Entry.Team = Line.Team;
		Entry.Sender = Line.Sender;
		Entry.Text = Line.Text;
		// A team line says so, and whose side it was for: only that side's players receive one.
		const FString Team = Entry.bTeamOnly ? FString::Printf(TEXT("[team only, %s] "), Entry.Team == 0 ? TEXT("spies") : Entry.Team == 1 ? TEXT("mercs") : TEXT("?")) : FString();
		UE_LOG(LogOpenSCCT, Log, TEXT("Online chat: %s%s%s"), *Team, Entry.bSystem ? TEXT("[server] ") : *FString::Printf(TEXT("<%s> "), *Entry.Sender), *Entry.Text);
		++ChatReceived;
		bAny = true;
	}
	if (bAny)
	{
		if (Chat.Num() > MaxChatLines)
		{
			Chat.RemoveAt(0, Chat.Num() - MaxChatLines);
		}
		Touch();
	}
}

void UScctOnlineSubsystem::HandleStateChange(EScctOnlineState Old, EScctOnlineState New)
{
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: state %s -> %s (server phase %d, map to load '%s')"), StateName(Old), StateName(New), Status.Phase, *MapToLoad);

	if (New == EScctOnlineState::Failed)
	{
		Notice = ReadText(EScctOnlineText::Error);
		if (Notice.IsEmpty())
		{
			Notice = TEXT("The connection to the server was lost.");
		}
		UE_LOG(LogOpenSCCT, Warning, TEXT("Online: the visit failed%s: %s"), HasFlag(EScctOnlineStatusFlags::Kicked) ? TEXT(" (status Kicked: the server kicked or banned this player)") : TEXT(""), *Notice);
	}
	else if (New == EScctOnlineState::Match && MatchSeconds < 0.0f)
	{
		MatchSeconds = 0.0f;
	}
	else if (New == EScctOnlineState::Lobby && IsHosting())
	{
		// The match the player's own server is set up for, as the session holds it (the line Create Session sent).
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: the hosted lobby's setup is %s"), *ReadText(EScctOnlineText::SetupOptions));
	}
	Touch();
}

// ---- the levels ---------------------------------------------------------------------------------------------------

void UScctOnlineSubsystem::OpenLevel(const FString& LevelUrl)
{
	UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: opening %s"), *LevelUrl);
	PendingLevel = LevelUrl;
	PendingFrom = World;
	GEngine->SetClientTravel(World, *LevelUrl, TRAVEL_Absolute);
}

void UScctOnlineSubsystem::OpenFrontEnd()
{
	if (PendingLevel.IsEmpty())
	{
		OpenLevel(FrontEndLevel);
	}
}

void UScctOnlineSubsystem::ForgetFinishedTravel()
{
	// The travel that was asked for happens at the start of the engine's next frame; it is over once another
	// world is open and playing (the level asked for or, if it could not be loaded, the engine's default).
	const UWorld* World = GetGameInstance()->GetWorld();
	if (!PendingLevel.IsEmpty() && World != nullptr && World->HasBegunPlay() && PendingFrom.Get() != World)
	{
		PendingLevel.Reset();
		TravelDelay = 0.0f;
	}
}

void UScctOnlineSubsystem::FollowLevel()
{
	const UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || !World->HasBegunPlay() || !PendingLevel.IsEmpty())
	{
		// Nothing new is asked while a travel is pending.
		return;
	}

	const bool bInOnlineLevel = World->URL.HasOption(OnlineOption);
	FString Wanted;
	if (IsOnARoundsMap(GetState()) && !MapToLoad.IsEmpty() && !HasFlag(EScctOnlineStatusFlags::RoundRefused))
	{
		const FString Level = LevelOf(MapToLoad);
		if (!IsPlayable(MapToLoad))
		{
			// The simulation could play it, but there is nothing to draw: the visit ends with the reason.
			UE_LOG(LogOpenSCCT, Error, TEXT("Online: the server plays %s, which has no imported level (%s) and no render files to build one from"), *MapToLoad, *Level);
			Api->Leave();
			Notice = FString::Printf(TEXT("The server plays %s, which this game can neither find imported nor build from its render files."), *MapToLoad);
			Touch();
			return;
		}
		if (!IsLevelOf(*World, MapToLoad) || !bInOnlineLevel)
		{
			Wanted = FString::Printf(TEXT("%s?%s=1"), *Level, OnlineOption);
		}
	}
	else if (bInOnlineLevel && GetState() == EScctOnlineState::Failed)
	{
		// The connection was lost or refused in the round: the level's play error page says why and stays until the
		// player leaves it (Leave dismisses the failure, and the next frame goes back to the front end).
		TravelDelay = 0.0f;
		return;
	}
	else if (bInOnlineLevel)
	{
		// Back in the lobby, or the visit is over: a level that was opened for a round is left.
		Wanted = FrontEndLevel;
	}

	if (Wanted.IsEmpty())
	{
		TravelDelay = 0.0f;
		return;
	}

	TravelDelay += World->GetDeltaSeconds();
	if (TravelDelay >= TravelNoticeSeconds)
	{
		OpenLevel(Wanted);
	}
}

void UScctOnlineSubsystem::ReportLoadedWhenReady()
{
	if (!HasFlag(EScctOnlineStatusFlags::RoundActive) || !HasFlag(EScctOnlineStatusFlags::NeedsLoadReport) || !PendingLevel.IsEmpty())
	{
		return;
	}

	const UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || !World->HasBegunPlay() || World->GetTimeSeconds() < LoadedAfterSeconds
		|| !World->URL.HasOption(OnlineOption) || !IsLevelOf(*World, MapToLoad))
	{
		return;
	}

	// The level's session subsystem must be presenting this very round: then the player sees it.
	const UScctSessionSubsystem* Session = World->GetSubsystem<UScctSessionSubsystem>();
	if (Session == nullptr || !Session->IsRemoteSession() || Session->GetSessionId() == ReportedSessionId)
	{
		return;
	}

	ReportedSessionId = Session->GetSessionId();
	Api->ReportLoaded();
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: level %s is up and presents remote session %d: told the server this player is in (ReportLoaded)"), *MapToLoad, ReportedSessionId);
}

// ---- unattended ---------------------------------------------------------------------------------------------------

void UScctOnlineSubsystem::RunUnattended(float DeltaSeconds)
{
	const UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || !World->HasBegunPlay())
	{
		return;
	}

	if (!bAutoListed && FParse::Param(FCommandLine::Get(), TEXT("ScctListServers")))
	{
		bAutoListed = true;
		RequestServerList();
	}
	if (!bAutoJoined && !AutoJoin.IsEmpty())
	{
		bAutoJoined = true;
		Join(AutoJoin, FString(), AutoSide);
	}
	RunUnattendedHost();
	if (Api == nullptr)
	{
		return;
	}

	if (GetState() == EScctOnlineState::Lobby && Status.LocalId >= 0)
	{
		if (bAutoReady && !HasFlag(EScctOnlineStatusFlags::LocalReady))
		{
			// Asked once per change of the lobby, not once per frame.
			if (AutoReadyRevision != Status.Revision)
			{
				AutoReadyRevision = Status.Revision;
				UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctAutoReady: saying ready (%d players in the lobby)"), Status.PlayerCount);
				Api->SetReady(1);
			}
		}
		if (!bAutoChatSent && !AutoChat.IsEmpty())
		{
			bAutoChatSent = true;
			SendChat(AutoChat);
		}
	}
	RunUnattendedModeration(DeltaSeconds);

	if (MatchSeconds >= 0.0f && !bLeftOnTimer && LeaveAfter > 0.0f)
	{
		MatchSeconds += DeltaSeconds;
		if (MatchSeconds >= LeaveAfter)
		{
			bLeftOnTimer = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctLeaveAfter: leaving %.0f s into the round"), MatchSeconds);
			Leave();
		}
	}
}

void UScctOnlineSubsystem::LogStatus(float DeltaSeconds)
{
	if (LogEvery <= 0.0f || GetState() == EScctOnlineState::Idle)
	{
		return;
	}
	LogTimer -= DeltaSeconds;
	if (LogTimer > 0.0f)
	{
		return;
	}
	LogTimer = LogEvery;

	UE_LOG(LogOpenSCCT, Log, TEXT("Online: %s | server phase %d flags 0x%x | '%s' %s on %s | players %d/%d local id %d team %d | countdown %.1f | map to load '%s'"),
		StateName(GetState()), Status.Phase, Status.Flags, *ServerName, *ModeName, *SetupMap, Status.PlayerCount, Status.MaxPlayers,
		Status.LocalId, Status.LocalTeam, Status.Countdown, *MapToLoad);
	LogHostStatus();

	// During a round: where the simulation's pawns are, so the log shows the other players moving.
	const UWorld* World = GetGameInstance()->GetWorld();
	const UScctSessionSubsystem* Session = World != nullptr ? World->GetSubsystem<UScctSessionSubsystem>() : nullptr;
	if (Session == nullptr || !Session->IsRemoteSession())
	{
		return;
	}
	const FScctBridgeSessionInfo& Info = Session->GetSessionInfo();
	FString Line = FString::Printf(TEXT("Online: remote session %d on %s, %d client ticks, %d pawns:"), Info.Id, Info.Map, Info.Ticks, Session->GetPawns().Num());
	for (const FScctBridgePawn& Pawn : Session->GetPawns())
	{
		Line += FString::Printf(TEXT(" #%d %s team %d%s hp %d at (%.0f, %.0f, %.0f)"), Pawn.ActorId, Pawn.Kind == 1 ? TEXT("merc") : TEXT("spy"), Pawn.Team,
			(Pawn.Flags & EScctPawnFlags::Local) != 0 ? TEXT(" LOCAL") : TEXT(""), Pawn.Health, Pawn.X, Pawn.Y, Pawn.Z);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("%s"), *Line);
}
