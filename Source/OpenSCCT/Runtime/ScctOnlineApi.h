#pragma once

#include "CoreMinimal.h"

// The online half of the C# bridge: joining the existing OpenSCCT servers. These mirror
// Bridge/OpenScct.UnrealBridge/Online/OnlineTypes.cs and OnlineExports.cs field for field; change both together
// and bump the version.
//
// Fetch the table once, next to the main one:
//   Host.GetFunction(TEXT("OpenScct.UnrealBridge.OnlineExports"), TEXT("GetApi"), Error)   // int32(__stdcall*)(FScctOnlineApi*)
//
// Every frame, on the game thread: Online.Update() first, then the main table's Update().
// A visit goes Connecting -> Lobby -> Travelling -> Loading -> Match -> Results -> (Travelling -> Lobby or the next
// round), and ends Failed (GetText(Error)) or, after Leave, Idle. While the RoundActive flag is set the main table's
// pawns, poses, view and input work on the online round as they do on a local match.
// Text is UTF-16, zero terminated; fixed text fields are cut to their size.

constexpr int32 ScctOnlineApiVersion = 1;

/** Where the online visit is. */
enum class EScctOnlineState : int32
{
	Idle = 0,       // not online: nothing was joined, or Leave cleared the last visit
	Connecting = 1, // asking the master how to reach a listed server, or trying the join attempts
	Lobby = 2,
	Travelling = 3, // between maps: the next connection starts about 2 s later; put the loading screen up now
	Loading = 4,    // connected to the round's map, the round has not started (it waits for ReportLoaded)
	Match = 5,
	Results = 6,
	Failed = 7,     // the visit ended with an error: GetText(EScctOnlineText::Error). Leave() clears it.
	Left = 8,
};

/** FScctOnlineStatus::Flags. */
namespace EScctOnlineStatusFlags
{
	constexpr int32 Dedicated = 1;         // a dedicated server (no player is its host)
	constexpr int32 Kicked = 2;            // the server kicked or banned this player (the state is Failed)
	constexpr int32 RoundActive = 4;       // a round's play session runs: read pawns, poses and the view, send input
	constexpr int32 PreparingBots = 8;     // the launch waits for the server to prepare bot navigation
	constexpr int32 CanControl = 16;       // the local player may change the setup
	constexpr int32 CanLaunch = 32;        // the local player may ask for the launch
	constexpr int32 CanStart = 64;         // everyone on a side is ready and has the map: a launch would start
	constexpr int32 NeedsLoadReport = 128; // the round waits for ReportLoaded()
	constexpr int32 ReportedWaiting = 256; // the server knows this player is in the map and waiting
	constexpr int32 ResultsReady = 512;    // the last round's results are in (GetResults)
	constexpr int32 LocalReady = 1024;     // the local player is ready
	constexpr int32 RoundRefused = 2048;   // the round's map has no converted content here: no round is entered
	constexpr int32 Practice = 4096;       // a practice server
	constexpr int32 Revenge = 8192;        // a revenge pair: two games, sides swapped for the second
}

/** The visit in one read (GetStatus); cheap enough for every frame. */
struct FScctOnlineStatus
{
	int32 State;              // EScctOnlineState
	int32 Phase;              // the server's session phase: 0 lobby, 1 loading, 2 match, 3 results
	int32 Flags;              // EScctOnlineStatusFlags
	int32 Revision;           // goes up when the lobby, phase, results or a text changed: re-read players and texts then
	int32 LocalId;            // this player's id in the session, -1 before the server said hello
	int32 LocalTeam;          // 0 spies, 1 mercs, 255 spectator, -1 not known yet
	int32 PlayerCount;
	int32 MaxPlayers;
	int32 Mode;               // EGameMode: 4 Disk Hunt, 5 Story, 6 Deathmatch, 7 Merc Deathmatch
	int32 TimeLimit;          // minutes, 0 for no limit
	int32 SpyLives, MercLives; // lives of each spy and merc, 0 for unlimited
	int32 RespawnSeconds;
	int32 ScoreLimit;         // kill limit (Merc Deathmatch), disks to take (Disk Hunt), else the map's score limit
	int32 GamesInARow;
	int32 Bots;               // server bots: 0 off, 1 a number per side, 2 fill the empty places, 3 custom
	float Countdown;          // seconds left of the launch countdown or of the results, or 0
	int32 LoadingPictureSeed; // the server's pick among the map's loading pictures (same for all players), -1 none
	int32 ChatWaiting;        // chat and system lines waiting for TakeChat
	int32 MasterState;        // EScctOnlineMasterState of the last master request
	int32 ServerCount;        // servers of the last list (GetServers)
	int32 AccountState;       // EScctOnlineAccountState
};

