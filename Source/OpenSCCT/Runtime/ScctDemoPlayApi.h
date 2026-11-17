#pragma once

#include "CoreMinimal.h"

// Demo playback and the demo library of the C# bridge (an OpenSCCT feature, docs/DEMOS.md): watching a .scctdemo
// with the playback bar's controls, cameras, players and timeline, and the Demos page's list of the demos on this
// computer and the master's recent online ones. These mirror Bridge/OpenScct.UnrealBridge/Demo/DemoPlayExports.cs
// field for field; change both together and bump the version. The demo-saving choice is the other demo table
// (Runtime/ScctDemoApi.h).
//
// The table is fetched like the other feature tables: ScctFeature<FScctDemoPlayApi>() (Runtime/ScctFeatures.h).
// Text is UTF-16, zero terminated. Game thread only.

constexpr int32 ScctDemoPlayApiVersion = 1;

/** The cameras (DemoCameraMode). */
enum class EScctDemoCamera : int32
{
	FirstPerson = 0,
	ThirdPerson = 1, // follows the watched player; the right mouse button orbits
	Free = 2,        // flies with the keys; the right mouse button looks
	AsPlayed = 3,    // the recorded player's own camera, aiming and gadget views included
};

/** FScctDemoPlayInfo::State. */
enum class EScctDemoPlayState : int32
{
	None = 0,
	Opened = 1,  // read and checked, waiting for its level (Start)
	Playing = 2, // in the bridge's main slot
};

namespace EScctDemoPlayFlags
{
	constexpr int32 Paused = 1;
	constexpr int32 Seeking = 2;
	constexpr int32 AtEnd = 4;
	constexpr int32 FromCheckpoint = 8; // this playback started at a checkpoint (a seek back)
	constexpr int32 Looking = 16;       // the right mouse button holds the orbit or the free look
	constexpr int32 CanLook = 32;       // the camera takes the mouse (third person, free)
}

/** Control's commands. */
enum class EScctDemoCommand : int32
{
	TogglePause = 0,
	SetPaused = 1,   // Arg 1 paused, 0 playing
	StepSpeed = 2,   // Arg +1 or -1
	SetSpeed = 3,    // Arg the speed's index: 0.25, 0.5, 1, 2, 4
	Skip = 4,        // Arg seconds, negative back
	Seek = 5,        // Arg a server tick
	Restart = 6,
	CycleCamera = 7,
	SetCamera = 8,   // Arg EScctDemoCamera
	CycleTarget = 9, // Arg +1 or -1
	WatchIndex = 10, // Arg a row of GetTargets
	WatchPlayer = 11,// Arg a player id
	SetLooking = 12, // Arg 1 while the right mouse button is down
	SeekEnd = 13,
	SeekMarker = 14, // Arg a row of GetMarkers (three seconds before it)
};

/** GetPlayText's kinds. */
enum class EScctDemoPlayText : int32
{
	Map = 0,
	Mode = 1,      // "Disk Hunt"
	Server = 2,
	Error = 3,     // why the last open, start or seek failed
	Watching = 4,  // the watched player's name, "Free camera" or "Waiting for a player"
	Path = 5,
	Hint = 6,      // the camera's controls (DemoHud's hint line)
	State = 7,     // "PLAYING", "PAUSED", "SEEKING", "FINISHED"
};

/** The demo's marker kinds (DemoMarkerKind). */
enum class EScctDemoMarkerKind : int32
{
	RoundStart = 0,
	RoundEnd = 1,
	Kill = 2,
	Objective = 3,
	MatchEnd = 4,
};

