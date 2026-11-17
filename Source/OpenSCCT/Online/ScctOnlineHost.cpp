// Hosting from the game and the Online settings page: the part of UScctOnlineSubsystem that runs the player's own
// server (Runtime/ScctHostApi.h). The bridge's listen server runs the session on its own thread and the local player
// joins it over loopback as the lobby's host, so once Host returned the rest of the subsystem sees an ordinary visit.

#include "Online/ScctOnlineSubsystem.h"

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"

#define LOCTEXT_NAMESPACE "ScctOnlineHost"

namespace
{
	// The project's settings (Config/DefaultGame.ini): the master server and the invite key every build ships. They
	// are the Online page's defaults; a value the player set on the page wins.
	const TCHAR* const ProjectSection = TEXT("OpenSCCT.Online");

	const TCHAR* const OnlinePage = TEXT("Online");
	const TCHAR* const KeyMasterUrl = TEXT("Online.MasterUrl");
	const TCHAR* const KeyInviteKey = TEXT("Online.InviteKey");
	const TCHAR* const KeyConnectMode = TEXT("Online.ConnectMode");
	const TCHAR* const KeySavedServers = TEXT("Online.SavedServers");
	const TCHAR* const KeyLanPort = TEXT("Online.LanPort");
	const TCHAR* const KeyHostPort = TEXT("Online.HostPort");
	const TCHAR* const KeyHostMaxPlayers = TEXT("Online.HostMaxPlayers");
	const TCHAR* const KeyHostListed = TEXT("Online.HostListed");
	const TCHAR* const KeyHostUpnp = TEXT("Online.HostUpnp");
	const TCHAR* const KeyHostRequireAccounts = TEXT("Online.HostRequireAccounts");
	const TCHAR* const KeyHostRecordDemos = TEXT("Online.HostRecordDemos");

	// OpenSCCT's defaults (BuildInfo.DefaultGamePort, DefaultLanPort, LobbySetup.DefaultMaxPlayers).
	constexpr int32 DefaultGamePort = 28888;
	constexpr int32 DefaultLanPort = 28889;
	constexpr int32 DefaultMaxPlayers = 24;

	FString ProjectValue(const TCHAR* Key)
	{
		FString Value;
		if (GConfig != nullptr)
		{
			GConfig->GetString(ProjectSection, Key, Value, GGameIni);
		}
		return Value;
	}

	const TCHAR* TypeName(EScctSettingType Type)
	{
		switch (Type)
		{
		case EScctSettingType::Bool: return TEXT("bool");
		case EScctSettingType::Float: return TEXT("float");
		case EScctSettingType::Int: return TEXT("int");
		case EScctSettingType::Choice: return TEXT("choice");
		default: return TEXT("text");
		}
	}
}

// ---- the Online settings page ---------------------------------------------------------------------------------------

