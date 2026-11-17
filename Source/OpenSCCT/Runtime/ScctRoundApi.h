#pragma once

#include "CoreMinimal.h"

// What a round of an online visit shows around its play: the waiting list while the round loads (the Godot client's
// RoundStartList), the loading picture the server's seed picks, and what the results screen adds to the online
// table's results (each line's objectives, early leave and rank; the personal demo's state). Mirrors
// Bridge/OpenScct.UnrealBridge/Online/RoundExports.cs field for field; change both together and bump the version.
//
// It works on the online visit (Runtime/ScctOnlineApi.h). Text is UTF-16, zero terminated.

constexpr int32 ScctRoundApiVersion = 1;

/** FScctRoundWaitLine::State. */
enum class EScctRoundWaitState : int32
{
	Joining = 0,  // in the session, not on the round's map here yet: "Loading..."
	Loading = 1,  // on the map, still loading: "is connecting"
	Ready = 2,    // loaded and waiting for the round: "is ready"
	TimedOut = 3, // was in the session when the round began loading and was dropped: "Player timed out..."
};

/** One line of the round's waiting list (GetWaitList). */
struct FScctRoundWaitLine
{
	int32 State; // EScctRoundWaitState
	int32 Team;  // 0 spies, 1 mercs, 255 spectator
	TCHAR Name[48];
};

/** What the results screen adds to a line of FScctOnlineApi::GetResults, in the same order. */
struct FScctRoundResultExtra
{
	int32 Id;
	int32 Objectives; // objectives the player took, from the round's scoreboard; -1 when not known
	int32 LeftEarly;  // 1 when he left before the end
	int32 Rank;       // packed rank, 0 for none
};

/** GetDemoFlags. */
namespace EScctRoundDemoFlags
{
	constexpr int32 Recording = 1;   // the player records his matches
	constexpr int32 PendingSave = 2; // the round's demo is still recording or downloading: leaving now loses it
	constexpr int32 Saved = 4;       // a demo of this visit was saved (LastDemoPath)
}

/** The texts GetText reads. */
enum class EScctRoundText : int32
{
	DemoStatus = 0,     // the personal demo's state, as the results screen words it
	LastDemoPath = 1,   // the file of the last saved demo, or empty
	LoadingPicture = 2, // the full path of the round map's loading picture for the player's side, the server's seed's pick; empty when none
};

struct FScctRoundApi
{
	int32 Version;

	/** While a network round waits for its players: writes the waiting list, returns its count (only Capacity are written); -1 when no round waits. Call every frame while the visit is Loading (a player who leaves meanwhile is kept as timed out). */
	int32 (__cdecl* GetWaitList)(FScctRoundWaitLine* Lines, int32 Capacity);

	/** Writes the extras of the last results' lines. Returns their count, -1 when there are no results. */
	int32 (__cdecl* GetResultExtras)(FScctRoundResultExtra* Rows, int32 Capacity);

	/** EScctRoundDemoFlags; 0 when not online. */
	int32 (__cdecl* GetDemoFlags)();

	/** Copies a text (EScctRoundText) into Buffer. Returns its full length in characters. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** Unattended checks only (-ScctEndMatch): runs the local match's clock out on its server, so the round ends by time as a timed round does (one minute is set first when it has no limit). Returns 1 when queued; 0 on a remote round. */
	int32 (__cdecl* EndLocalRound)();
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctRoundWaitLine) == 104, "FScctRoundWaitLine does not match RoundWaitLine");
static_assert(sizeof(FScctRoundResultExtra) == 16, "FScctRoundResultExtra does not match RoundResultExtra");
static_assert(sizeof(FScctRoundApi) == 8 + 5 * sizeof(void*), "FScctRoundApi does not match RoundApi");
