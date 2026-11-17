#pragma once

#include "CoreMinimal.h"

// The bridge's entry points for unattended checks of the level state: they make things happen on the server's
// world of a local match (doors opened, lights switched or shot out, props broken), so a run with nobody at the
// keyboard can show what Unreal draws for them. Mirrors Bridge/OpenScct.UnrealBridge/Devices/WorldDebugExports.cs;
// change both together. Only the level state's -ScctLevelDo switch uses it. Every entry returns 1 when the
// action was handed to the server and 0 when there is no local server (an online round) or no match.

// Version 2: FireEmitters added. 3: ShootNear added. 4: ThrowFlare and DirtyFeet added.
constexpr int32 ScctWorldDebugApiVersion = 4;

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
	/** Fires the tag of every map Emitter: the waiting ones start, the running ones stop, the bursts fire. */
	int32 (__cdecl* FireEmitters)();
	/**
	 * Puts a few bullet marks on each of the Count movers (Kind 0) or breakable props (Kind 1) nearest the point,
	 * through the impact manager as a gun's trace does, with the target as the marks' surface.
	 */
	int32 (__cdecl* ShootNear)(float X, float Y, float Z, int32 Count, int32 Kind);
	/**
	 * Throws a merc flare from the point along the direction, as the flare launcher does: it catches on what it hits
	 * and every machine that sees it hit leaves the burn mark.
	 */
	int32 (__cdecl* ThrowFlare)(float X, float Y, float Z, float DirX, float DirY, float DirZ);
	/**
	 * Gives every pawn of the CLIENT's world the dirty feet a sand floor gives, so their next steps leave footprints
	 * on any floor (the dirt is not replicated: each machine prints its own copies' steps). Returns the pawn count.
	 */
	int32 (__cdecl* DirtyFeet)();
};
