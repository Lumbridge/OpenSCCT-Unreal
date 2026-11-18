#pragma once

#include "CoreMinimal.h"

// The hub half of the C# bridge: visiting an OpenSCCT hub (docs/HUB.md in OpenSCCT), and hosting one on this PC.
// These mirror Bridge/OpenScct.UnrealBridge/Hub/HubTypes.cs and HubExports.cs field for field; change both
// together and bump the version.
//
// The table is fetched like the other feature tables: ScctFeature<FScctHubApi>() (Runtime/ScctFeatures.h).
//
// Every frame, on the game thread: Hub.Update() first, then the main table's Update(). Once the visit's session is
// in the main slot (RoundActive), Hub.Update() runs its simulation ticks itself (the visit owns the connection), so
// the main Update() usually returns 0 ticks and only brings the frame's blend factor up to date; keep calling both.
// Calling the main table's Stop or StartLocal during a visit ends it (the player leaves the hub).
// A visit goes Connecting -> Loading -> InHub and ends Failed (GetText(Error)) or, after Leave, Idle. Open the level
// of GetText(MapToLoad) once it is not empty; call ReportLoaded once that level is up and presents the session, and
// the hub spawns the player's pawn.
// Text is UTF-16, zero terminated.

constexpr int32 ScctHubApiVersion = 2;

/** Where the hub visit is. */
enum class EScctHubState : int32
{
	Idle = 0,       // not in a hub: nothing was joined, or Leave cleared the last visit
	Connecting = 1, // waiting for the hub to answer and welcome the player
	Loading = 2,    // welcomed: the hub map loads and the player's controller arrives
	InHub = 3,      // the visit's session is in the main slot: pawns, poses, view and input work as in a match
	Failed = 4,     // the visit ended with an error: GetText(EScctHubText::Error). Leave() clears it.
	InMatch = 5,    // away at a pad's match (the online visit plays it); the hub is joined again after its results
};

/** FScctHubStatus::Flags. */
namespace EScctHubStatusFlags
{
	constexpr int32 Welcomed = 1;        // the hub welcomed this player
	constexpr int32 RoundActive = 2;     // the visit's session runs in the main slot
	constexpr int32 NeedsLoadReport = 4; // the hub waits for ReportLoaded()
	constexpr int32 ReportedWaiting = 8; // the hub knows this player is in the map
	constexpr int32 Hosting = 16;        // this PC hosts the hub
	constexpr int32 LocalPawn = 32;      // the local player has a pawn
	constexpr int32 HostListed = 64;     // the hub this PC hosts is listed in the master's hub list
	constexpr int32 ListedHub = 128;     // the visit came in through the master's join answer for a listed hub
	constexpr int32 AskingMaster = 256;  // the master is being asked the way to a listed hub (no connection yet)
}

/** Where the master's hub list is (GetListState). */
enum class EScctHubListState : int32
{
	Idle = 0,   // not asked yet
	Busy = 1,   // asked, the answer is not in
	Ready = 2,  // the list is in (it may be empty)
	Failed = 3, // no answer or no master: GetText(EScctHubText::ListMessage)
};

/** FScctHubListEntry::Flags. */
namespace EScctHubListFlags
{
	constexpr int32 Official = 1;        // one of the OpenSCCT servers' own hubs
	constexpr int32 Dedicated = 2;       // a dedicated hub server, not one a player hosts
	constexpr int32 RequireAccounts = 4; // signed-in players only
	constexpr int32 Full = 8;
	constexpr int32 OtherBuild = 16;     // another build's protocol: the join would be refused
	constexpr int32 OtherMap = 32;       // its hub map is not the one installed here
	constexpr int32 Pinged = 64;         // PingMs is valid
}

/** One hub of the master's list, in the page's order: official hubs first, then the nearest, then the fullest. */
struct FScctHubListEntry
{
	int32 Players;
	int32 MaxPlayers;
	int32 PingMs;       // -1 before an answer
	int32 Flags;        // EScctHubListFlags
	TCHAR Id[64];
	TCHAR Name[64];
	TCHAR Region[16];
	TCHAR Problem[32];  // why it cannot be joined ("full", "another build", "another hub map"), empty when it can
};

