#pragma once

#include "CoreMinimal.h"

// The lobby's moderation: the host's or an admin's Kick, Ban and Voice Ban from the player rows (the Godot client's
// OnlineLobby), and a packed rank as the lobby shows it. Mirrors Bridge/OpenScct.UnrealBridge/Online/LobbyExports.cs
// field for field; change both together and bump the version.
//
// It works on the online visit (Runtime/ScctOnlineApi.h): player ids are FScctOnlinePlayer::Id. The server decides
// whether a request is allowed (the host or an admin, never on a bot or oneself); a refused one changes nothing. What
// happens comes back as the visit does: the kicked player leaves the list, a voice ban sets his VoiceBanned flag, and
// the server says it in a system chat line. Text is UTF-16, zero terminated.

constexpr int32 ScctLobbyApiVersion = 2;

// Version 2: the lobby's extras for every player (Bridge/OpenScct.UnrealBridge/Online/LobbyExtras.cs): the last round's
// scoreboard (the Godot lobby's "View scoreboard", RoundScoreboardDialog and RoundScoreboardModel), the setup's briefing
// (BriefingPanel and BriefingPages) and a rank's emblem colour and words (RankIcons).

/** The last round's scoreboard as a whole (LobbyScoreboardInfo). */
struct FScctLobbyScoreboardInfo
{
	/** The rows GetLastScoreboardRows writes: the local player's side first, then the other side. */
	int32 RowCount;
	int32 SpyColumns;
	int32 MercColumns;
	/** 0 spies, 1 mercs, 255 nobody. */
	int32 Winner;
	int32 Reason;
	/** EGameMode. */
	int32 Mode;
	/** EScctLobbyScoreboardFlags. */
	int32 Flags;
	int32 LocalParticipantId;
	int32 ObjectivesTaken;
	int32 ObjectivesNeeded;
	float ElapsedSeconds;
	/** Changes with every new scoreboard. */
	int32 Sequence;
};

namespace EScctLobbyScoreboardFlags
{
	constexpr int32 Completed = 1;
	constexpr int32 Practice = 2;
	constexpr int32 Partial = 4;
}

/** One line of the last round's scoreboard (LobbyScoreRow). */
struct FScctLobbyScoreRow
{
	int32 ParticipantId;
	int32 Team;
	int32 Score;
	int32 Kills;
	int32 Deaths;
	/** Lives left, -1 unlimited, below -1 unknown. */
	int32 Lives;
	/** Objectives taken, -1 unknown. */
	int32 Objectives;
	/** The packed rank, 0 for none. */
	int32 Rank;
	int32 PingMs;
	/** EScctLobbyScoreRowFlags. */
	int32 Flags;
	TCHAR Name[48];
};

namespace EScctLobbyScoreRowFlags
{
	constexpr int32 Bot = 1;
	constexpr int32 LeftEarly = 2;
	constexpr int32 Local = 4;
}

/** GetLastScoreboardText's kinds (LobbyScoreboardText). */
enum class EScctLobbyScoreboardText : int32
{
	Title = 0,             // "Enhanced Aquarius · Mercenaries win · Practice"
	Metadata = 1,          // "17 Nov 2026 21:04 · Server · 01:00"
	Timing = 2,            // "Final totals", "Partial record · ..."
	ColumnLabel = 3,       // A = side, B = column
	ColumnDescription = 4, // A = side, B = column
	Cell = 5,              // A = row, B = column of the row's side
	Details = 6,           // A = row: "Group\tLabel\tValue" lines
	Summary = 7,           // "Last game · Map\nOutcome"
	Spectators = 8,        // "Spectators: a, b", or empty
};

/** GetBriefingText's kinds (LobbyBriefingText). */
enum class EScctLobbyBriefingText : int32
{
	Title = 0,         // "Enhanced Aquarius - Story"
	PageTitle = 1,     // "MISSION MAIN OBJECTIVE"
	Objective = 2,
	ObjectiveName = 3,
	MapBrief = 4,
	ModeBrief = 5,
	Legend = 6,
	Picture = 7,       // the level picture, a full PNG path or empty
	Plan = 8,          // the briefing plan (else the mercs' loading picture), a full PNG path or empty
};

/** A place number on the briefing plan, at U across and V down (0..1 over the picture). */
struct FScctLobbyBriefingMark
{
	float U;
	float V;
	int32 Number;
};

struct FScctLobbyApi
{
	int32 Version;

	/** Asks the server to kick a player. Returns 1 when sent (a visit is on), 0 otherwise. */
	int32 (__cdecl* Kick)(int32 PlayerId);

	/** Asks the server to ban a player (kicked, and kept out by install id and address). Returns 1 when sent. */
	int32 (__cdecl* Ban)(int32 PlayerId);

	/** Asks the server to ban a player from voice chat (bBanned 1) or to lift the ban (0). Returns 1 when sent. */
	int32 (__cdecl* SetVoiceBanned)(int32 PlayerId, int32 bBanned);

	/** A packed rank (FScctOnlinePlayer::Rank) as text: "P2 41" with the title after a middle dot, "41", or empty for none. Returns its full length in characters. */
	int32 (__cdecl* RankText)(int32 PackedRank, TCHAR* Buffer, int32 Capacity);

	// ---- version 2 ----

	/** The last round's scoreboard of this visit: fills Info, returns 1; 0 when no round was played yet. */
	int32 (__cdecl* GetLastScoreboard)(FScctLobbyScoreboardInfo* Info);

	/** Writes its rows (the local player's side first). Returns their count (only Capacity are written), -1 without a scoreboard. */
	int32 (__cdecl* GetLastScoreboardRows)(FScctLobbyScoreRow* Rows, int32 Capacity);

	/** A text of the last scoreboard (EScctLobbyScoreboardText, with its arguments A and B). Returns its full length. */
	int32 (__cdecl* GetLastScoreboardText)(int32 Kind, int32 A, int32 B, TCHAR* Buffer, int32 Capacity);

	/** A text of the setup's briefing (EScctLobbyBriefingText) for Side 0 spies, 1 mercs, -1 the local player's. Returns its full length. */
	int32 (__cdecl* GetBriefingText)(int32 Kind, int32 Side, TCHAR* Buffer, int32 Capacity);

	/** Writes the place numbers of the briefing plan. Returns their count (only Capacity are written). */
	int32 (__cdecl* GetBriefingMarks)(FScctLobbyBriefingMark* Marks, int32 Capacity);

	/** A packed rank's emblem colour, 0xRRGGBB (white for none). */
	int32 (__cdecl* RankColour)(int32 PackedRank);

	/** A packed rank in words: Kind 0 the badge ("P3 · 42"), 1 the description ("Prestige 3, level 42 - Title"). Returns its full length. */
	int32 (__cdecl* RankWords)(int32 PackedRank, int32 Kind, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctLobbyApi) == 8 + 11 * sizeof(void*), "FScctLobbyApi does not match LobbyApi");
static_assert(sizeof(FScctLobbyScoreboardInfo) == 48, "FScctLobbyScoreboardInfo does not match LobbyScoreboardInfo");
static_assert(sizeof(FScctLobbyScoreRow) == 40 + 48 * 2, "FScctLobbyScoreRow does not match LobbyScoreRow");
static_assert(sizeof(FScctLobbyBriefingMark) == 12, "FScctLobbyBriefingMark does not match LobbyBriefingMark");
