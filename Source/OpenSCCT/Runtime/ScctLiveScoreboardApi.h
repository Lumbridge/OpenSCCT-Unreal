#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctLobbyApi.h"

// The running round's scoreboard (the Godot client's ScoreboardOverlay with RoundScoreboardView's live board): the
// playing client's latest snapshot from the server, whose public columns (lives, objectives, kills and deaths, ping)
// are refreshed each second and whose combat counters (spies: neutralisations, camera sleeps, shocker stuns, flashes;
// mercs: damage, mine kills, grenade kills, headshots) the server releases on its 30 second delay, with the line that
// counts down to the next release. Mirrors Bridge/OpenScct.UnrealBridge/Hud/LiveScoreboardExports.cs field for field;
// change both together and bump the version. Fetch it with ScctFeature<FScctLiveScoreboardApi>().
//
// Rows are the lobby table's FScctLobbyScoreRow (Runtime/ScctLobbyApi.h): the local player's side first, then the
// other, 24 at most, players who left last. On a server without detailed scoreboards the bridge makes the board from
// the HUD's own player list and the combat columns read "—". Ask for the board every frame from the session's start:
// the bridge notes when each snapshot arrives, which the countdown is counted from. Text is UTF-16, zero terminated.

constexpr int32 ScctLiveScoreboardApiVersion = 2;

/** The running round's scoreboard as a whole (LiveScoreboardInfo). */
struct FScctLiveScoreboardInfo
{
	int32 RowCount;
	/** The side of the first table (0 spies, 1 mercs): the local player's, the spies' for a spectator. */
	int32 FirstTeam;
	int32 SpyColumns;
	int32 MercColumns;
	/** EScctLiveScoreboardFlags. */
	int32 Flags;
	int32 LocalParticipantId;
	/** Players of the two sides the 24 rows had no room for. */
	int32 Omitted;
	float ElapsedSeconds;
	/** Changes whenever the rows or cells may have changed. */
	int32 Revision;
	int32 RoundSerial;
};

namespace EScctLiveScoreboardFlags
{
	constexpr int32 Detailed = 1; // the server sends detailed scoreboards
	constexpr int32 Practice = 2;
	constexpr int32 Partial = 4;
	constexpr int32 Completed = 8;
}

/** GetText's kinds (LiveScoreboardText). */
enum class EScctLiveScoreboardText : int32
{
	Title = 0,             // "Enhanced Aquarius (AquaE) · Practice"
	Clock = 1,             // "04:12"
	Timing = 2,            // "Combat stats delayed at least 30s · Next update 12.4s"; changes every frame
	ColumnLabel = 3,       // A = side, B = column
	ColumnDescription = 4, // A = side, B = column
	Cell = 5,              // A = row, B = column of the row's side
	Spectators = 6,        // "Spectators: a, b", or empty
	Omitted = 7,           // "3 earlier participants are included in final details", or empty
};

struct FScctLiveScoreboardApi
{
	int32 Version;

	/** Fills Info (may be null). Returns 1 when there is a board to show, else 0. */
	int32 (__cdecl* GetBoard)(FScctLiveScoreboardInfo* Info);

	/** Writes the rows (only Capacity of them). Returns their count, -1 without a board. */
	int32 (__cdecl* GetRows)(FScctLobbyScoreRow* Rows, int32 Capacity);

	/** A text of the board (EScctLiveScoreboardText, with its arguments A and B). Returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, int32 A, int32 B, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctLiveScoreboardInfo) == 40, "FScctLiveScoreboardInfo does not match LiveScoreboardInfo");
static_assert(sizeof(FScctLiveScoreboardApi) == 8 + 3 * sizeof(void*), "FScctLiveScoreboardApi does not match LiveScoreboardApi");

template <>
struct TScctFeatureInfo<FScctLiveScoreboardApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.LiveScoreboardExports"); }
	static constexpr int32 Version = ScctLiveScoreboardApiVersion;
};