void UScctOnlineSubsystem::RegisterOnlineSettings()
{
	static bool bRegistered = false;
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;

	int32 Order = 0;
	auto Add = [&Order](const TCHAR* Key, const FText& Group, const FText& Label, const FText& Tooltip, EScctSettingType Type,
		const FString& Default, float Min = 0.f, float Max = 1.f, TArray<FText> Choices = {})
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = OnlinePage;
		Desc.Group = Group;
		Desc.Label = Label;
		Desc.Tooltip = Tooltip;
		Desc.Type = Type;
		Desc.Default = Default;
		Desc.Min = Min;
		Desc.Max = Max;
		Desc.Step = Type == EScctSettingType::Int ? 1.f : 0.f;
		Desc.Choices = MoveTemp(Choices);
		Desc.Order = Order++;
		FScctSettings::Register(Desc);
	};

	const FText Servers = LOCTEXT("GroupServers", "Finding servers");
	const FText Hosting = LOCTEXT("GroupHosting", "Hosting");

	// Godot's online.cfg [master] and [browser] (game/Scripts/Online/OnlineSettings.cs).
	Add(KeyMasterUrl, Servers, LOCTEXT("MasterUrl", "Master server"),
		LOCTEXT("MasterUrlTip", "The master server's address (http or https). It lists servers, introduces players to hosts behind a router and relays those it cannot introduce. Empty: LAN and typed addresses only."),
		EScctSettingType::Text, ProjectValue(TEXT("MasterUrl")));
	Add(KeyInviteKey, Servers, LOCTEXT("InviteKey", "Invite key"),
		LOCTEXT("InviteKeyTip", "The key a private master server asks for. Hosting registers with the same key."),
		EScctSettingType::Text, ProjectValue(TEXT("InviteKey")));
	Add(KeyConnectMode, Servers, LOCTEXT("ConnectMode", "Connect mode"),
		LOCTEXT("ConnectModeTip", "How a listed server is reached: Auto tries what the master suggests (the host's own network, its public address, a NAT punch, the relay); the others try that way only."),
		EScctSettingType::Choice, TEXT("0"), 0.f, 3.f,
		{ LOCTEXT("ConnectAuto", "Auto"), LOCTEXT("ConnectDirect", "Direct"), LOCTEXT("ConnectPunch", "NAT punch"), LOCTEXT("ConnectRelay", "Relay") });
	Add(KeySavedServers, Servers, LOCTEXT("SavedServers", "Saved servers"),
		LOCTEXT("SavedServersTip", "Server addresses the server browser always asks (host or host:port), separated by ';'."),
		EScctSettingType::Text, FString());
	Add(KeyLanPort, Servers, LOCTEXT("LanPort", "LAN port"),
		LOCTEXT("LanPortTip", "The UDP port LAN searches are sent to, and the port a hosted game answers them on."),
		EScctSettingType::Int, FString::FromInt(DefaultLanPort), 1024.f, 65535.f);

	// Godot's online.cfg [host] (Create Session's hosting options).
	Add(KeyHostPort, Hosting, LOCTEXT("HostPort", "Host UDP port"),
		LOCTEXT("HostPortTip", "The UDP port a hosted game listens on. Forward it on the router when UPnP cannot."),
		EScctSettingType::Int, FString::FromInt(DefaultGamePort), 1024.f, 65535.f);
	Add(KeyHostMaxPlayers, Hosting, LOCTEXT("HostMaxPlayers", "Max players"),
		LOCTEXT("HostMaxPlayersTip", "Player places of a hosted game, both sides together."),
		EScctSettingType::Int, FString::FromInt(DefaultMaxPlayers), 2.f, 24.f);
	Add(KeyHostListed, Hosting, LOCTEXT("HostListed", "Listed on the master"),
		LOCTEXT("HostListedTip", "Show a hosted game in everyone's server list. Off: players join by address or LAN only."),
		EScctSettingType::Bool, TEXT("true"));
	Add(KeyHostUpnp, Hosting, LOCTEXT("HostUpnp", "UPnP"),
		LOCTEXT("HostUpnpTip", "Ask the router to forward the host port while hosting."),
		EScctSettingType::Bool, TEXT("true"));
	Add(KeyHostRequireAccounts, Hosting, LOCTEXT("HostRequireAccounts", "Signed-in players only"),
		LOCTEXT("HostRequireAccountsTip", "Only players signed in on the master may join a hosted game."),
		EScctSettingType::Bool, TEXT("false"));
	Add(KeyHostRecordDemos, Hosting, LOCTEXT("HostRecordDemos", "Record demos"),
		LOCTEXT("HostRecordDemosTip", "Record every round of a hosted game as a demo in Saved/Demos."),
		EScctSettingType::Bool, TEXT("false"));

	if (FParse::Param(FCommandLine::Get(), TEXT("ScctOnlineSettingsDump")))
	{
		for (const FScctSettingDesc* Desc : FScctSettings::OptionsOf(OnlinePage))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Settings: [%s / %s] %s '%s' (%s) = '%s'"), *Desc->Page.ToString(), *Desc->Group.ToString(),
				*Desc->Key.ToString(), *Desc->Label.ToString(), TypeName(Desc->Type),
				// The invite key is a secret of the master: its length only.
				Desc->Key == FName(KeyInviteKey) ? *FString::Printf(TEXT("<%d characters>"), FScctSettings::GetString(Desc->Key).Len()) : *FScctSettings::GetString(Desc->Key));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Settings: pages %s"), *FString::JoinBy(FScctSettings::Pages(), TEXT(", "), [](FName Page) { return Page.ToString(); }));
	}
}

