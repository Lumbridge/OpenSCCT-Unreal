#pragma once

#include "CoreMinimal.h"

// The bridge's entry points for unattended checks of the level state: they make things happen on the server's
// world of a local match (doors opened, lights switched or shot out, props broken), so a run with nobody at the
// keyboard can show what Unreal draws for them. Mirrors Bridge/OpenScct.UnrealBridge/Devices/WorldDebugExports.cs;
// change both together. Only the level state's -ScctLevelDo switch uses it. Every entry returns 1 when the
// action was handed to the server and 0 when there is no local server (an online round) or no match.

constexpr int32 ScctWorldDebugApiVersion = 1;

struct FScctWorldDebugApi
{
	int32 Version;

	/** Fires the tag of every mover, as a switch or a trigger would. */
	int32 (__cdecl* FireMovers)();
	/** Fires the tag of every switched light: each one toggles. */
	int32 (__cdecl* FireLights)();
	/** Breaks the Count lit switched lights nearest the point, as a shot does: half a second of flicker, then dark. */
	int32 (__cdecl* BreakLights)(float X, float Y, float Z, int32 Count);
	/** Destroys the Count breakable props nearest the point (lamps, vents, glass panes). */
	int32 (__cdecl* BreakProps)(float X, float Y, float Z, int32 Count);
};
