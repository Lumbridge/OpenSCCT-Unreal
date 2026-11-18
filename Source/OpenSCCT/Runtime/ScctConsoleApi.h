#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The developer console's commands, from the C# bridge (Bridge/OpenScct.UnrealBridge/ConsoleExports.cs): a typed line
// is run by the Godot client's own console parser (UI/ConsoleCommands.cs, linked into the bridge), so its words, usage
// lines, help and rules are the same in both clients. Play/ScctConsoleCommands.cpp is the only caller.
//
// This mirrors ConsoleExports.cs; change both together and bump the version.

constexpr int32 ScctConsoleApiVersion = 1;

/** What Run made of a line (ConsoleResult). */
namespace EScctConsoleResult
{
	constexpr int32 Unknown = 0; // not a word of the console: Unreal's own commands get the line
	constexpr int32 Done = 1;    // run; the buffer holds what it printed, lines joined by '\n'
	constexpr int32 Quit = 2;    // quit or exit: Unreal closes the game
	constexpr int32 Client = 3;  // a word Unreal runs itself (the settings words, grabarea); nothing was done
}

struct FScctConsoleApi
{
	int32 Version;

	/** Runs a typed line. Writes what it printed (cut to fit, zero terminated) and returns the full length; Result is an EScctConsoleResult. */
	int32 (__cdecl* Run)(const TCHAR* Line, TCHAR* Buffer, int32 Capacity, int32* Result);

	/** The next line a server sent for the console (the answer to a command): 1 when one was written, 0 when none waits. */
	int32 (__cdecl* TakeLine)(TCHAR* Buffer, int32 Capacity);

	/** The text the last command asked to put on the clipboard (whereami copy), given once; returns its full length, 0 for none. */
	int32 (__cdecl* TakeCopy)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctConsoleApi) == 8 + 3 * sizeof(void*), "FScctConsoleApi must match ConsoleApi (64-bit)");

template <> struct TScctFeatureInfo<FScctConsoleApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.ConsoleExports"); }
	static constexpr int32 Version = ScctConsoleApiVersion;
};