void UScctOnlineSubsystem::ReadMasterSettings(FString& OutUrl, FString& OutInviteKey)
{
	RegisterOnlineSettings();
	OutUrl = FScctSettings::GetString(KeyMasterUrl).TrimStartAndEnd();
	OutInviteKey = FScctSettings::GetString(KeyInviteKey).TrimStartAndEnd();
}

FScctHostRequest FScctHostRequest::Default()
{
	UScctOnlineSubsystem::RegisterOnlineSettings();
	FScctHostRequest Request;
	Request.Port = FScctSettings::GetInt(KeyHostPort);
	Request.MaxPlayers = FScctSettings::GetInt(KeyHostMaxPlayers);
	Request.LanPort = FScctSettings::GetInt(KeyLanPort);
	Request.bListed = FScctSettings::GetBool(KeyHostListed);
	Request.bUpnp = FScctSettings::GetBool(KeyHostUpnp);
	Request.bRequireAccounts = FScctSettings::GetBool(KeyHostRequireAccounts);
	Request.bRecordDemos = FScctSettings::GetBool(KeyHostRecordDemos);
	return Request;
}

void FScctHostRequest::Remember() const
{
	UScctOnlineSubsystem::RegisterOnlineSettings();
	if (Port > 0)
	{
		FScctSettings::SetInt(KeyHostPort, Port);
	}
	FScctSettings::SetInt(KeyHostMaxPlayers, MaxPlayers);
	FScctSettings::SetBool(KeyHostListed, bListed);
	FScctSettings::SetBool(KeyHostUpnp, bUpnp);
	FScctSettings::SetBool(KeyHostRequireAccounts, bRequireAccounts);
	FScctSettings::SetBool(KeyHostRecordDemos, bRecordDemos);
	FScctSettings::Save();
}

void UScctOnlineSubsystem::HandleSettingChanged(FName Key)
{
	if (Key == FName(KeyMasterUrl) || Key == FName(KeyInviteKey))
	{
		ApplyMasterSettings();
	}
	else if (Key == FName(KeyConnectMode))
	{
		ApplyConnectMode();
	}
}

void UScctOnlineSubsystem::ApplyMasterSettings()
{
	if (Api == nullptr)
	{
		// The bridge is not up yet: Connect reads the page when it is.
		return;
	}

	FString Url, InviteKey;
	ReadMasterSettings(Url, InviteKey);
	// An empty address leaves the bridge without a master (its state reads as failed): LAN and addresses only.
	Api->SetMaster(*Url, *InviteKey);
	bMasterSet = !Url.IsEmpty();
	Servers.Reset();
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: master server is now %s"), bMasterSet ? *Url : TEXT("none"));
	Touch();
}

void UScctOnlineSubsystem::ApplyConnectMode()
{
	if (Api == nullptr || ConnectHost() == nullptr)
	{
		return;
	}
	HostApi->SetConnectPath(FScctSettings::GetInt(KeyConnectMode));
}

// ---- hosting --------------------------------------------------------------------------------------------------------

const FScctHostApi* UScctOnlineSubsystem::ConnectHost()
{
	if (HostApi != nullptr)
	{
		return HostApi;
	}

	FString Error;
	HostApi = ScctFeature<FScctHostApi>(&Error);
	if (HostApi == nullptr)
	{
		Notice = Error;
		Touch();
	}
	return HostApi;
}

