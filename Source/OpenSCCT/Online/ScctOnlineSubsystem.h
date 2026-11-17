#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctHostApi.h"
#include "Runtime/ScctOnlineApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctOnlineSubsystem.generated.h"

class UScctSessionSubsystem;

/**
 * A game the player hosts (Create Session): the server's name and password, the first match and the hosting
 * choices. Default() fills the hosting choices from the Online settings page.
 */
struct FScctHostRequest
{
	FString Name;
	FString Password;
	/** The first match as an options line: "?MapName=AquaE?Mode=Story?TimeLimit=10" (see FScctHostApi::Host). */
	FString SetupOptions;
	/** UDP port; 0 lets the system pick one. */
	int32 Port = 28888;
	int32 MaxPlayers = 24;
	int32 LanPort = 28889;
	bool bListed = true;
	bool bUpnp = true;
	bool bRequireAccounts = false;
	bool bRecordDemos = false;
	bool bLan = true;
	/** Bind the loopback addresses only (test runs on one machine). */
	bool bLoopbackOnly = false;

	/** A request with the Online settings page's hosting values (port, players, listing, UPnP, accounts, demos, LAN port). */
	static FScctHostRequest Default();

	/** Keeps this request's hosting choices on the Online settings page for the next time (Create Session does it on CREATE). */
	void Remember() const;
};

/** One chat or system line as the menus and the match overlay list it. */
struct FScctOnlineChatEntry
{
	bool bSystem = false;
	bool bTeamOnly = false;
	int32 Team = 255;
	FString Sender;
	FString Text;
};

/** Raised when what a menu shows changed: the state, the lobby, a text, the server list, the chat. */
DECLARE_MULTICAST_DELEGATE(FScctOnlineChanged);

/**
 * The player's visit to an OpenSCCT server (the existing dedicated and listen servers; Runtime/ScctOnlineApi.h).
 * It belongs to the game instance, so it lives across level changes: the lobby is shown in the front end level,
 * each round in the imported level of its map, and this subsystem moves the game between them.
 *
 * Every frame it runs the bridge's Online.Update. In a level that presents a round that happens at the start of
 * the session subsystem's frame (its OnBeforeTicks: the visit may put a round into the bridge's session slot,
 * which the session subsystem then finds); in any other level, and while no level ticks, a core ticker does it.
 *
 * Which level is open follows the bridge:
 *   - MapToLoad names a map (travelling to a round, loading, match, results): /Game/Imported/Maps/<Map>/<Map> is
 *     opened with ?ScctOnline=1, which tells AScctGameMode to start no local match and to present the bridge's
 *     session. When that level has begun play and the round is in the session slot, ReportLoaded is called and
 *     the server's start gate counts this player.
 *   - otherwise (lobby, back from a round, left) a level opened for a round is left for the front end, which shows
 *     the lobby. A visit that failed in a round's level stays there with the play error page (Menu/SScctPlayErrorPage.h)
 *     until the player leaves it; a failure anywhere else is the front end's notice.
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard, used with -RenderOffscreen):
 *   -ScctJoin=<host[:port]>   join that server as soon as the game is up
 *   -ScctSide=spy|merc|watch  the side asked for with -ScctJoin (default: the smaller side)
 *   -ScctAutoReady            say "ready" as soon as the lobby is reached, each time it is reached (a spectator's
 *                             "ready" starts nothing: the server wants a player on a side)
 *   -ScctAutoWalk             hold "forward" while a round is played, so the local player moves on the server
 *   -ScctName=<name>          the player's name for this run (not saved)
 *   -ScctLeaveAfter=S         leave the server S seconds after the first round started
 *   -ScctChat=<text>          say this once in the lobby
 *   -ScctListServers          ask the master for its server list once and log it
 *   -ScctOnlineLog=S          seconds between the status lines in the log (default: 2 with -ScctJoin or -ScctHost, else off)
 *   -ScctHost[=port]          host a game as soon as the game is up (Online.HostPort when no port is given) and join
 *                             it as the lobby's host; loopback only and unlisted unless -ScctHostPublic is given
 *   -ScctHostMap=<map>        the hosted game's map (default: the first map installed)
 *   -ScctHostMode=<mode>      Story, DiskHunt, Deathmatch, MercDeathmatch... (default Story)
 *   -ScctHostName=<name>      the hosted game's name; -ScctHostPassword=<password> its password
 *   -ScctHostPublic           host on every interface with the Online page's listing, UPnP and LAN choices
 *   -ScctLaunchWhen=N         the host launches once N players are in the lobby and the others are ready (default 2);
 *                             -ScctSide picks the host's own side
 *   -ScctOnlineSettingsDump   log every option of the Online settings page with its value at start
 *
 * Hosting (Online/ScctOnlineHost.cpp): a listen server in this process, which the local player joins as the
 * lobby's host; from then on it is an ordinary visit (lobby, rounds, results) and Leave closes the server. The
 * Online settings page (master server, invite key, connect mode, saved servers, LAN port and the hosting choices)
 * is registered here; a change of the master or connect mode applies at once.
 */