struct FScctDemoPlayInfo
{
	int32 State;        // EScctDemoPlayState
	int32 Flags;        // EScctDemoPlayFlags
	int32 SessionId;    // the main slot's session the playback is, 0 when not playing
	int32 Camera;       // EScctDemoCamera
	int32 SpeedIndex;   // 0 to 4: 0.25, 0.5, 1, 2, 4
	int32 TickRate;     // server ticks a second
	int32 FirstTick;    // the timeline: server ticks
	int32 LastTick;
	int32 CurrentTick;
	int32 SeekTarget;   // 0 when no seek runs
	int32 TargetIndex;  // the watched player's row of GetTargets, -1 for none
	int32 TargetCount;
	int32 MarkerCount;
	int32 Revision;     // goes up when a control or a seek changed something
};
static_assert(sizeof(FScctDemoPlayInfo) == 56, "FScctDemoPlayInfo must match the bridge's DemoPlayInfoBlock");

struct FScctDemoTarget
{
	int32 PlayerId;
	int32 Team;       // 0 spies, 1 mercs
	TCHAR Name[32];
};
static_assert(sizeof(FScctDemoTarget) == 72, "FScctDemoTarget must match the bridge's DemoTargetBlock");

struct FScctDemoMarker
{
	int32 Tick;
	int32 Kind;       // EScctDemoMarkerKind
	TCHAR Text[64];
};
static_assert(sizeof(FScctDemoMarker) == 136, "FScctDemoMarker must match the bridge's DemoMarkerBlock");

// ---- the library ----------------------------------------------------------------------------------------------------

/** GetEntries' collections. */
enum class EScctDemoCollection : int32
{
	Local = 0,  // Saved/Demos and its download cache
	Online = 1, // the master's recent demos, the page last asked for
};

/** FScctDemoLibraryInfo::OnlineState. */
enum class EScctDemoOnlineState : int32
{
	NoMaster = 0,
	SignedOut = 1,
	NotLoaded = 2,
	Loading = 3,
	Failed = 4,
	Ready = 5,
};

namespace EScctDemoEntryFlags
{
	constexpr int32 Cached = 1;        // a download that expires after seven days unless kept
	constexpr int32 Playable = 2;      // recorded on this build's protocol
	constexpr int32 HasScoreboard = 4; // the file carries its round's final scoreboard
	constexpr int32 Expired = 8;       // an online demo past its availability
}

namespace EScctDemoLibraryFlags
{
	constexpr int32 StatusIsError = 1;
	constexpr int32 Downloading = 2;
	constexpr int32 WatchReady = 4; // a download meant for watching finished: TakeWatchPath
}

struct FScctDemoEntry
{
	int32 Flags;           // EScctDemoEntryFlags
	int32 Mode;            // the game mode's number (EGameMode), -1 when not known
	int32 DurationSeconds;
	int32 PlayerCount;
	int64 StartUnixMs;     // when the recording started (local) or the round ended (online)
	int64 Bytes;
	int64 Id;              // the master's demo id (online), 0 for a local file
	int64 ExpiresUnixMs;   // online: when the master drops it; 0 for a local file
};
static_assert(sizeof(FScctDemoEntry) == 48, "FScctDemoEntry must match the bridge's DemoEntryBlock");

/** GetEntryText's kinds. */
enum class EScctDemoEntryText : int32
{
	Path = 0,
	FileName = 1,
	Map = 2,
	Server = 3,
	Players = 4,      // "Alice, Bob, Carol"
	GameVersion = 5,
	ModeName = 6,
	Outcome = 7,      // the scoreboard's "Spies win · objectives 2 of 3", empty without one
};

struct FScctDemoLibraryInfo
{
	int32 Flags;          // EScctDemoLibraryFlags
	int32 LocalCount;
	int32 OnlineState;    // EScctDemoOnlineState
	int32 OnlinePage;     // from 0
	int32 OnlinePages;
	int32 OnlineTotal;
	int32 OnlineCount;    // rows of this page
	int32 DownloadPercent;// -1 when not downloading or the size is not known
	int32 Revision;
	int32 Reserved;
};
static_assert(sizeof(FScctDemoLibraryInfo) == 40, "FScctDemoLibraryInfo must match the bridge's DemoLibraryInfoBlock");

/** GetLibraryText's kinds. */
enum class EScctDemoLibraryText : int32
{
	Status = 0,       // the last message: kept, deleted, downloading..., an error
	OnlineError = 1,  // why the online list could not be read
	SavedFolder = 2,
	CacheFolder = 3,
};