bool UScctOnlineSubsystem::Host(const FScctHostRequest& Request, FString* OutError)
{
	if (Connect() == nullptr || ConnectHost() == nullptr)
	{
		return false;
	}

	Notice.Reset();
	Chat.Reset();
	int32 Flags = 0;
	Flags |= Request.bListed ? EScctHostFlags::Listed : 0;
	Flags |= Request.bUpnp ? EScctHostFlags::Upnp : 0;
	Flags |= Request.bRequireAccounts ? EScctHostFlags::RequireAccounts : 0;
	Flags |= Request.bRecordDemos ? EScctHostFlags::RecordDemos : 0;
	Flags |= Request.bLan ? EScctHostFlags::Lan : 0;
	Flags |= Request.bLoopbackOnly ? EScctHostFlags::LoopbackOnly : 0;

	FString DemoFolder;
	if (Request.bRecordDemos)
	{
		DemoFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Demos"));
		IFileManager::Get().MakeDirectory(*DemoFolder, true);
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Online: hosting '%s' on UDP port %d (%s, %d players, flags 0x%x, LAN port %d)"), *Request.Name, Request.Port,
		*Request.SetupOptions, Request.MaxPlayers, Flags, Request.LanPort);
	const int32 Port = HostApi->Host(*Request.Name, Request.Password.IsEmpty() ? nullptr : *Request.Password, Request.Port, Request.MaxPlayers,
		*Request.SetupOptions, Flags, Request.LanPort, DemoFolder.IsEmpty() ? nullptr : *DemoFolder);
	if (Port <= 0)
	{
		Notice = ReadText(EScctOnlineText::Error);
		if (Notice.IsEmpty())
		{
			Notice = TEXT("The game could not be hosted.");
		}
		UE_LOG(LogOpenSCCT, Warning, TEXT("Online: %s"), *Notice);
		TickHost();
		if (OutError != nullptr)
		{
			// The page that asked shows the reason; the bridge's failed visit would otherwise take the screen over.
			*OutError = Notice;
			Api->Leave();
			Notice.Reset();
		}
		Touch();
		return false;
	}

	HostPort = Port;
	bAutoLaunched = false;
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: the host listens on UDP port %d; the local player joins it as the host"), HostPort);
	Touch();
	return true;
}

void UScctOnlineSubsystem::StopHosting()
{
	if (IsHosting())
	{
		// The visit to the player's own server ends with it, as in the Godot client.
		Leave();
	}
}

bool UScctOnlineSubsystem::HostCommand(const FString& Line)
{
	const FString Trimmed = Line.TrimStartAndEnd();
	return HostApi != nullptr && IsHosting() && !Trimmed.IsEmpty() && HostApi->HostCommand(*Trimmed) != 0;
}

FString UScctOnlineSubsystem::GetHostText(EScctHostText Kind) const
{
	if (HostApi == nullptr)
	{
		return FString();
	}

	TCHAR Short[512];
	const int32 Length = HostApi->GetHostText(static_cast<int32>(Kind), Short, UE_ARRAY_COUNT(Short));
	if (Length < UE_ARRAY_COUNT(Short))
	{
		return FString(Short);
	}

	TArray<TCHAR> Long;
	Long.SetNumZeroed(Length + 1);
	HostApi->GetHostText(static_cast<int32>(Kind), Long.GetData(), Long.Num());
	return FString(Long.GetData());
}

void UScctOnlineSubsystem::TickHost()
{
	if (HostApi == nullptr)
	{
		return;
	}

	TCHAR Line[1024];
	for (int32 Taken = 0; Taken < 64 && HostApi->TakeHostLine(Line, UE_ARRAY_COUNT(Line)) >= 0; ++Taken)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Online host: %s"), Line);
	}

	if (HostPort > 0 && HostApi->IsHosting() == 0)
	{
		// The bridge closed the server with the visit (a failure, a join elsewhere): the visit says why.
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: the server on UDP port %d is closed"), HostPort);
		HostPort = 0;
		Touch();
	}
}

