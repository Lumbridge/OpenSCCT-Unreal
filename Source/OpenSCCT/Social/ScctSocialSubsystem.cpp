#include "Social/ScctSocialSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Hub/ScctHubSubsystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"
#include "Demo/ScctDemoPlayback.h"
#include "Social/ScctCrashContext.h"
#include "Social/SScctSocialToast.h"

const TCHAR* const UScctSocialSubsystem::DiscordStatusSetting = TEXT("Online.DiscordStatus");

namespace
{
	/** Seconds between two descriptions of the player's state (DiscordPresence.UpdateSeconds). */
	constexpr float PresenceSeconds = 1.0f;

	/** Above the front end, the HUD and the hub's loading cover. */
	constexpr int32 ToastZOrder = 1100;

	const TCHAR* DiscordStateName(int32 State)
	{
		switch (static_cast<EScctDiscordState>(State))
		{
		case EScctDiscordState::Connecting: return TEXT("connecting");
		case EScctDiscordState::NotRunning: return TEXT("not running");
		case EScctDiscordState::Ready: return TEXT("ready");
		default: return TEXT("off");
		}
	}

	FString ReadBuffer(int32 (*Read)(TCHAR*, int32))
	{
		TCHAR Short[512];
		const int32 Length = Read(Short, UE_ARRAY_COUNT(Short));
		if (Length < 0)
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

UScctSocialSubsystem* UScctSocialSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctSocialSubsystem>() : nullptr;
}

void UScctSocialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The online subsystem is up first: a join this subsystem asks for runs on its visit.
	Collection.InitializeDependency<UScctOnlineSubsystem>();
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	// The website starts the game with --svm-join-server=<id>; -svm-join-server=<id> is Unreal's own form of it.
	FParse::Value(CommandLine, TEXT("svm-join-server="), StartupWebJoin);
	FParse::Value(CommandLine, TEXT("ScctWebJoin="), AutoWebJoin);
	FParse::Value(CommandLine, TEXT("ScctDiscordJoin="), AutoDiscordJoin);
	bForceDiscord = FParse::Param(CommandLine, TEXT("ScctDiscord"));
	bRegister = FParse::Param(CommandLine, TEXT("ScctDiscordRegister"));
	bLease = !FParse::Param(CommandLine, TEXT("ScctNoPresenceLease"));
	bVerbose = FApp::IsUnattended() || FParse::Param(CommandLine, TEXT("ScctSocialLog"));
	FParse::Value(CommandLine, TEXT("ScctDiscordToggle="), ToggleAt);

	FScctSettingDesc Discord;
	Discord.Key = DiscordStatusSetting;
	Discord.Page = TEXT("Online");
	Discord.Group = NSLOCTEXT("ScctSocial", "DiscordGroup", "Discord");
	Discord.Label = NSLOCTEXT("ScctSocial", "DiscordStatus", "Discord status");
	Discord.Tooltip = NSLOCTEXT("ScctSocial", "DiscordStatusTip",
		"Shows what you are doing in OpenSCCT on your Discord profile (through the Discord app on this PC) and lets your friends join your game from it.");
	Discord.Type = EScctSettingType::Bool;
	Discord.Default = TEXT("true");
	FScctSettings::Register(Discord);
	SettingHandle = FScctSettings::OnChanged().AddUObject(this, &UScctSocialSubsystem::HandleSettingChanged);

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctSocialSubsystem::Tick));
}

void UScctSocialSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	FScctSettings::OnChanged().Remove(SettingHandle);
	if (Api != nullptr && bDiscordOn)
	{
		// The status goes when the game closes, not when Discord notices the pipe closed.
		Api->SetDiscord(0, TEXT(""), TEXT(""));
		bDiscordOn = false;
		DrainLog();
	}
	if (Toast.IsValid() && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Toast.ToSharedRef());
	}
	Toast.Reset();
	Api = nullptr;
	Super::Deinitialize();
}

