#pragma once

#include "CoreMinimal.h"

// The main menu dashboard of the C# bridge's social services (the Godot client's MenuDashboardService and
// MasterVersionCheck): the master's status (players online, announcement, message of the day), the game and hub lists,
// the changelog and the master's version line. These mirror Bridge/OpenScct.UnrealBridge/Social/MenuDashboard.cs and
// DashboardExports.cs field for field; change both together and bump the version. Bridge/Probes/SocialProbe
// ("dashboard") prints the sizes asserted below.
//
// Fetched like the other feature tables: ScctFeature<FScctDashboardApi>() (Runtime/ScctFeatures.h). The dashboard
// runs inside the social table's Update, so it moves only while UScctSocialSubsystem ticks. Text is UTF-16, zero
// terminated. Game thread only.

constexpr int32 ScctDashboardApiVersion = 1;

namespace EScctDashboardFlags
{
	constexpr int32 HasMaster = 1;
	constexpr int32 Watched = 2;               // a dashboard page is on screen: the feeds are polled
	constexpr int32 StatusLoading = 4;
	constexpr int32 StatusUnavailable = 8;
	constexpr int32 ChangelogLoading = 16;
	constexpr int32 ChangelogUnavailable = 32;
	constexpr int32 PlayersKnown = 64;         // PlayersOnline is filled
	constexpr int32 GamesKnown = 128;
	constexpr int32 HubsKnown = 256;
	constexpr int32 Preview = 512;             // made-up data (-ScctDashboardPreview)
	constexpr int32 HasUnlock = 1024;          // UnlockCurrent / UnlockTarget and the NextUnlock text are filled
}

struct FScctDashboardInfo
{
	int32 Flags;     // EScctDashboardFlags
	int32 Revision;  // goes up whenever anything here or in the texts changed (the account's revision included)
	int32 PlayersOnline;
	int32 GameSessions;
	int32 GamePlayers;
	int32 HubSessions;
	int32 HubPlayers;
	int32 Releases;  // releases in the changelog, newest first (at most five)
	int32 UnlockCurrent;
	int32 UnlockTarget;
	int32 Requests;  // requests started so far this run (all feeds)
	int32 Reserved;
};
static_assert(sizeof(FScctDashboardInfo) == 48, "FScctDashboardInfo must match the bridge's DashboardInfoBlock");

/** GetText's kinds. */
enum class EScctDashboardText : int32
{
	MasterLine = 0,         // "Master 1.0.0  build 20261027.1200 (abc1234)", "Master: checking...", "Master: not reached", "Master: none set"
	Announcement = 1,       // the player site's announcement (empty: none)
	Motd = 2,
	PlayersLine = 3,        // "Players online: 1,204" or "Players online: —"
	SessionsLine = 4,       // "3 games, 41 playing · 1 hub, 6 in them", or why it is not known
	LatestTitle = 5,        // the newest release's tag, or "Latest update"
	LatestPreview = 6,      // up to three highlights, or "Checking release notes..." and the like
	LatestState = 7,        // "12 Nov 2026" (" · Last received" when stale)
	ChangelogStatus = 8,    // the What's new page's status line
	RecentResults = 9,      // "Recent: W-L-W" (empty: none)
	NextUnlock = 10,        // "Next unlock · Desert Pattern · 12 / 25" (empty: none)
	AnnouncementStatus = 11,
};

struct FScctDashboardApi
{
	int32 Version;

	/** A page that shows the dashboard is on screen (1) or not (0): the feeds are polled only while it is. */
	void (__cdecl* SetWatched)(int32 bWatched);

	int32 (__cdecl* GetInfo)(FScctDashboardInfo* Info);

	/** A text (EScctDashboardText). Returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** The release at Index (newest first): tag, name, date, plain notes and its page. Returns the notes' full length (at least 1), 0 past the end. */
	int32 (__cdecl* GetRelease)(int32 Index, TCHAR* Tag, int32 TagCapacity, TCHAR* Name, int32 NameCapacity, TCHAR* Date, int32 DateCapacity,
		TCHAR* Notes, int32 NotesCapacity, TCHAR* Url, int32 UrlCapacity);

	/** Made-up data for checks: nothing is asked of the master. */
	void (__cdecl* SetPreview)(int32 bPreview);
};
static_assert(sizeof(FScctDashboardApi) == 8 + 5 * sizeof(void*), "FScctDashboardApi does not match DashboardApi");