/** The visit in one read (GetStatus); cheap enough for every frame. */
struct FScctHubStatus
{
	int32 State;          // EScctHubState
	int32 Flags;          // EScctHubStatusFlags
	int32 Revision;       // goes up when anything the hub page shows changed
	int32 PlayerId;       // this player's id in the hub, 0 before the welcome
	int32 Team;           // 0 spies, 1 mercs, -1 not known yet
	int32 PlayerCount;    // players on the hub's roster (sent every few seconds)
	int32 MaxPlayers;
	int32 HostPort;       // the UDP port of the hub this PC hosts, 0 when it hosts none
	float ConnectSeconds; // seconds since the visit started connecting
	int32 LogWaiting;     // log lines waiting for TakeLog
};

/** The texts GetText reads. */
enum class EScctHubText : int32
{
	Error = 0,      // why the visit failed, as a player reads it
	HubName = 1,
	MapToLoad = 2,  // the level to have open while in the hub ("Hub"); empty while connecting and when not in a hub
	Address = 3,    // the "host:port" the visit connects to
	Status = 4,     // what is happening, as the hub page says it
	PlayerName = 5, // the name the player joined under
	LoadingPicture = 7,
	ListMessage = 6, // what the master's hub list says ("3 hub(s) listed.", the master's error)
};

struct FScctHubApi
{
	int32 Version;

	/** A folder that holds SVMap folders, searched first for the hub map (the project's Saved/ScctImport/HubContent/svmap). Seen by the next visit. */
	void (__cdecl* AddMapRoot)(const TCHAR* Folder);

	/** Joins "host", "host:port" or "[v6]:port" (port 28886 when none) as Name on Team (0 spies, 1 mercs). Returns 1 when the visit started. */
	int32 (__cdecl* Connect)(const TCHAR* Address, const TCHAR* Name, int32 Team);

	/** Opens a hub on this PC on UDP Port (0: 28886; every interface unless bLoopbackOnly) and joins it. Returns 1 when it is up and the join started, 0 with the reason in GetText(Error). */
	int32 (__cdecl* HostAndJoin)(int32 Port, const TCHAR* Name, int32 Team, int32 bLoopbackOnly);

	/** Leaves the hub, closes the hub this PC hosts, and clears a failure: the state is Idle afterwards. */
	void (__cdecl* Leave)();

	/** Once a frame, before the main table's Update: runs the visit's 60 Hz ticks that are due. Returns the ticks run. The tick that receives the hub map loads its simulation and blocks for as long. */
	int32 (__cdecl* Update)();

	/** Writes the status. Returns its State. */
	int32 (__cdecl* GetStatus)(FScctHubStatus* Status);

	/** Copies a text (EScctHubText) into Buffer. Returns its full length in characters. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** The hub level is up and presents the visit's session: the hub may place the player. Once per visit, while NeedsLoadReport is set. */
	void (__cdecl* ReportLoaded)();

	/** Copies the next waiting log line (the visit's steps, a hosted hub's own lines) into Buffer and drops it. Returns its full length, -1 when none waits. */
	int32 (__cdecl* TakeLog)(TCHAR* Buffer, int32 Capacity);

	// Version 2: the master's hub list, listed joins and the hosted hub's listing.

	/** The master (its http address and invite key) and the saved sign-in token, used when the online side has not set them yet; a master or token already set is kept. Null or empty values are skipped. */
	void (__cdecl* ConfigureMaster)(const TCHAR* Url, const TCHAR* InviteKey, const TCHAR* AccountToken);

	/** Asks the master for its hubs (the pings follow by themselves while the list is up). The revision moves when the answer is in. */
	void (__cdecl* RequestList)();

	/** EScctHubListState. */
	int32 (__cdecl* GetListState)();

	/** Writes up to Capacity hubs. Returns the number of hubs. */
	int32 (__cdecl* GetList)(FScctHubListEntry* Entries, int32 Capacity);

	/** Joins the hub at Index of GetList as Name on Team, through the master's join answer. Returns 1 when it started (state Connecting, AskingMaster), 0 with the reason in GetText(Error). */
	int32 (__cdecl* JoinListed)(int32 Index, const TCHAR* Name, int32 Team);

	/** Whether a hub hosted from now on lists itself in the master's hub list (on by default; a loopback-only hub never does). */
	void (__cdecl* SetHostListed)(int32 bListed);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctHubStatus) == 40, "FScctHubStatus does not match HubStatus");
static_assert(sizeof(FScctHubListEntry) == 368, "FScctHubListEntry does not match HubListEntry");
static_assert(sizeof(FScctHubApi) == 8 + 15 * sizeof(void*), "FScctHubApi does not match HubApi");
