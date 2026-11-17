#pragma once

#include "CoreMinimal.h"

// The demo half of the C# bridge (an OpenSCCT addition, docs/DEMOS.md): the player's demo-saving choice for offline
// matches and online rounds, and its status. These mirror Bridge/OpenScct.UnrealBridge/Demo/DemoExports.cs and
// DemoSaving.cs field for field; change both together and bump the version.
//
// The table is fetched like the other feature tables: ScctFeature<FScctDemoApi>() (Runtime/ScctFeatures.h).
// Text is UTF-16, zero terminated. Game thread only.

constexpr int32 ScctDemoApiVersion = 1;

namespace EScctDemoFlags
{
	constexpr int32 RecordOffline = 1;
	constexpr int32 RecordOnline = 2;
	constexpr int32 RecordingOffline = 4; // the local match being played records a demo
	constexpr int32 VisitRecords = 8;     // the online visit asked its server for personal demos
	constexpr int32 PendingSave = 16;     // a round of the visit is recording or its demo is still downloading
	constexpr int32 PendingTraining = 32; // the demo is saved, its training data is still downloading
	constexpr int32 HasOnlineDemo = 64;
	constexpr int32 HasOfflineDemo = 128;
}

namespace EScctDemoConfigure
{
	constexpr int32 Offline = 1; // save local matches
	constexpr int32 Online = 2;  // save online rounds (a server with personal demos sends each one down)
}

/** GetText's kinds. */
enum class EScctDemoText : int32
{
	Folder = 0,
	OnlineStatus = 1,        // "Your upcoming matches will be saved to Demos on this computer.", the server's answer...
	OfflineStatus = 2,
	CurrentOfflineDemo = 3,  // the file the local match being played records into
	LastOfflineDemo = 4,
	LastOnlineDemo = 5,
};

struct FScctDemoInfo
{
	int32 Flags;    // EScctDemoFlags
	int32 Revision; // goes up whenever anything here or in the texts changed
};
static_assert(sizeof(FScctDemoInfo) == 8, "FScctDemoInfo must match the bridge's DemoInfoBlock");

struct FScctDemoApi
{
	int32 Version;

	/** The demo folder and which matches are saved (EScctDemoConfigure). A visit in progress is told at once. */
	void (__cdecl* Configure)(const TCHAR* Folder, int32 Flags);

	int32 (__cdecl* GetInfo)(FScctDemoInfo* Info);

	/** A text (EScctDemoText). Returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);
};
static_assert(sizeof(FScctDemoApi) == 8 + 3 * sizeof(void*), "FScctDemoApi does not match DemoApi");
