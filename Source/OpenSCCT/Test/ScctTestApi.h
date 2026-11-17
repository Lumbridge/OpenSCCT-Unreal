#pragma once

#include "CoreMinimal.h"

// The test-only reads of the C# bridge for the client's named tests (Test/ScctTest.h): the running session's tick
// and netcode counters, where the client and the local server have each pawn, and the boot test's simulation check.
// These mirror Bridge/OpenScct.UnrealBridge/Test/TestExports.cs and TestTypes.cs field for field; change both together
// and bump the version (later test tasks append entries at the end of FScctTestApi). Bridge/Probes/TestProbe prints
// the C# sizes.
//
// Fetch the table through ScctFeature<FScctTestApi>() (Runtime/ScctFeatures.h). Call it on the game thread.

constexpr int32 ScctTestApiVersion = 1;

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
};
static_assert(sizeof(FScctTestApi) == 48, "FScctTestApi must match TestApi");