UCLASS()
class OPENSCCT_API UScctOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The front end level, where the lobby is shown. */
	static const TCHAR* const FrontEndLevel;

	/** The URL option AScctGameMode reads: the level presents the online round, it starts no local match. */
	static const TCHAR* const OnlineOption;

	static UScctOnlineSubsystem* Get(const UObject* WorldContext);

	/** The imported level of an OpenSCCT map ("/Game/Imported/Maps/AquaE/AquaE"), whether it exists or not. */
	static FString LevelOf(const FString& Map);

	/** The OpenSCCT maps that have an imported level in this project, by folder name, sorted. */
	static TArray<FString> FindImportedMaps();

	// ---- joining and leaving ---------------------------------------------------------------------------------

	/**
	 * Joins "host", "host:port" or "[v6]:port". PreferredTeam: 0 spies, 1 mercs, 255 watch, -1 the smaller side.
	 * Meant to be called from the front end; a local match still running in the open level is ended when the
	 * first round arrives. False when the visit could not start (GetNotice says why).
	 */
	bool Join(const FString& Address, const FString& Password = FString(), int32 PreferredTeam = -1);

	/** Joins the server at Index of GetServers(). */
	void JoinListed(int32 Index, const FString& Password = FString(), int32 PreferredTeam = -1);

	/** Leaves the server (or gives up connecting, or dismisses a failure). The state is Idle afterwards. A host stops hosting. */
	void Leave();

	/**
	 * Brings the online bridge up (the identity, the master) without joining, so a visit another part of the game
	 * starts in the bridge (a hub's pad match) is followed: its levels opened, its round presented. False when the
	 * bridge cannot be loaded (GetNotice says why).
	 */
	bool EnsureReady() { return Connect() != nullptr; }
	// ---- hosting (ScctOnlineHost.cpp) ------------------------------------------------------------------------

	/**
	 * Hosts a game and joins it as the lobby's host; the visit then goes on as any other. False when the server
	 * could not start: GetNotice says why (the port is in use, the map is not installed), or, with OutError, the
	 * reason goes there instead and the failed visit is dismissed, so the page that asked can show it itself.
	 */
	bool Host(const FScctHostRequest& Request, FString* OutError = nullptr);

	/** Closes the player's server; the visit to it ends. */
	void StopHosting();

	/** The player's own server runs. */
	bool IsHosting() const { return HostPort > 0; }

	/** The UDP port of the player's server, 0 when not hosting. */
	int32 GetHostPort() const { return HostPort; }

	/** An admin command on the player's own server (status, players, kick, ban, map, set, say...); answers arrive as system chat. */
	bool HostCommand(const FString& Line);

	/** A text of the player's server (status, players, master listing, UPnP result). */
	FString GetHostText(EScctHostText Kind) const;

	/** Puts the Online page's options into the settings registry (once per process; the subsystem does it at start). */
	static void RegisterOnlineSettings();

	/** The master and invite key: the Online page's values, which default to DefaultGame.ini's [OpenSCCT.Online]. */
	static void ReadMasterSettings(FString& OutUrl, FString& OutInviteKey);

	// ---- what the menus show ---------------------------------------------------------------------------------

	EScctOnlineState GetState() const { return static_cast<EScctOnlineState>(Status.State); }
	const FScctOnlineStatus& GetStatus() const { return Status; }
	bool HasFlag(int32 Flag) const { return (Status.Flags & Flag) != 0; }

	/** A visit is under way or has failed and not been dismissed: the menus show it instead of their pages. */
	bool IsVisiting() const { return GetState() != EScctOnlineState::Idle || !Notice.IsEmpty(); }

	/** The session's players, re-read whenever the bridge's revision changes. */
	const TArray<FScctOnlinePlayer>& GetPlayers() const { return Players; }

	/** A text of the visit as of the last revision (the cheap ones are kept; see RefreshTexts). */
	const FString& GetServerName() const { return ServerName; }
	const FString& GetSetupMap() const { return SetupMap; }
	const FString& GetModeName() const { return ModeName; }
	const FString& GetSetupDescription() const { return SetupDescription; }
	const FString& GetConnectLog() const { return ConnectLog; }
	const FString& GetMapToLoad() const { return MapToLoad; }

	/** Why the visit failed or could not go on, as a player reads it; empty when there is nothing to say. */
	const FString& GetNotice() const { return Notice; }

	/** The last chat and system lines, oldest first. */
	const TArray<FScctOnlineChatEntry>& GetChat() const { return Chat; }

	/** The last round's results; false when there are none. */
	bool GetResults(FScctOnlineResults& OutResults, TArray<FScctOnlineResultRow>& OutRows) const;

	/** Goes up whenever anything above changed. */
	int32 GetRevision() const { return Revision; }
	FScctOnlineChanged OnChanged;

	// ---- the lobby -------------------------------------------------------------------------------------------

	void SetTeam(int32 Team);
	void SetReady(bool bReady);
	void Launch();
	void SendChat(const FString& Text, bool bTeamOnly = false);

	/** With CanControl: the next match is played on Map (its own default settings). False when not sent. */
	bool SetMatchMap(const FString& Map);

	// ---- the lobby's setup, for the menus' match settings (Menu/SScctMenuLobby.cpp) ---------------------------

	/**
	 * With CanControl: the next match is Map (empty: keep) in Mode (EGameMode; negative: keep), with that map's
	 * own rules for the mode. False when not sent (not in the lobby, or the map is not installed here).
	 */
	bool SetMatch(const FString& Map, int32 Mode);

	/** With CanControl: changes the values an options line names ("?TimeLimit=5?SvmBots=2"); the rest stay. */
	bool SetSetupOptions(const FString& Options);

	/** The setup as an options line, as of the last revision; one value of it, or Fallback. */
	const FString& GetSetupOptions() const { return SetupOptions; }
	int32 GetSetupOption(const TCHAR* Name, int32 Fallback) const;

	/** The master's message of the day, empty when it has none. */
	const FString& GetMotd() const { return Motd; }

	/** A dedicated server's admin password, and a command of its console (the answers arrive as system chat lines). */
	void AdminLogin(const FString& Password);
	void AdminCommand(const FString& Command);

	/** Host or admin: everyone back to the lobby. */
	void BackToLobby();

	// ---- identity, master, account ---------------------------------------------------------------------------

	const FString& GetPlayerName() const { return PlayerName; }

	/** The name a guest joins under; kept in the user's settings. */
	void SetPlayerName(const FString& Name);

	/** Asks the master for its server list; GetServers changes when the answer is in. */
	void RequestServerList();
	const TArray<FScctOnlineServerEntry>& GetServers() const { return Servers; }
	const FString& GetMasterError() const { return MasterError; }
	bool HasMaster() const { return bMasterSet; }

	/** Starts the bridge's online half now (it starts on first use otherwise), so HasMaster and the name are known. */
	void EnsureConnected() { Connect(); }

	/** Starts a sign-in in the player's browser (the official servers take signed-in players only). */
	void BeginSignIn();
	void SignOut();
	const FString& GetAccountName() const { return AccountName; }
	const FString& GetAccountError() const { return AccountError; }
	const FString& GetSignInCode() const { return SignInCode; }

	// ---- the levels ------------------------------------------------------------------------------------------

	/**
	 * Called by the game mode of a level opened with the online option: that world's session subsystem presents
	 * the visit's rounds, and the visit's Update runs at the start of its frames.
	 */
	void PresentIn(UScctSessionSubsystem* Session);

	/** Opens the front end level (from a match level's menu: back to the main menu). */
	void OpenFrontEnd();

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** The table, fetched on first use; null (the reason is in Notice) when the bridge cannot be loaded. */
	const FScctOnlineApi* Connect();

	/** Once a frame, whoever calls first: Online.Update, the status, and everything that follows from it. */
	void TickVisit();
	bool TickFromTicker(float DeltaSeconds);
	void HandleBeforeTicks(float DeltaSeconds);

	void RefreshTexts();
	void DrainChat();
	void HandleStateChange(EScctOnlineState Old, EScctOnlineState New);

	/** Clears the pending travel once the world it was asked in has been replaced. */
	void ForgetFinishedTravel();

	/** Opens the level the visit needs when it is not the one that is open. */
	void FollowLevel();

	/** Tells the server this player is in the round's level, once the level is up. */
	void ReportLoadedWhenReady();

	void RunUnattended(float DeltaSeconds);
	void LogStatus(float DeltaSeconds);

	// Hosting and the Online settings page (ScctOnlineHost.cpp).

	void ReadHostSwitches();
	void HandleSettingChanged(FName Key);
	void ApplyMasterSettings();
	void ApplyConnectMode();
	const FScctHostApi* ConnectHost();

	/** Once a frame while hosting: the host's log lines into the log, and notices that the server stopped. */
	void TickHost();
	void RunUnattendedHost();
	void LogHostStatus();
	void Touch();

	FString ReadText(EScctOnlineText Kind) const;
	void LoadIdentity();
	void SaveIdentity();
	void OpenLevel(const FString& LevelUrl);

	const FScctOnlineApi* Api = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 LastTickFrame = 0;
	double LastTickSeconds = 0.0;

	FScctOnlineStatus Status = {};
	int32 BridgeRevision = -1;
	int32 Revision = 0;

	TArray<FScctOnlinePlayer> Players;
	TArray<FScctOnlineServerEntry> Servers;
	TArray<FScctOnlineChatEntry> Chat;
	FString ServerName, SetupMap, ModeName, SetupDescription, ConnectLog, MapToLoad, SetupOptions, Motd;
	FString MasterError, AccountName, AccountError, SignInCode;
	FString Notice;

	FString PlayerName;
	bool bMasterSet = false;
	int32 LastMasterState = 0;
	int32 LastAccountState = 0;
	bool bSignInPageOpened = false;

	/** The level a travel was asked for, and the world it was asked in; empty once another world is open. */
	FString PendingLevel;
	TWeakObjectPtr<const UWorld> PendingFrom;

	/** Seconds the visit has wanted another level; the travel starts after a moment so the screen can say so. */
	float TravelDelay = 0.0f;

	/**
	 * A round's map with no level in the game: its cooked pack is fetched (Online/ScctMapPacks) before the visit
	 * gives up. The map being fetched, and why it could not be (the visit then ends with that reason).
	 */
	FString LevelFetchMap;
	FString LevelFetchError;

	/** ReportLoaded was sent for this bridge session id. */
	int32 ReportedSessionId = 0;

	// Unattended switches.
	FString AutoJoin;
	int32 AutoSide = -1;
	bool bAutoReady = false;
	bool bAutoWalk = false;
	bool bAutoWalkHeld = false;
	TWeakObjectPtr<class AScctPlayerController> AutoWalkPlayer;
	int32 AutoReadyRevision = -1;
	bool bAutoJoined = false;
	bool bAutoListed = false;
	bool bAutoChatSent = false;
	FString AutoChat;
	float LeaveAfter = 0.0f;
	float MatchSeconds = -1.0f;
	bool bLeftOnTimer = false;
	float LogEvery = 0.0f;
	float LogTimer = 0.0f;

	// Hosting.
	const FScctHostApi* HostApi = nullptr;
	int32 HostPort = 0;
	FDelegateHandle SettingsChangedHandle;

	// Unattended hosting switches.
	int32 AutoHostPort = -1;
	FString AutoHostMap;
	FString AutoHostMode;
	FString AutoHostName;
	FString AutoHostPassword;
	bool bAutoHostPublic = false;
	bool bAutoHosted = false;
	int32 LaunchWhen = 2;
	/** -ScctLaunchWhen was given: a game hosted from Create Session launches unattended too. */
	bool bLaunchWhenGiven = false;
	bool bAutoLaunched = false;
	int32 AutoSideRevision = -1;
};
