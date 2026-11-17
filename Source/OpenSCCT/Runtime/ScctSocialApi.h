#pragma once

#include "CoreMinimal.h"

// The social half of the C# bridge (an OpenSCCT addition): the player's Discord status with a friend's Join, the
// website's Join button and the master's presence lease. These mirror Bridge/OpenScct.UnrealBridge/Social/
// (SocialTypes.cs, PresenceSnapshot.cs and SocialExports.cs) field for field; change both together and bump the
// version. Bridge/Probes/SocialProbe prints the sizes asserted below.
//
// The table is fetched like the other feature tables: ScctFeature<FScctSocialApi>() (Runtime/ScctFeatures.h).
// Text is UTF-16, zero terminated. Game thread only.

constexpr int32 ScctSocialApiVersion = 1;

/** FScctSocialPresence::Activity. */
enum class EScctSocialActivity : int32
{
	Menus = 0,
	Online = 1,  // a visit to an online server (joining, lobby, match, results)
	Hub = 2,     // a hub visit
	Offline = 3, // an offline match
};

/** FScctSocialPresence::Page: the front end's page while in the menus. */
enum class EScctSocialPage : int32
{
	Other = 0,
	Servers = 1,
	JoinByAddress = 2,
	OfflineSetup = 3,
	Hub = 4,
	Options = 5,
	Results = 6,
	HostSetup = 7,
};

/** FScctSocialPresence::Phase of an online visit. */
enum class EScctSocialPhase : int32
{
	Joining = 0,
	Lobby = 1,
	Match = 2,
	Results = 3,
};

namespace EScctSocialPresenceFlags
{
	constexpr int32 Hosting = 1;      // this PC hosts the server or the hub
	constexpr int32 RoundDrawn = 2;   // the round's level is played (side and scores mean something)
	constexpr int32 Spectating = 4;
	constexpr int32 HubMatch = 8;     // the visit is a hub pad's match: friends cannot join it
	constexpr int32 JoinPassword = 16; // the server asks for a password (the password itself stays here)
	constexpr int32 HubArrived = 32;  // the player stands in the drawn hub
}

/** What the player is doing, as the social subsystem builds it each second. */
struct FScctSocialPresence
{
	int32 Activity;  // EScctSocialActivity
	int32 Page;      // EScctSocialPage, in the menus
	int32 Phase;     // EScctSocialPhase, online
	int32 Flags;     // EScctSocialPresenceFlags
	int32 Side;      // 0 spy, 1 merc, -1 not on a side
	int32 SpyScore;  // the match score, -1 when not known
	int32 MercScore;
	int32 Mode;      // EGameMode, -1 when not known
	int32 Bots;      // bots in an offline match
	int32 PartySize; // players on the server or in the hub
	int32 PartyMax;
	int32 JoinKind;  // EScctSocialJoinKind, -1: friends cannot join
};
static_assert(sizeof(FScctSocialPresence) == 48, "FScctSocialPresence must match the bridge's SocialPresence");

/** How friends join (a Discord join secret's kind). */
enum class EScctSocialJoinKind : int32
{
	None = -1,
	Master = 0,  // a server the master lists, by its id
	Address = 1, // a server by "host:port"
	Hub = 2,     // a hub by "host:port"
};

/** What the bridge asks Unreal to do (TakeRequest). */
enum class EScctSocialRequest : int32
{
	None = 0,
	JoinAddress = 1,   // join the online server at Value
	JoinHub = 2,       // join the hub at Value
	JoinMaster = 3,    // join the server the master lists as Value (JoinMaster); Text is its name
	JoinHubListed = 4, // join the hub the master lists as Value (JoinHubListed); Text is its name
	Notice = 5,        // show Text for a few seconds
	SignIn = 6,        // show Text and the page where the player signs in (a website join goes on afterwards)
	SignedOut = 7,     // the master refused the saved sign-in: forget it
	Quit = 8,          // this process handed its website join to the game already running
};

namespace EScctSocialRequestFlags
{
	constexpr int32 NeedsPassword = 1;  // ask the player for the server's password first
	constexpr int32 RequireAccount = 2; // join only with the signed-in account's ticket
	constexpr int32 FromWebsite = 4;
	constexpr int32 FromDiscord = 8;
}