const FScctSocialApi* UScctSocialSubsystem::Connect()
{
	bConnectTried = true;

	// The website join and a friend's Join go through the online bridge's master and sign-in.
	if (UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>(); Online == nullptr || !Online->EnsureReady())
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Social: the online subsystem is not up: website joins and friends' Joins cannot start"));
	}

	FString Error;
	Api = ScctFeature<FScctSocialApi>(&Error);
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Social: no Discord status, website join or presence lease: %s"), *Error);
		return nullptr;
	}

	Api->Start(*StartupWebJoin, bLease ? EScctSocialStartFlags::Lease : 0);
	ApplyDiscord();
	if (!AutoDiscordJoin.IsEmpty())
	{
		Api->SimulateDiscordJoin(*AutoDiscordJoin);
	}
	DrainLog();
	return Api;
}

void UScctSocialSubsystem::HandleSettingChanged(FName Key)
{
	if (Key == DiscordStatusSetting && Api != nullptr)
	{
		ApplyDiscord();
		DrainLog();
	}
}

bool UScctSocialSubsystem::WantsDiscord() const
{
	if (!FScctSettings::GetBool(DiscordStatusSetting))
	{
		return false;
	}
	// Runs with nobody at the keyboard must not touch the player's Discord.
	const bool bUnattended = FApp::IsUnattended() || FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"));
	return bForceDiscord || !bUnattended;
}

void UScctSocialSubsystem::ApplyDiscord()
{
	const bool bWanted = WantsDiscord();
	if (bWanted == bDiscordOn)
	{
		return;
	}

	FString Exe, Command;
	if (bWanted)
	{
		FindLaunchCommand(Exe, Command);
	}
	Api->SetDiscord(bWanted ? 1 : 0, *Exe, *Command);
	bDiscordOn = bWanted;
	PresenceTimer = PresenceSeconds;
	UE_LOG(LogOpenSCCT, Log, TEXT("Social: Discord status %s%s"), bWanted ? TEXT("on") : TEXT("off"),
		bWanted && Command.IsEmpty() ? TEXT(" (not registered for friends' Joins in a development run; -ScctDiscordRegister does it)") : TEXT(""));
}

void UScctSocialSubsystem::FindLaunchCommand(FString& OutExe, FString& OutCommand) const
{
	OutExe = FPaths::ConvertRelativePathToFull(FPlatformProcess::ExecutablePath());
	FPaths::MakePlatformFilename(OutExe);
#if WITH_EDITOR
	// A development run of the editor's executable: registered only when asked, with the project and -game, and the
	// windowed editor rather than its console twin.
	if (!bRegister)
	{
		OutExe.Reset();
		OutCommand.Reset();
		return;
	}
	OutExe.ReplaceInline(TEXT("UnrealEditor-Cmd.exe"), TEXT("UnrealEditor.exe"), ESearchCase::IgnoreCase);
	FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	FPaths::MakePlatformFilename(Project);
	OutCommand = FString::Printf(TEXT("\"%s\" \"%s\" -game"), *OutExe, *Project);
#else
	OutCommand = FString::Printf(TEXT("\"%s\""), *OutExe);
#endif
}

bool UScctSocialSubsystem::IsFrontEndOpen() const
{
	const UWorld* World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	return World != nullptr && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == UScctOnlineSubsystem::FrontEndLevel;
}

int32 UScctSocialSubsystem::FindReadiness() const
{
	if (!IsFrontEndOpen())
	{
		return 0;
	}

	int32 Readiness = EScctSocialReadiness::InMenus;
	const UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
	const UScctHubSubsystem* Hub = GetGameInstance()->GetSubsystem<UScctHubSubsystem>();
	const bool bOnlineIdle = Online == nullptr || !Online->IsVisiting();
	const bool bHubIdle = Hub == nullptr || ((Hub->GetState() == EScctHubState::Idle || Hub->GetState() == EScctHubState::Failed) && !Hub->HasFlag(EScctHubStatusFlags::Hosting));
	if (bOnlineIdle && bHubIdle)
	{
		Readiness |= EScctSocialReadiness::Idle;
	}
	return Readiness;
}

