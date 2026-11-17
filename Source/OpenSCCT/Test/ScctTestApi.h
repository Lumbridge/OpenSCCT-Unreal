#pragma once

#include "CoreMinimal.h"

// The test-only reads of the C# bridge for the client's named tests (Test/ScctTest.h): the running session's tick
// and netcode counters, where the client and the local server have each pawn, and the boot test's simulation check.
// These mirror Bridge/OpenScct.UnrealBridge/Test/TestExports.cs and TestTypes.cs field for field; change both together
// and bump the version (later test tasks append entries at the end of FScctTestApi). Bridge/Probes/TestProbe prints
// the C# sizes.
//
// Fetch the table through ScctFeature<FScctTestApi>() (Runtime/ScctFeatures.h). Call it on the game thread.

constexpr int32 ScctTestApiVersion = 2;

namespace EScctTestFlags
{
	constexpr int32 Remote = 1;        // the session is a round on a remote server
	constexpr int32 ServerStopped = 2; // the local server was stopped (StopServer): its state is exact now
	constexpr int32 ServerFailed = 4;  // the local server failed on its thread
}

namespace EScctTestPawnFlags
{
	constexpr int32 Local = 1;    // the pawn of the player at this machine
	constexpr int32 OnClient = 2; // the local client has it (ClientActorId and the client locations are set)
	constexpr int32 OnServer = 4; // the server's state is set (only once the server is readable)
	constexpr int32 Bot = 8;      // in a local match: any pawn but the local player's
}

/** The running session's counters. */
struct FScctTestCounters
{
	int32 SessionId;
	int32 Flags;              // EScctTestFlags
	int64 ClientTicks;        // client simulation ticks run
	int64 ServerTicks;        // the local server's ticks (a moment old while its thread runs; exact once stopped); -1 remote
	int64 CorrectionsApplied; // server corrections the local client applied to its own pawn
	int64 CorrectionsReceived;
	int64 Reinits;            // reinitialisations the server sent the local player; -1 while the server thread runs
	int32 ClientState;        // OpenScct.Server.EClientState
	int32 ClientPawns;        // pawns the client has a snapshot of
};
static_assert(sizeof(FScctTestCounters) == 56, "FScctTestCounters must match BridgeTestCounters");

/** One pawn of a local match. Client locations are the replicated ones of the last tick and of the tick before. */
struct FScctTestPawn
{
	int32 ServerActorId; // the server's actor id (the client's channel NetGUID)
	int32 ClientActorId; // the id GetPawns and the proxies use; 0 when the client lacks the pawn
	int32 Flags;         // EScctTestPawnFlags
	int32 MovesApplied;  // moves the server applied from this pawn's player; -1 unknown
	float ServerX, ServerY, ServerZ;
	float ClientX, ClientY, ClientZ;
	float PreviousX, PreviousY, PreviousZ; // equal to the Client* ones when the pawn was placed, reset or is new
};
static_assert(sizeof(FScctTestPawn) == 52, "FScctTestPawn must match BridgeTestPawn");

// ---- version 2: the netcode checks (Test/ScctNetcodeTests.cpp; Bridge/.../Test/NetLinkLaunch.cs) ----

namespace EScctNetFlags
{
	constexpr int32 Linked = 1;         // the session runs over the simulated link SetNextMatchLink set
	constexpr int32 Dedicated = 2;      // an isolated server with bot clients of its own; the session is a round on it
	constexpr int32 Stock = 4;          // the original's net profile, not OpenSCCT's tuned one
	constexpr int32 ScenarioDone = 8;   // the grab or co-op scenario finished
	constexpr int32 ScenarioFailed = 16; // it stopped; GetNetDetail says why
}

namespace EScctNetPawnFlags
{
	constexpr int32 Local = 1;
	constexpr int32 OnClient = 2;
	constexpr int32 OnServer = 4;
	constexpr int32 Bot = 8;
}