/** FScctOnlinePlayer::Flags (OpenSCCT's SessionPlayerFlags). */
namespace EScctOnlinePlayerFlags
{
	constexpr int32 Ready = 1;
	constexpr int32 Host = 2;
	constexpr int32 Admin = 4;
	constexpr int32 Spectator = 8;
	constexpr int32 InGame = 16;
	constexpr int32 Loading = 32;
	constexpr int32 Control = 64;      // may change the setup and launch
	constexpr int32 TeamVoice = 128;   // microphone restricted to the team in the lobby
	constexpr int32 Bot = 256;         // a server bot: always ready, cannot be kicked
	constexpr int32 Verified = 512;    // signed in: the name is the account's
	constexpr int32 VoiceBanned = 1024;
}

/** One player of the session (GetPlayers), as the lobby and the scoreboard list them. */
struct FScctOnlinePlayer
{
	int32 Id;
	int32 Team;     // 0 spies, 1 mercs, 255 spectator
	int32 Flags;    // EScctOnlinePlayerFlags
	int32 PingMs;
	int32 MapState; // has the setup's map: 0 unknown, 1 yes, 2 missing, 3 another version
	int32 Rank;     // rank with its title (OpenSCCT's PlayerRank packed), 0 for guests and bots
	TCHAR Name[48];
};

/** One chat or system line (TakeChat). */
struct FScctOnlineChatLine
{
	int32 System;   // 1 for a line of the server itself (joins, leaves, command answers): no sender
	int32 Team;     // the sender's side (0 spies, 1 mercs), 255 for none
	int32 TeamOnly; // 1 when only the sender's side got it
	TCHAR Sender[48];
	TCHAR Text[256];
};

/** The results of the last round (GetResults). */
struct FScctOnlineResults
{
	int32 WinningTeam;   // 0 spies, 1 mercs, 255 none (the round was stopped)
	int32 WinningReason; // 0 objectives or kill limit, 1 time out, 2 a side eliminated, 255 the round was stopped
	int32 ObjectivesTaken, ObjectivesNeeded;
	int32 GamesLeft;     // games of the series still to play (0: the lobby is next)
	int32 RowCount;
};

/** One player's line of the results. */
struct FScctOnlineResultRow
{
	int32 Id;
	int32 Team;
	int32 Score, Kills, Deaths, Lives;
	TCHAR Name[48];
};

/** FScctOnlineServerEntry::Flags. */
namespace EScctOnlineServerFlags
{
	constexpr int32 Password = 1;
	constexpr int32 Dedicated = 2;
	constexpr int32 RequireAccounts = 4; // only players signed in on the master may join
	constexpr int32 Trusted = 8;         // one of the master's own servers
	constexpr int32 Practice = 16;
	constexpr int32 SameBuild = 32;      // runs this build's network protocol; without it the join is refused
	constexpr int32 HasMap = 64;         // the server's map is installed here
	constexpr int32 SameMapVersion = 128; // and it is the server's version of it
	constexpr int32 Full = 256;
}

/** One server of the master's list (GetServers). */
struct FScctOnlineServerEntry
{
	int32 Players, MaxPlayers;
	int32 Mode;       // EGameMode, as FScctOnlineStatus::Mode
	int32 Phase;      // 0 lobby, 1 loading, 2 match, 3 results
	int32 Flags;      // EScctOnlineServerFlags
	int32 AgeSeconds; // seconds since the server's last heartbeat
	TCHAR Id[64];
	TCHAR Name[64];
	TCHAR Map[32];
	TCHAR Region[16];
	TCHAR Version[24];
};

enum class EScctOnlineMasterState : int32
{
	Idle = 0,
	Busy = 1,
	Ready = 2,
	Failed = 3, // GetText(EScctOnlineText::MasterError)
};

enum class EScctOnlineAccountState : int32
{
	SignedOut = 0,
	SigningIn = 1, // waiting for the player in the browser: open GetText(SignInUrl) once it is not empty
	SignedIn = 2,
	Failed = 3,    // GetText(EScctOnlineText::AccountError)
};