bool UScctSocialSubsystem::Tick(float DeltaSeconds)
{
	if (Api == nullptr)
	{
		if (bConnectTried || Connect() == nullptr)
		{
			return true;
		}
	}

	if (!bAutoQueued && !AutoWebJoin.IsEmpty())
	{
		bAutoQueued = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: a website join of %s as if it came over the pipe (-ScctWebJoin)"), *AutoWebJoin);
		Api->QueueWebJoin(*AutoWebJoin);
	}

	Api->Update(DeltaSeconds, FindReadiness());
	DrainLog();
	HandleRequests();
	Api->GetStatus(&Status);
	if (Status.Discord != LastDiscordState)
	{
		LastDiscordState = Status.Discord;
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: Discord %s%s"), DiscordStateName(Status.Discord),
			Status.Discord == static_cast<int32>(EScctDiscordState::Ready) && Status.DiscordCanJoin != 0 ? TEXT(", friends can join") : TEXT(""));
	}

	// -ScctDiscordToggle: the option goes off and on again through the settings, as the Online page would set it.
	RunSeconds += DeltaSeconds;
	if (ToggleAt >= 0.0f && ToggleStep < 2 && RunSeconds >= ToggleAt + ToggleStep * 5.0f)
	{
		const bool bOn = ToggleStep == 1;
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: the Discord status option set %s (-ScctDiscordToggle)"), bOn ? TEXT("on") : TEXT("off"));
		++ToggleStep;
		FScctSettings::SetBool(DiscordStatusSetting, bOn);
	}

	PresenceTimer += DeltaSeconds;
	if (PresenceTimer >= PresenceSeconds)
	{
		PresenceTimer = 0.0f;
		SendPresence();
	}
	return true;
}