struct FScctSocialRequest
{
	int32 Kind;  // EScctSocialRequest
	int32 Flags; // EScctSocialRequestFlags
};
static_assert(sizeof(FScctSocialRequest) == 8, "FScctSocialRequest must match the bridge's SocialRequest");

/** Update's readiness. */
namespace EScctSocialReadiness
{
	constexpr int32 InMenus = 1; // the front end is up: a friend's Join may start (leaving a visit)
	constexpr int32 Idle = 2;    // the front end is up and nothing is visited or hosted: a website join may start
}

enum class EScctDiscordState : int32
{
	Off = 0,
	Connecting = 1,
	NotRunning = 2,
	Ready = 3,
};

struct FScctSocialStatus
{
	int32 Discord;          // EScctDiscordState
	int32 DiscordCanJoin;   // the game is registered with Discord: friends' Joins are taken
	int32 WebJoinListening; // this game owns the website join pipe
	int32 WebJoinPending;   // a website join waits for the menus or the sign-in
	int32 PresenceLeases;   // presence leases the master accepted so far
	int32 RequestsWaiting;
	int32 LogWaiting;
};
static_assert(sizeof(FScctSocialStatus) == 28, "FScctSocialStatus must match the bridge's SocialStatus");

struct FScctSocialJoin
{
	int32 Kind;     // EScctSocialJoinKind
	int32 Password; // 1 when the server asks for a password
};
static_assert(sizeof(FScctSocialJoin) == 8, "FScctSocialJoin must match the bridge's SocialJoin");

namespace EScctSocialStartFlags
{
	constexpr int32 Lease = 1; // post the presence lease every 30 s
}

struct FScctSocialApi
{
	int32 Version;

	/** Starts the website join (the command line's server id, or empty) and with Lease the presence lease. Once. */
	void (__cdecl* Start)(const TCHAR* WebJoinId, int32 Flags);

	/** The Discord status on (1) or off (0); the exe and the command register the game for a friend's Join (empty: no Join). */
	void (__cdecl* SetDiscord)(int32 bEnabled, const TCHAR* LaunchExe, const TCHAR* LaunchCommand);

	/** The player's state now, with the server's or hub's name, the map's id and the join value. */
	int32 (__cdecl* SetPresence)(const FScctSocialPresence* Presence, const TCHAR* Server, const TCHAR* Map, const TCHAR* JoinValue);

	/** Once a frame: seconds since the last call and EScctSocialReadiness. Returns the requests waiting. */
	int32 (__cdecl* Update)(float Seconds, int32 Readiness);

	int32 (__cdecl* GetStatus)(FScctSocialStatus* Status);

	/** Takes the oldest request (Value and Text cut to fit). Returns 1, or 0 when none waits. */
	int32 (__cdecl* TakeRequest)(FScctSocialRequest* Request, TCHAR* Value, int32 ValueCapacity, TCHAR* Text, int32 TextCapacity);

	/** Takes the oldest log line. Returns its full length, or -1 when none waits. */
	int32 (__cdecl* TakeLog)(TCHAR* Buffer, int32 Capacity);

	/** The online visit's join secret (Kind -1: none). Returns the value's full length. */
	int32 (__cdecl* GetVisitJoin)(FScctSocialJoin* Join, TCHAR* Value, int32 Capacity);

	/** Joins the server the master lists by id (team 0, 1, 255 or -1). Returns 1 when the master is asked. */
	int32 (__cdecl* JoinMaster)(const TCHAR* Id, const TCHAR* Name, const TCHAR* Password, int32 Team, int32 bRequireAccount);

	/** Joins the hub the master lists by id. Returns 1 when the master is asked (UScctHubSubsystem::JoinListedId calls it). */
	int32 (__cdecl* JoinHubListed)(const TCHAR* Id, const TCHAR* HubName, const TCHAR* PlayerName, int32 Team, int32 bRequireAccount);

	/** A friend's Join with this secret, as Discord would hand it (unattended checks). Returns 1 when understood. */
	int32 (__cdecl* SimulateDiscordJoin)(const TCHAR* Secret);

	/** A website join of this id, as if it came over the pipe. Returns 1 when it is a server id. */
	int32 (__cdecl* QueueWebJoin)(const TCHAR* Id);

	/** What Discord shows for the last state described, as one line. Returns its full length. */
	int32 (__cdecl* GetPresenceText)(TCHAR* Buffer, int32 Capacity);
};
