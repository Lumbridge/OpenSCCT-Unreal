#pragma once

#include "CoreMinimal.h"

// The crash report table of the C# bridge (Bridge/OpenScct.UnrealBridge/Social/CrashExports.cs and CrashLog.cs, a port
// of the Godot client's App/CrashLog.cs): where the managed crash reports (crash-<utc>.log) go, what is being played
// (written into each report), and the test crash. Mirrors the bridge field for field; change both together and bump
// the version. Fetched with ScctFeature<FScctCrashApi>() (Runtime/ScctFeatures.h). Text is UTF-16, zero terminated.

constexpr int32 ScctCrashApiVersion = 1;

enum class EScctCrashText : int32
{
	Context = 0,  // what the reports say is being played
	Folder = 1,   // where the reports go
	LastPath = 2, // the last report written
	Notices = 3,  // lines for the log about reports written since the last call (taken)
};

struct FScctCrashApi
{
	int32 Version;
	/** Names the reports' folder. Returns 1. */
	int32 (__cdecl* Install)(const TCHAR* Folder);
	/** The map's id (empty: none), EGameMode (-1 unknown), the side (0 spy, 1 merc, else none) and where. Returns 1. */
	int32 (__cdecl* SetContext)(const TCHAR* Map, int32 Mode, int32 Side, const TCHAR* Where);
	/** A text (EScctCrashText), cut to fit; returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);
	/** 1: throws on a bridge worker thread (the process ends). 2: writes a report and goes on. */
	int32 (__cdecl* Test)(int32 Kind);
};