void UScctSocialSubsystem::SendPresence()
{
	FScctSocialPresence P = {};
	P.Side = -1;
	P.SpyScore = -1;
	P.MercScore = -1;
	P.Mode = -1;
	P.JoinKind = static_cast<int32>(EScctSocialJoinKind::None);
	FString Server, Map, JoinValue;

	const UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
	const UScctHubSubsystem* Hub = GetGameInstance()->GetSubsystem<UScctHubSubsystem>();
	const EScctHubState HubState = Hub != nullptr ? Hub->GetState() : EScctHubState::Idle;
	const EScctOnlineState OnlineState = Online != nullptr ? Online->GetState() : EScctOnlineState::Idle;
	const bool bOnline = OnlineState != EScctOnlineState::Idle && OnlineState != EScctOnlineState::Failed && OnlineState != EScctOnlineState::Left;
	const bool bHub = HubState == EScctHubState::Connecting || HubState == EScctHubState::Loading || HubState == EScctHubState::InHub;
	const UWorld* World = GetGameInstance()->GetWorld();
	const UScctSessionSubsystem* Session = World != nullptr ? World->GetSubsystem<UScctSessionSubsystem>() : nullptr;

	// The match's score and mode as the HUD reads them, for an online round and an offline match alike.
	const auto ReadRound = [&P]
	{
		if (const FScctHudApi* Hud = ScctFeature<FScctHudApi>())
		{
			FScctHudRound Round = {};
			if (Hud->GetRound(&Round) != 0 && Round.Valid != 0)
			{
				P.SpyScore = Round.TeamScore[0];
				P.MercScore = Round.TeamScore[1];
				P.Mode = Round.GameMode;
				return Round.TeamSize[0] + Round.TeamSize[1];
			}
		}
		return 0;
	};

	if (bOnline)
	{
		const FScctOnlineStatus& S = Online->GetStatus();
		P.Activity = static_cast<int32>(EScctSocialActivity::Online);
		switch (OnlineState)
		{
		case EScctOnlineState::Lobby: P.Phase = static_cast<int32>(EScctSocialPhase::Lobby); break;
		case EScctOnlineState::Match: P.Phase = static_cast<int32>(EScctSocialPhase::Match); break;
		case EScctOnlineState::Results: P.Phase = static_cast<int32>(EScctSocialPhase::Results); break;
		default: P.Phase = static_cast<int32>(EScctSocialPhase::Joining); break;
		}
		Server = Online->GetServerName();
		Map = Online->GetMapToLoad().IsEmpty() ? Online->GetSetupMap() : Online->GetMapToLoad();
		P.Mode = S.Mode;
		P.PartyMax = S.MaxPlayers;
		for (const FScctOnlinePlayer& Player : Online->GetPlayers())
		{
			if ((Player.Flags & EScctOnlinePlayerFlags::Bot) == 0)
			{
				++P.PartySize;
			}
			// The listen server's host is the player at the PC that hosts it.
			if (Player.Id == S.LocalId && (Player.Flags & EScctOnlinePlayerFlags::Host) != 0 && (S.Flags & EScctOnlineStatusFlags::Dedicated) == 0)
			{
				P.Flags |= EScctSocialPresenceFlags::Hosting;
			}
		}
		if (S.LocalTeam == 0 || S.LocalTeam == 1)
		{
			P.Side = S.LocalTeam;
		}
		else if (S.LocalTeam == 255)
		{
			P.Flags |= EScctSocialPresenceFlags::Spectating;
		}
		if ((S.Flags & EScctOnlineStatusFlags::RoundActive) != 0)
		{
			P.Flags |= EScctSocialPresenceFlags::RoundDrawn;
			ReadRound();
		}
		if (HubState == EScctHubState::InMatch)
		{
			P.Flags |= EScctSocialPresenceFlags::HubMatch;
		}

		FScctSocialJoin Join = {};
		TCHAR Value[256];
		Api->GetVisitJoin(&Join, Value, UE_ARRAY_COUNT(Value));
		P.JoinKind = Join.Kind;
		JoinValue = Value;
		if (Join.Password != 0)
		{
			P.Flags |= EScctSocialPresenceFlags::JoinPassword;
		}
	}
	else if (bHub)
	{
		const FScctHubStatus& S = Hub->GetStatus();
		P.Activity = static_cast<int32>(EScctSocialActivity::Hub);
		Server = Hub->GetHubName();
		JoinValue = Hub->GetAddress();
		P.JoinKind = static_cast<int32>(EScctSocialJoinKind::Hub);
		P.Side = S.Team;
		P.PartySize = S.PlayerCount;
		P.PartyMax = S.MaxPlayers;
		if (Hub->HasFlag(EScctHubStatusFlags::Hosting))
		{
			P.Flags |= EScctSocialPresenceFlags::Hosting;
		}
		if (HubState == EScctHubState::InHub)
		{
			P.Flags |= EScctSocialPresenceFlags::HubArrived;
		}
	}
	else if (Session != nullptr && Session->IsPlaying() && !Session->IsRemoteSession())
	{
		const FScctBridgeSessionInfo& Info = Session->GetSessionInfo();
		P.Activity = static_cast<int32>(EScctSocialActivity::Offline);
		P.Flags |= EScctSocialPresenceFlags::RoundDrawn;
		P.Side = (Info.Flags & EScctSessionFlags::Merc) != 0 ? 1 : 0;
		Map = Info.Map;
		// Everyone else in an offline match is a bot.
		P.Bots = FMath::Max(0, ReadRound() - 1);
	}
	else
	{
		P.Activity = static_cast<int32>(EScctSocialActivity::Menus);
		P.Page = static_cast<int32>(IsFrontEndOpen() ? FrontEndPage : EScctSocialPage::Other);
	}

	// Crash reports say the same: the map, the mode, the side and where.
	{
		const bool bDemo = World != nullptr && World->URL.HasOption(UScctDemoPlayback::DemoOption);
		const FString CrashMap = !Map.IsEmpty() || !bDemo ? Map : FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
		const FString Where = bDemo ? FString(TEXT("demo"))
			: P.Activity == static_cast<int32>(EScctSocialActivity::Online) ? TEXT("online server ") + Server
			: P.Activity == static_cast<int32>(EScctSocialActivity::Hub) ? TEXT("hub ") + Server
			: P.Activity == static_cast<int32>(EScctSocialActivity::Offline) ? FString(TEXT("offline"))
			: FString();
		ScctCrashContext::Update(CrashMap, P.Mode, P.Side, Where);
	}

	Api->SetPresence(&P, *Server, *Map, *JoinValue);
	const FString Text = GetPresenceText();
	if (Text != LastPresenceText)
	{
		LastPresenceText = Text;
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: status %s%s"), *Text, bDiscordOn ? TEXT("") : TEXT(" (not sent: the Discord status is off)"));
	}
}

