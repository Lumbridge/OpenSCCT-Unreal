#pragma once

#include "CoreMinimal.h"

// What the menus need from the C# bridge with no match running: the maps a match can be hosted on (titles, modes
// and their rules, pictures, briefing texts), the game's own interface strings, and the bindable actions with the
// keys that do them. These mirror Bridge/OpenScct.UnrealBridge/Menu/MenuExports.cs field for field; change both
// together and bump the version.
//
// Fetch the table once: ScctFeature<FScctMenuApi>() (Runtime/ScctFeatures.h). Every entry works without a session;
// the map and string entries need the content folder the bridge was configured with.
// Text is UTF-16, zero terminated; the text getters copy what fits and return the full length.

constexpr int32 ScctMenuApiVersion = 1;

/** One mode of a map as the match setup offers it. */
struct FScctMenuMode
{
	int32 Playable;       // 1 when the map has what the mode plays with
	int32 TimeLimit;      // minutes, 0 for no limit
	int32 SpyLives;       // 0 for unlimited (and in Merc Deathmatch, where ScoreLimit is the kills to win)
	int32 MercLives;
	int32 RespawnSeconds;
	int32 ScoreLimit;
};

namespace EScctMenuMapFlags
{
	constexpr int32 CanPlay = 1; // the map has the converted content a round is played on
}

/** A map of the host list (GetMaps). */
struct FScctMenuMap
{
	int32 Flags; // EScctMenuMapFlags
	int32 RecommendedPlayers;
	int32 SpyStarts, MercStarts;
	FScctMenuMode Modes[4]; // Story, Disk Hunt, Deathmatch, Merc Deathmatch
	TCHAR Id[32];           // the folder name ("AquaE")
	TCHAR Title[64];        // the name every page shows ("ENHANCED AQUARIUS")
};

/** GetMapText's kinds. */
enum class EScctMenuMapText : int32
{
	Description = 0,
	PreviewImage = 1,     // PNG path of the map list picture, empty when the map has none
	BannerImage = 2,      // PNG path of the 16:9 picture
	LoadingSpyImage = 3,
	LoadingMercImage = 4,
	Legend = 5,           // date and place lines, LF between them
	BriefSpy = 6,
	BriefMerc = 7,
	Places = 8,
};

namespace EScctMenuSides
{
	constexpr int32 Spy = 1;
	constexpr int32 Merc = 2;
	constexpr int32 DoubleTap = 4;
}

/** An action a key or pad control can be bound to (GetActions). */
struct FScctMenuAction
{
	int32 Sides; // EScctMenuSides
	TCHAR Id[32];
	TCHAR Category[24];
	TCHAR Label[64];
};

/** GetText's kinds. */
enum class EScctMenuText : int32
{
	GameVersion = 0, // OpenSCCT's release version the bridge was built from
	BuildNumber = 1,
	ContentRoot = 2,
	SharedRoot = 3,  // the content's shared folder (interface sounds and pictures)
	Language = 4,
};

struct FScctMenuApi
{
	int32 Version;

	/** Writes the host list, in its order. Returns the count (only Capacity are written). */
	int32 (__cdecl* GetMaps)(FScctMenuMap* Maps, int32 Capacity);

	int32 (__cdecl* GetMapText)(const TCHAR* Map, int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** What a map says about a mode (EGameMode: 4 Disk Hunt, 5 Story, 6 Deathmatch, 7 Merc Deathmatch) for a side. Kind 0: the one-line objective; 1: the briefing paragraph. */
	int32 (__cdecl* GetModeText)(const TCHAR* Map, int32 Mode, int32 bMerc, int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** A string of the game's .int files (System/<File>.int, [Section], Key); 0 when the content has none. */
	int32 (__cdecl* Localize)(const TCHAR* File, const TCHAR* Section, const TCHAR* Key, TCHAR* Buffer, int32 Capacity);

	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	int32 (__cdecl* GetActions)(FScctMenuAction* Actions, int32 Capacity);

	/** The keys (bPad: the pad controls) of one side that do Action, LF between them. */
	int32 (__cdecl* GetActionKeys)(int32 bMerc, int32 bPad, const TCHAR* Action, TCHAR* Buffer, int32 Capacity);

	/** The labels of the side's actions Key does now, LF between them: what binding Key anew would take away. */
	int32 (__cdecl* GetKeyActions)(int32 bMerc, int32 bPad, const TCHAR* Key, TCHAR* Buffer, int32 Capacity);

	/**
	 * Binds Action of one side to Key (null or empty: only clears). Key then does Action and nothing else.
	 * Returns 1, 0 for an unknown action or key name. Save with
	 * the controls table's SaveBindings.
	 */
	// Mode 0 replaces all slots, 1 keeps them, 2 edits only ordinary presses, 3 edits only double taps.
	int32 (__cdecl* BindAction)(int32 bMerc, int32 bPad, const TCHAR* Action, const TCHAR* Key, int32 Mode);

	/** Forgets the map catalog and the strings, so content converted since is listed. */
	void (__cdecl* RefreshMaps)();
};

static_assert(sizeof(FScctMenuMode) == 24, "FScctMenuMode does not match BridgeMenuMode");
static_assert(sizeof(FScctMenuMap) == 304, "FScctMenuMap does not match BridgeMenuMap");
static_assert(sizeof(FScctMenuAction) == 244, "FScctMenuAction does not match BridgeMenuAction");
static_assert(sizeof(FScctMenuApi) == 8 + 10 * sizeof(void*), "FScctMenuApi does not match MenuApi");