namespace EScctDemoScoreFlags
{
	constexpr int32 Bot = 1;
	constexpr int32 LeftEarly = 2;
}

struct FScctDemoScoreRow
{
	int32 Team;       // 0 spies, 1 mercs
	int32 Score;
	int32 Kills;
	int32 Deaths;
	int32 Objectives; // -1 when not counted
	int32 Flags;      // EScctDemoScoreFlags
	TCHAR Name[32];
};
static_assert(sizeof(FScctDemoScoreRow) == 88, "FScctDemoScoreRow must match the bridge's DemoScoreRowBlock");

struct FScctDemoPlayApi
{
	int32 Version;

	// ---- playback ----

	/** Reads and checks a demo (any playback before is stopped). Returns 1, or 0 with the reason in the Error text. Map gets the level to open. */
	int32 (__cdecl* Open)(const TCHAR* Path, int32 SeekTick, TCHAR* MapBuffer, int32 MapCapacity);

	/** From the demo's level: puts the playback in the main slot. Returns the session id, or 0 (Error text). */
	int32 (__cdecl* Start)();

	/** Once a frame before the main table's Update: a seek's fast-forward, the camera's return, the paused view. */
	void (__cdecl* Frame)(float RealSeconds);

	void (__cdecl* Stop)();

	int32 (__cdecl* GetPlayInfo)(FScctDemoPlayInfo* Info);

	void (__cdecl* Control)(int32 Command, int32 Arg);

	/** Mouse movement while looking (pixels). */
	void (__cdecl* Turn)(float DeltaX, float DeltaY);

	/** The free camera's keys (-1 to 1 each) for RealSeconds. */
	void (__cdecl* Fly)(float Forward, float Right, float Up, int32 bFast, float RealSeconds);

	int32 (__cdecl* GetTargets)(FScctDemoTarget* Rows, int32 Capacity);

	int32 (__cdecl* GetMarkers)(FScctDemoMarker* Rows, int32 Capacity);

	/** A text (EScctDemoPlayText). Returns its full length. */
	int32 (__cdecl* GetPlayText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	// ---- the library ----

	/** Reads the demo folders again. Returns the number of local demos. */
	int32 (__cdecl* RefreshLocal)();

	int32 (__cdecl* GetLibraryInfo)(FScctDemoLibraryInfo* Info);

	/** The rows of a collection (EScctDemoCollection). Returns the count written. */
	int32 (__cdecl* GetEntries)(int32 Collection, FScctDemoEntry* Rows, int32 Capacity);

	/** A text of a row (EScctDemoEntryText). Returns its full length. */
	int32 (__cdecl* GetEntryText)(int32 Collection, int32 Index, int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** Moves a local download into the kept demos. Returns 1 when done. */
	int32 (__cdecl* Keep)(int32 Index);

	/** Deletes a local demo (ask first: it cannot be undone). Returns 1 when done. */
	int32 (__cdecl* Delete)(int32 Index);

	/** Asks the master for a page of recent demos (signed in). */
	void (__cdecl* RequestOnline)(int32 Page);

	/** Downloads a recent demo; bKeep saves it, otherwise it is handed to TakeWatchPath. Returns 1 when started. */
	int32 (__cdecl* Download)(int32 Index, int32 bKeep);

	/** A finished download to watch, once. Returns its length, 0 when there is none. */
	int32 (__cdecl* TakeWatchPath)(TCHAR* Buffer, int32 Capacity);

	/** A local demo's final scoreboard. Returns the row count, -1 when it has none. */
	int32 (__cdecl* GetScoreboard)(int32 Index, FScctDemoScoreRow* Rows, int32 Capacity);

	/** A text (EScctDemoLibraryText). Returns its full length. */
	int32 (__cdecl* GetLibraryText)(int32 Kind, TCHAR* Buffer, int32 Capacity);
};
static_assert(sizeof(FScctDemoPlayApi) == 8 + 22 * sizeof(void*), "FScctDemoPlayApi does not match DemoPlayApi");