FString UScctSocialSubsystem::GetPresenceText() const
{
	return Api != nullptr ? ReadBuffer(Api->GetPresenceText) : FString();
}

void UScctSocialSubsystem::DrainLog()
{
	if (Api == nullptr)
	{
		return;
	}

	// A line longer than the buffer is cut: these are log lines, not data.
	TCHAR Buffer[1024];
	for (int32 Guard = 0; Guard < 256; ++Guard)
	{
		if (Api->TakeLog(Buffer, UE_ARRAY_COUNT(Buffer)) < 0)
		{
			break;
		}
		const FString Line(Buffer);
		// Every status sent and every lease is for checking runs; a player's log keeps the rest.
		const bool bChatty = Line.StartsWith(TEXT("Discord: status ")) || Line.StartsWith(TEXT("Presence lease"));
		if (bChatty && !bVerbose)
		{
			UE_LOG(LogOpenSCCT, Verbose, TEXT("%s"), *Line);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("%s"), *Line);
		}
	}
}

void UScctSocialSubsystem::HandleRequests()
{
	FScctSocialRequest Request = {};
	TCHAR Value[512];
	TCHAR Text[1024];
	while (Api->TakeRequest(&Request, Value, UE_ARRAY_COUNT(Value), Text, UE_ARRAY_COUNT(Text)) != 0)
	{
		HandleRequest(Request, FString(Value), FString(Text));
	}
}

