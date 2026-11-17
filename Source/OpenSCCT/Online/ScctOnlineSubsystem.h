#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctOnlineApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctOnlineSubsystem.generated.h"

class UScctSessionSubsystem;

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
 *   - otherwise (lobby, back from a round, failed, left) a level opened for a round is left for the front end,
 *     which shows the lobby or the reason of the failure.
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
 *   -ScctOnlineLog=S          seconds between the status lines in the log (default 2; 0 turns them off)
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
	 * A local match that is running is stopped first. False when the visit could not start (GetNotice says why).
	 */
	bool Join(const FString& Address, const FString& Password = FString(), int32 PreferredTeam = -1);

	/** Joins the server at Index of GetServers(). */
	void JoinListed(int32 Index, const FString& Password = FString(), int32 PreferredTeam = -1);

	/** Leaves the server (or gives up connecting, or dismisses a failure). The state is Idle afterwards. */
	void Leave();

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

	// ---- identity, master, account ---------------------------------------------------------------------------

	const FString& GetPlayerName() const { return PlayerName; }

	/** The name a guest joins under; kept in the user's settings. */
	void SetPlayerName(const FString& Name);

	/** Asks the master for its server list; GetServers changes when the answer is in. */
	void RequestServerList();
	const TArray<FScctOnlineServerEntry>& GetServers() const { return Servers; }
	const FString& GetMasterError() const { return MasterError; }
	bool HasMaster() const { return bMasterSet; }

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

	/** Opens the level the visit needs when it is not the one that is open. */
	void FollowLevel();

	/** Tells the server this player is in the round's level, once the level is up. */
	void ReportLoadedWhenReady();

	void RunUnattended(float DeltaSeconds);
	void LogStatus(float DeltaSeconds);
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
	FString ServerName, SetupMap, ModeName, SetupDescription, ConnectLog, MapToLoad;
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

	/** ReportLoaded was sent for this bridge session id. */
	int32 ReportedSessionId = 0;

	// Unattended switches.
	FString AutoJoin;
	int32 AutoSide = -1;
	bool bAutoReady = false;
	bool bAutoWalk = false;
	int32 AutoReadyRevision = -1;
	bool bAutoJoined = false;
	bool bAutoListed = false;
	bool bAutoChatSent = false;
	FString AutoChat;
	float LeaveAfter = 0.0f;
	float MatchSeconds = -1.0f;
	bool bLeftOnTimer = false;
	float LogEvery = 2.0f;
	float LogTimer = 0.0f;
};