/** The texts GetText reads. */
enum class EScctOnlineText : int32
{
	Error = 0,            // why the visit failed, as a player reads it
	ServerName = 1,
	SetupMap = 2,         // the map of the match the lobby sets up (its folder name, e.g. AquaE)
	CurrentMap = 3,       // the map the connection is on now: "Lobby" in the lobby
	MapToLoad = 4,        // the map Unreal must have loaded: the travel's target, then the round's map; empty otherwise
	ModeName = 5,         // Story, Disk Hunt...
	SetupDescription = 6, // the whole setup in a line
	ConnectLog = 7,       // what the join tried, one attempt per line (the connecting screen)
	PlayerName = 8,       // the name the player joins under (the account's when signed in)
	InstallId = 9,        // save it; give it back with SetIdentity
	PrivateKey = 10,      // save it; give it back with SetIdentity. Never show it.
	MasterError = 11,
	SignInUrl = 12,       // the page to open in the player's browser
	SignInCode = 13,      // the short code that page shows too
	AccountToken = 14,    // save it; give it back with SetAccountToken. Never show it.
	AccountName = 15,
	AccountError = 16,
	ResultsMap = 17,
	MasterMotd = 18,
};

struct FScctOnlineApi
{
	int32 Version;

	/** Who joins: the guest name, and the saved install id and private key (null or empty on a first run: new ones are made, read them with GetText and save them). */
	void (__cdecl* SetIdentity)(const TCHAR* PlayerName, const TCHAR* InstallId, const TCHAR* PrivateKey);

	/** Joins "host", "host:port" or "[v6]:port" (port 28888 when none). PreferredTeam: 0 spies, 1 mercs, 255 watch, -1 the smaller side. Password may be null. Returns 1 when the visit started. */
	int32 (__cdecl* Join)(const TCHAR* Address, const TCHAR* Password, int32 PreferredTeam);

	/** Leaves the server and clears the visit (a failed one too): the state is Idle afterwards. */
	void (__cdecl* Leave)();

	/** Once a frame, before the main table's Update: runs the visit's 60 Hz ticks that are due. Returns the ticks run. The tick that connects to a round's map loads its simulation and blocks for seconds. */
	int32 (__cdecl* Update)();

	/** Writes the status. Returns its State. */
	int32 (__cdecl* GetStatus)(FScctOnlineStatus* Status);

	/** Writes the session's players. Returns their count (only Capacity are written). */
	int32 (__cdecl* GetPlayers)(FScctOnlinePlayer* Players, int32 Capacity);

	/** Copies a text (EScctOnlineText) into Buffer. Returns its full length in characters. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** Picks a side in the lobby: 0 spies, 1 mercs, 255 spectators. */
	void (__cdecl* SetTeam)(int32 Team);

	void (__cdecl* SetReady)(int32 bReady);

	/** Asks for the launch (again while the countdown runs: its cancel), or for the next round from the results. */
	void (__cdecl* Launch)();

	/** Host or admin: everyone back to the lobby. */
	void (__cdecl* BackToLobby)();

	/** Unreal's level for MapToLoad is up: the server may start the round with this player. Call once per round, while NeedsLoadReport is set. */
	void (__cdecl* ReportLoaded)();

	/** A chat line (at most 100 characters) to everyone, or to the player's side. Lobby and round. */
	void (__cdecl* SendChat)(const TCHAR* Text, int32 bTeamOnly);

	/** Writes the next waiting chat or system line. Returns 1 when one was written, 0 when none waits. */
	int32 (__cdecl* TakeChat)(FScctOnlineChatLine* Line);

	/** Writes the last round's results and its lines. Returns the number of lines (only Capacity are written), -1 when there are no results. */
	int32 (__cdecl* GetResults)(FScctOnlineResults* Results, FScctOnlineResultRow* Rows, int32 Capacity);

	/** Logs in as an admin of a dedicated server. */
	void (__cdecl* AdminLogin)(const TCHAR* Password);

	/** An admin console command (status, map, set...); the answers arrive as system chat lines. */
	void (__cdecl* AdminCommand)(const TCHAR* Command);

	/** Sets the master server's address (http or https) and its invite key (null or empty for none). */
	void (__cdecl* SetMaster)(const TCHAR* Url, const TCHAR* InviteKey);

	/** Asks the master for its servers; MasterState is Busy until the answer is in. */
	void (__cdecl* RequestServerList)();

	/** Writes the last list. Returns the number of servers (only Capacity are written). */
	int32 (__cdecl* GetServers)(FScctOnlineServerEntry* Servers, int32 Capacity);

	/** Joins the server at Index of the last list (direct, NAT punch or relay as the master says; with the player's ticket when signed in). */
	void (__cdecl* JoinListed)(int32 Index, const TCHAR* Password, int32 PreferredTeam);

	/** Signs in with a token saved from an earlier sign-in (null or empty: signed out). The master answers with the account: AccountState turns SignedIn. */
	void (__cdecl* SetAccountToken)(const TCHAR* Token);

	/** Starts a sign-in through the player's browser (see EScctOnlineAccountState::SigningIn). */
	void (__cdecl* BeginSignIn)();

	void (__cdecl* SignOut)();
};