void UScctSocialSubsystem::HandleRequest(const FScctSocialRequest& Request, const FString& Value, const FString& Text)
{
	UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
	UScctHubSubsystem* Hub = GetGameInstance()->GetSubsystem<UScctHubSubsystem>();
	const bool bPassword = (Request.Flags & EScctSocialRequestFlags::NeedsPassword) != 0;
	const bool bRequireAccount = (Request.Flags & EScctSocialRequestFlags::RequireAccount) != 0;
	const TCHAR* From = (Request.Flags & EScctSocialRequestFlags::FromWebsite) != 0 ? TEXT("website join")
		: (Request.Flags & EScctSocialRequestFlags::FromDiscord) != 0 ? TEXT("friend's Join") : TEXT("join");

	switch (static_cast<EScctSocialRequest>(Request.Kind))
	{
	case EScctSocialRequest::JoinAddress:
		LeaveVisits(false, true);
		if (bPassword)
		{
			LeaveVisits(true, false);
			UE_LOG(LogOpenSCCT, Log, TEXT("Social: %s to %s asks for the server's password"), From, *Value);
			PendingPrompt = FScctSocialPasswordPrompt{ EScctSocialRequest::JoinAddress, Value, Value, Request.Flags };
			break;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: %s to the server at %s"), From, *Value);
		if (Online != nullptr && !Online->Join(Value))
		{
			ShowNotice(Online->GetNotice());
		}
		break;

	case EScctSocialRequest::JoinHub:
		LeaveVisits(true, false);
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: %s to the hub at %s"), From, *Value);
		PageToOpen = TEXT("hub");
		if (Hub != nullptr && !Hub->Join(Value, 0))
		{
			ShowNotice(Hub->GetNotice());
		}
		break;

	case EScctSocialRequest::JoinMaster:
		LeaveVisits(false, true);
		if (bPassword)
		{
			LeaveVisits(true, false);
			UE_LOG(LogOpenSCCT, Log, TEXT("Social: %s to %s (%s) asks for the server's password"), From, *Text, *Value);
			PendingPrompt = FScctSocialPasswordPrompt{ EScctSocialRequest::JoinMaster, Value, Text, Request.Flags };
			break;
		}
		JoinMaster(Value, Text, FString(), -1, bRequireAccount);
		break;

	case EScctSocialRequest::JoinHubListed:
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: %s to the hub %s (%s)"), From, *Text, *Value);
		PageToOpen = TEXT("hub");
		if (Hub != nullptr && !Hub->JoinListedId(Value, Text, bRequireAccount, 0))
		{
			ShowNotice(Hub->GetNotice());
		}
		break;

	case EScctSocialRequest::Notice:
		ShowNotice(Text);
		break;

	case EScctSocialRequest::SignIn:
		ShowNotice(Text);
		// The server list page is where the player signs in.
		PageToOpen = TEXT("servers");
		break;

	case EScctSocialRequest::SignedOut:
		if (Online != nullptr)
		{
			Online->SignOut();
		}
		break;

	case EScctSocialRequest::Quit:
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: the website join went to the game already running; quitting"));
		FPlatformMisc::RequestExit(false);
		break;

	default:
		break;
	}
}

void UScctSocialSubsystem::JoinMaster(const FString& Id, const FString& Name, const FString& Password, int32 Team, bool bRequireAccount)
{
	UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
	if (Online == nullptr || !Online->EnsureReady())
	{
		ShowNotice(Online != nullptr ? Online->GetNotice() : FString(TEXT("Online play is not available.")));
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Social: joining %s (%s) through the master%s"), *Name, *Id, bRequireAccount ? TEXT(" with the signed-in account") : TEXT(""));
	Api->JoinMaster(*Id, *Name, *Password, Team, bRequireAccount ? 1 : 0);
	DrainLog();
}

void UScctSocialSubsystem::LeaveVisits(bool bOnline, bool bHub)
{
	if (bOnline)
	{
		if (UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>(); Online != nullptr && Online->IsVisiting())
		{
			Online->Leave();
		}
	}
	if (bHub)
	{
		if (UScctHubSubsystem* Hub = GetGameInstance()->GetSubsystem<UScctHubSubsystem>(); Hub != nullptr && Hub->GetState() != EScctHubState::Idle)
		{
			Hub->Leave();
		}
	}
}

bool UScctSocialSubsystem::TakePasswordPrompt(FScctSocialPasswordPrompt& OutPrompt)
{
	if (!PendingPrompt.IsSet())
	{
		return false;
	}
	OutPrompt = PendingPrompt.GetValue();
	PendingPrompt.Reset();
	return true;
}

void UScctSocialSubsystem::SubmitPassword(const FScctSocialPasswordPrompt& Prompt, const FString& Password, int32 Team)
{
	if (Prompt.Kind == EScctSocialRequest::JoinMaster)
	{
		JoinMaster(Prompt.Value, Prompt.Name, Password, Team, (Prompt.Flags & EScctSocialRequestFlags::RequireAccount) != 0);
	}
	else if (UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Social: joining the server at %s with its password"), *Prompt.Value);
		Online->Join(Prompt.Value, Password, Team);
	}
}

FString UScctSocialSubsystem::TakePageToOpen()
{
	FString Page = MoveTemp(PageToOpen);
	PageToOpen.Reset();
	return Page;
}

void UScctSocialSubsystem::ShowNotice(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Social: notice: %s"), *Text);
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	// A level change empties the viewport: the toast is put back on each notice.
	if (!Toast.IsValid())
	{
		Toast = SNew(SScctSocialToast);
	}
	GEngine->GameViewport->RemoveViewportWidgetContent(Toast.ToSharedRef());
	GEngine->GameViewport->AddViewportWidgetContent(Toast.ToSharedRef(), ToastZOrder);
	Toast->Show(Text);
}
