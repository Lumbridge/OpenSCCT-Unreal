#pragma once

#include "CoreMinimal.h"

// Downloads from OpenSCCT-Content: the content index, a map's download with the host's content hash, the lobby's
// missing-map line with its Download button, and the prebuilt bot navigation offered before a start with bots.
// Mirrors Bridge/OpenScct.UnrealBridge/Online/ContentExports.cs field for field; change both together and bump the
// version.
//
// Text is UTF-16, zero terminated. A download is named by a key: a map code, or "nav:" and a map for its bot
// navigation.

constexpr int32 ScctContentApiVersion = 2;

/** FScctContentDownload::State. */
enum class EScctContentDownloadState : int32
{
	None = 0,
	Running = 1,
	Done = 2,
	Failed = 3,
	Stopped = 4,
};

/** FScctContentDownload::Flags of the lobby's line (GetLobbyFetch). */
namespace EScctContentLobbyFlags
{
	constexpr int32 Shown = 1;       // there is a line to show
	constexpr int32 CanDownload = 2; // a Download (or Retry) button is offered
	constexpr int32 Running = 4;     // a download is under way (a Stop button)
	constexpr int32 Problem = 8;     // the line is a failure, or a reason nothing can be downloaded
}

/** The texts GetText reads. */
enum class EScctContentText : int32
{
	IndexError = 0,      // why the content index could not be read, else empty
	Download = 1,        // the key's download: "Downloading Aquarius: 12.3 / 31.0 MB", its error or "... downloaded"
	LobbyLine = 2,       // the lobby's missing-map line
	InstalledFolder = 3, // where the key's download was installed, once done
	BotNavigation = 4,   // the key's (a map's) bot navigation question, or why none is offered
	Library = 5,         // where the content comes from and where downloads go (two lines)
	Summary = 6,         // the Content page's status line: loading, the index's failure, or the count of maps
	PacksFolder = 7,     // the folder downloads go into
};

/** CheckBotNavigation. */
enum class EScctBotNavigationState : int32
{
	None = 0,     // nothing to offer: start now
	Checking = 1, // the content index is still being read: ask again (start anyway after about three seconds)
	Offer = 2,    // a prebuilt copy can be downloaded: the BotNavigation text is the question
};

struct FScctContentDownload
{
	int32 State;      // EScctContentDownloadState
	int32 Generation; // moves on each time a map finished installing
	int64 Received;
	int64 Total;
	float Fraction;   // 0 to 1
	int32 Flags;      // GetLobbyFetch: EScctContentLobbyFlags
};

struct FScctContentApi
{
	int32 Version;

	/** Starts reading the index if due (Force: now). Returns 0 while loading, 1 read, 2 failed. */
	int32 (__cdecl* RefreshIndex)(int32 Force);

	/** Copies a text (EScctContentText) for a key into Buffer. Returns its full length in characters. */
	int32 (__cdecl* GetText)(int32 Kind, const TCHAR* Key, TCHAR* Buffer, int32 Capacity);

	/** Downloads a map; Hash (16 hex digits, or null) picks the host's version. Returns 1 started, 0 no such map, -1 index not read yet, -2 failure. */
	int32 (__cdecl* StartMapDownload)(const TCHAR* Map, const TCHAR* Hash);

	/** Stops a key's download. Returns 1 when one was running. */
	int32 (__cdecl* CancelDownload)(const TCHAR* Key);

	/** Describes a key's download. Returns its EScctContentDownloadState. */
	int32 (__cdecl* GetDownload)(const TCHAR* Key, FScctContentDownload* Out);

	/** The lobby's missing-map line (text: GetText LobbyLine). Returns its EScctContentLobbyFlags. */
	int32 (__cdecl* GetLobbyFetch)(FScctContentDownload* Out);

	/** The lobby's Download: fetches the setup's map with the host's content hash. Returns 1 started. */
	int32 (__cdecl* StartLobbyFetch)();

	/** Stops the lobby's download. */
	void (__cdecl* CancelLobbyFetch)();

	/** Whether to offer a map's prebuilt bot navigation. Returns EScctBotNavigationState. */
	int32 (__cdecl* CheckBotNavigation)(const TCHAR* Map);

	/** Downloads a map's prebuilt bot navigation (key "nav:" and the map). Returns 1 started. */
	int32 (__cdecl* StartBotNavigation)(const TCHAR* Map);

	/** Moves on each time a map finished installing. */
	int32 (__cdecl* GetGeneration)();

	// Version 2: the Maps & Content page (Menu/SScctMenuContent.cpp, Online/ScctContentCatalogue.h).

	/**
	 * Copies the page's rows, one per line, fields separated by tabs: kind (map, shared), key, title, category, size
	 * in bytes, state (available, downloaded, update, ingame, unlisted, notneeded), version, job (empty, running,
	 * done, stopped, failed), the job's line, its fraction, picture (PNG path), modes (comma separated), detail ('|'
	 * between lines). Returns the text's full length in characters.
	 */
	int32 (__cdecl* GetCatalogue)(TCHAR* Buffer, int32 Capacity);

	/** Downloads a shared pack (key "shared:" and its id). Returns 1 started, 0 no such pack, -1 index not read yet, -2 failure. */
	int32 (__cdecl* StartSharedDownload)(const TCHAR* Id);

	/** Removes a downloaded map, with why not in Error. Returns 1 removed, 0 not downloaded, -1 its download is running, -2 failed. */
	int32 (__cdecl* RemoveMap)(const TCHAR* Map, TCHAR* Error, int32 Capacity);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctContentDownload) == 32, "FScctContentDownload does not match ContentDownloadInfo");
static_assert(sizeof(FScctContentApi) == 8 + 14 * sizeof(void*), "FScctContentApi does not match ContentApi");
