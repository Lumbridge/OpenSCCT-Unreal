#pragma once

#include "CoreMinimal.h"

// The Stats, Match history and Achievements pages' table of the C# bridge (Bridge/OpenScct.UnrealBridge/Social/
// CareerExports.cs, CareerModel.cs and ScoreboardCapture.cs: the Godot client's StatsScreen, the match history of its
// DemosScreen, AchievementsScreen and ScoreboardCapture), and the scoreboard archive's folder. These mirror the bridge
// field for field; change both together and bump the version. Bridge/Probes/SocialProbe ("career") prints the sizes
// asserted below.
//
// Fetched like the other feature tables: ScctFeature<FScctCareerApi>() (Runtime/ScctFeatures.h). Every read takes the
// master's answers first. Text is UTF-16, zero terminated. Game thread only.

constexpr int32 ScctCareerApiVersion = 1;

namespace EScctCareerFlags
{
	constexpr int32 HasMaster = 1;
	constexpr int32 SignedIn = 2;
	constexpr int32 HistoryLoading = 4;
	constexpr int32 HistoryMore = 8;      // the master has more of the account's history
	constexpr int32 DetailsLoading = 16;
	constexpr int32 AccountKnown = 32;    // the master described the account: stats and progress are its
	constexpr int32 HasCareer = 64;       // the master gives detailed counters
	constexpr int32 Capturing = 128;      // the archive's folder is set: rounds played are saved
	constexpr int32 Preview = 256;
	constexpr int32 AccountLoading = 512;
}

struct FScctCareerInfo
{
	int32 Flags;              // EScctCareerFlags
	int32 Revision;           // goes up whenever anything here changed
	int32 Matches;            // rounds of the match history (saved and the master's, joined)
	int32 LocalMatches;       // of which played on this PC
	int32 Stats;              // stat rows
	int32 Achievements;
	int32 AchievementsEarned;
	int32 NextUnlock;         // the achievement the dashboard's next unlock names, -1 none
	int32 Saved;              // scoreboards saved this run
	int32 StatCategories;
	int32 Reserved0;
	int32 Reserved1;
};
static_assert(sizeof(FScctCareerInfo) == 48, "FScctCareerInfo must match the bridge's CareerInfoBlock");

namespace EScctCareerMatchFlags
{
	constexpr int32 Saved = 1;            // played (or imported) on this PC
	constexpr int32 Online = 2;           // the master knows it (details can be asked for)
	constexpr int32 Partial = 4;
	constexpr int32 Practice = 8;
	constexpr int32 LeftEarly = 16;
	constexpr int32 Completed = 32;
	constexpr int32 DetailsLoaded = 64;
	constexpr int32 HasTimeline = 128;
	constexpr int32 TimelineTruncated = 256;
	constexpr int32 Counted = 512;
	constexpr int32 HasReplay = 1024;
	constexpr int32 ReplayPlayable = 2048;
}

struct FScctCareerMatch
{
	int32 Flags;           // EScctCareerMatchFlags
	int32 Winner;          // 0 spies, 1 mercenaries, -1 none
	int32 Mode;
	int32 DurationSeconds;
	int32 Players;
	int32 Events;          // timeline events (details loaded)
	int32 LocalTeam;       // the player's side in it, -1 unknown
	int32 Reserved;
	int64 MatchId;         // the master's id, 0 none
	int64 StartedUnix;
};
static_assert(sizeof(FScctCareerMatch) == 48, "FScctCareerMatch must match the bridge's CareerMatchBlock");

namespace EScctCareerPlayerFlags
{
	constexpr int32 Local = 1;
	constexpr int32 Bot = 2;
	constexpr int32 LeftEarly = 4;
}

struct FScctCareerPlayer
{
	int32 Team;            // 0 spy, 1 mercenary
	int32 Score;
	int32 Kills;
	int32 Deaths;
	int32 Flags;           // EScctCareerPlayerFlags
	int32 Reserved;
};
static_assert(sizeof(FScctCareerPlayer) == 24, "FScctCareerPlayer must match the bridge's CareerPlayerBlock");

namespace EScctCareerAchievementFlags
{
	constexpr int32 Earned = 1;
	constexpr int32 Reported = 2;         // the master gave its progress
	constexpr int32 HasRewards = 4;
	constexpr int32 NextUnlock = 8;
}

struct FScctCareerAchievement
{
	int32 Category;        // General, Spy, Merc, Weapons, Gadgets, Map
	int32 Current;
	int32 Target;
	int32 Flags;           // EScctCareerAchievementFlags
	int64 EarnedUnix;
};
static_assert(sizeof(FScctCareerAchievement) == 24, "FScctCareerAchievement must match the bridge's CareerAchievementBlock");

/** GetString's kinds (the bridge's CareerString). */
enum class EScctCareerString : int32
{
	HistoryStatus = 0,
	HistoryError = 1,
	MatchMap = 2,          // Index: the round
	MatchOutcome = 3,      // "Mercenaries win · Practice · Left early"
	MatchMetadata = 4,     // "17 Nov 2026 · 21:05 · Story · 06:12"
	MatchServer = 5,       // the server's name, or "Offline game"
	MatchReplay = 6,
	MatchReplayPath = 7,
	MatchReason = 8,       // "Team eradicated · objectives 1 of 3"
	PlayerName = 9,        // Index: the round, Sub: the player
	EventTime = 10,        // Index: the round, Sub: the event
	EventText = 11,
	StatCategory = 12,     // Index: the row
	StatLabel = 13,
	StatValue = 14,
	StatCategoryName = 15, // Index: the category
	AchievementId = 16,    // Index: the achievement
	AchievementTitle = 17,
	AchievementDescription = 18,
	AchievementStatus = 19,
	AchievementStatusLong = 20,
	AchievementRewards = 21, // one cosmetic a line
	CategoryName = 22,     // Index: the achievement category
	CategoryNote = 23,
	StatsStatus = 24,
	AchievementsSummary = 25,
	DetailsError = 26,
	ArchiveFolder = 27,
	AchievementRewardIds = 28, // the visible cosmetic ids in the same order as AchievementRewards
	AchievementWeapon = 29, // the primary weapon whose mastery this achievement tracks
};

enum class EScctCareerCommand : int32
{
	RefreshHistory = 0,    // the archive read again, the master asked for the account's first page
	MoreHistory = 1,
	LoadDetails = 2,       // Index: the round
	RefreshAccount = 3,    // at most once a minute
	RefreshAccountNow = 4,
};

struct FScctCareerApi
{
	int32 Version;

	/** The archive's folder: from now on every round the player plays is saved there. */
	void (__cdecl* SetFolder)(const TCHAR* Folder);

	int32 (__cdecl* GetInfo)(FScctCareerInfo* Info);

	/** EScctCareerCommand. */
	void (__cdecl* Command)(int32 Kind, int32 Index);

	/** A text (EScctCareerString). Returns its full length. */
	int32 (__cdecl* GetString)(int32 Kind, int32 Index, int32 Sub, TCHAR* Buffer, int32 Capacity);

	int32 (__cdecl* GetMatch)(int32 Index, FScctCareerMatch* Match);
	int32 (__cdecl* GetPlayer)(int32 Index, int32 Row, FScctCareerPlayer* Player);
	int32 (__cdecl* GetAchievement)(int32 Index, FScctCareerAchievement* Achievement);
};
static_assert(sizeof(FScctCareerApi) == 8 + 7 * sizeof(void*), "FScctCareerApi does not match CareerApi");