void UScctOnlineSubsystem::LogHostStatus()
{
	if (!IsHosting())
	{
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online host: UDP port %d | %s | players: %s | %s | %s"), HostPort, *GetHostText(EScctHostText::Status),
		*GetHostText(EScctHostText::Players), *GetHostText(EScctHostText::Master), *GetHostText(EScctHostText::Upnp));
}

// ---- unattended hosting ---------------------------------------------------------------------------------------------

void UScctOnlineSubsystem::ReadHostSwitches()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	int32 Port = 0;
	if (FParse::Value(CommandLine, TEXT("ScctHost="), Port))
	{
		AutoHostPort = FMath::Clamp(Port, 0, 65535);
	}
	else if (FParse::Param(CommandLine, TEXT("ScctHost")))
	{
		// No port: the Online page's host port (0 here means "ask the page").
		AutoHostPort = 0;
	}
	FParse::Value(CommandLine, TEXT("ScctHostMap="), AutoHostMap);
	FParse::Value(CommandLine, TEXT("ScctHostMode="), AutoHostMode);
	FParse::Value(CommandLine, TEXT("ScctHostName="), AutoHostName, false);
	FParse::Value(CommandLine, TEXT("ScctHostPassword="), AutoHostPassword);
	bAutoHostPublic = FParse::Param(CommandLine, TEXT("ScctHostPublic"));
	bLaunchWhenGiven = FParse::Value(CommandLine, TEXT("ScctLaunchWhen="), LaunchWhen);
	LaunchWhen = FMath::Max(1, LaunchWhen);
}

void UScctOnlineSubsystem::RunUnattendedHost()
{
	if (AutoHostPort >= 0 && !bAutoHosted)
	{
		bAutoHosted = true;
		FScctHostRequest Request = FScctHostRequest::Default();
		if (AutoHostPort > 0)
		{
			Request.Port = AutoHostPort;
		}
		Request.Name = AutoHostName.IsEmpty() ? TEXT("Test round") : AutoHostName;
		Request.Password = AutoHostPassword;
		Request.MaxPlayers = FMath::Max(Request.MaxPlayers, LaunchWhen);
		Request.SetupOptions = FString::Printf(TEXT("?Mode=%s"), AutoHostMode.IsEmpty() ? TEXT("Story") : *AutoHostMode);
		if (!AutoHostMap.IsEmpty())
		{
			Request.SetupOptions = FString::Printf(TEXT("?MapName=%s%s"), *AutoHostMap, *Request.SetupOptions);
		}
		if (!bAutoHostPublic)
		{
			// As Godot's command-line round: loopback only and unlisted, so a test run raises no firewall prompt and
			// touches neither the router nor the master.
			Request.bLoopbackOnly = true;
			Request.bListed = false;
			Request.bUpnp = false;
			Request.bLan = false;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctHost: hosting %s, launching once %d player(s) are in the lobby"), *Request.SetupOptions, LaunchWhen);
		Host(Request);
	}

	// The side and the launch below are for unattended runs only: a game hosted from Create Session waits for its host.
	if (!(bAutoHosted || bLaunchWhenGiven) || !IsHosting() || Api == nullptr || GetState() != EScctOnlineState::Lobby || Status.LocalId < 0)
	{
		return;
	}

	// -ScctSide: the host's own side, asked once per change of the lobby.
	if (AutoSide >= 0 && Status.LocalTeam != AutoSide)
	{
		if (AutoSideRevision != Status.Revision)
		{
			AutoSideRevision = Status.Revision;
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctSide: the host takes side %d"), AutoSide);
			SetTeam(AutoSide);
		}
		return;
	}

	if (!bAutoLaunched && Status.PlayerCount >= LaunchWhen && HasFlag(EScctOnlineStatusFlags::CanStart))
	{
		bAutoLaunched = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctLaunchWhen: launching %s with %d player(s): %s"), *SetupMap, Status.PlayerCount,
			*GetHostText(EScctHostText::Players));
		Launch();
	}
}

#undef LOCTEXT_NAMESPACE
