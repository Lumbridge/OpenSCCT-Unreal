#pragma once

#include "CoreMinimal.h"

// The bridge's entry points for unattended checks of the goggles: they move pawns on the server's world of a local
// match, so a run with nobody at the keyboard can show what motion vision does with a pawn that moves and one that
// stands still. Mirrors Bridge/OpenScct.UnrealBridge/Devices/VisionDebugExports.cs; change both together. Only the
// vision subsystem's -ScctVisionBots switch uses it. Every entry returns 1 when the action was handed to the server
// and 0 when there is no local server (an online round) or no match.

// Version 1: StandSpyBots and GetReport.
constexpr int32 ScctVisionDebugApiVersion = 1;

struct FScctVisionDebugApi
{
	int32 Version;

	/**
	 * For HoldSeconds walks the nearest spy bot back and forth across the merc player's view in front of him, faster
	 * than the tracker's 150 uu/s, and holds the next nearest still further in and to the side. Called again, starts over.
	 */
	int32 (__cdecl* StandSpyBots)(float HoldSeconds);

	/** Writes what the last action did on the server (NUL terminated) and returns how many actions have reported. */
	int32 (__cdecl* GetReport)(TCHAR* Buffer, int32 Capacity);
};