/** The link and netcode figures of a netcode check's match. */
struct FScctNetStats
{
	int32 SessionId;
	int32 Flags; // EScctNetFlags
	int32 LatencyMs; // the link profile's round trip
	int32 JitterMs;
	float LossPercent;
	float RttMs;    // the local client's smoothed round trip as its connection measures it; 0 before one
	float MinRttMs;
	int32 Stage;    // the scenario's stage, 0 for the walk
	int64 ClientTicks;
	int64 ServerTicks;
	int64 JoinedTick; // the client tick the local player first had a pawn on; -1 before
	int64 CorrectionsApplied;
	int64 CorrectionsReceived;
	int64 ServerReinits; // reinitialisations the server sent the local player
	int64 ClientResets;  // resets of any pawn the client saw since the join
	int64 LinkSent;
	int64 LinkLost;
	int64 LinkDelivered;
	int32 BotsJoined;
	int32 BotProblems; // bot clients that left or resynchronised their moves
	int32 HealthDrop;  // the grab scenario's merc health lost so far
	int32 SubjectActorId; // the server actor the scenario is about (grabbed merc, climber), or 0
};
static_assert(sizeof(FScctNetStats) == 128, "FScctNetStats must match BridgeNetStats");

/** One pawn of a netcode check's match: the server's location now and the client's, as drawn. */
struct FScctNetPawn
{
	int32 ServerActorId;
	int32 ClientActorId; // the proxies' id; 0 when the client lacks the pawn
	int32 Flags;         // EScctNetPawnFlags
	int32 Resets;        // resets the client saw of this pawn: a drawn body may jump only on one
	float ServerX, ServerY, ServerZ;
	float ClientX, ClientY, ClientZ;
	float Drift; // distance to the nearest place the server had the pawn over the last 1.5 s; -1 unknown
};
static_assert(sizeof(FScctNetPawn) == 44, "FScctNetPawn must match BridgeNetPawn");

struct FScctTestApi
{
	int32 Version;

	/** Writes the running session's counters. Returns its id, 0 when none runs (the counters zeroed). */
	int32 (__cdecl* GetCounters)(FScctTestCounters* Counters);

	/** Writes the session's pawns (with the server's state once readable). Returns the count written. */
	int32 (__cdecl* GetPawns)(FScctTestPawn* Pawns, int32 Capacity);

	/** Stops the local match's server so its state reads exactly; the client stops with it. 1, or 0 with LastError. */
	int32 (__cdecl* StopServer)();

	/** Loads the simulation assemblies and steps a new World Ticks ticks. Returns the ticks run, -1 with LastError. */
	int32 (__cdecl* StepWorld)(int32 Ticks);

	/** Copies the last failure's text. Returns its length. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);

	// Version 2.

	/**
	 * Sets the link of the next local start: Profile A, B, C, D, 100, 150, 200 or 300 (OpenScct.Net.SimLinkProfile),
	 * the original's net profile when bStock, a dedicated server with bot clients of its own when bDedicated, and the
	 * Scenario (walk; grab or coop need a dedicated server). The next start uses it up. 1, or 0 with LastError.
	 */
	int32 (__cdecl* SetNextMatchLink)(const TCHAR* Profile, int32 bStock, int32 bDedicated, const TCHAR* Scenario);

	/** Writes the linked match's figures. Returns its session id, 0 when no linked match runs. */
	int32 (__cdecl* GetNetStats)(FScctNetStats* Stats);

	/** Writes the linked match's pawns. Returns the count written. */
	int32 (__cdecl* GetNetPawns)(FScctNetPawn* Pawns, int32 Capacity);

	/** Copies the scenario's state or its failure. Returns its length. */
	int32 (__cdecl* GetNetDetail)(TCHAR* Buffer, int32 Capacity);
};
static_assert(sizeof(FScctTestApi) == 80, "FScctTestApi must match TestApi");
