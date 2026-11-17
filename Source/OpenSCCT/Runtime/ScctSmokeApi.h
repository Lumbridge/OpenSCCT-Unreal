#pragma once

#include "CoreMinimal.h"

// Responsive smoke: when the match runs SvmResponsiveSmoke, a smoke grenade lets out a shared simulated cloud (the
// simulation's ResponsiveSmokeField) that walls hold back, that grows along the free air and that shots and blasts
// push holes through; gas, coughing and AI sight read the same cloud. The bridge says whether it is on, lists the
// clouds of the client's world with the box each one's grid covers and samples one cloud's density onto a grid of the
// caller's size, at the shared clock. Mirrors Bridge/OpenScct.UnrealBridge/Events/SmokeExports.cs; change both
// together.

constexpr int32 ScctSmokeApiVersion = 1;

/** The largest density grid the bridge samples (cells per axis). */
constexpr int32 ScctSmokeMaxResolution = 64;

struct FScctSmokeState
{
	/** 1 when the match runs responsive smoke. */
	int32 Enabled;
	/** Clouds the client's world holds now. */
	int32 FieldCount;
	/** The simulation's cells per axis of a cloud (25). */
	int32 Dimension;
	/** Density grids written since the session started. */
	int32 Uploads;
	/** The shared clock the densities are evaluated at, seconds. */
	float Time;
	/** One simulation cell's edge, cm. */
	float CellSize;
	/** Optical depth per cm through smoke of density 1, as gas and AI see it. */
	float Extinction;
	/** Milliseconds the last density grid took to sample. */
	float LastSampleMilliseconds;
};

static_assert(sizeof(FScctSmokeState) == 32, "FScctSmokeState must match BridgeSmokeState");

struct FScctSmokeField
{
	/** The client actor id of the cloud's emitter: the id the events feature lists that emitter (SSmokeEmitter) by. */
	int32 EmitterId;
	/** Changes when the cloud's cells, arrival times or holes change. */
	uint32 Revision;
	/** The lowest corner of the box the grid covers and the box's edge, cm. */
	float OriginX, OriginY, OriginZ;
	float Size;
	/** Where the cloud blooms from, cm. */
	float CenterX, CenterY, CenterZ;
	/** Seconds since it was let out, and seconds it lasts in all. */
	float Age, Lifetime;
	/** Holes shots and blasts have pushed through it that are open or refilling. */
	int32 Carves;
	int32 Reserved;
};

static_assert(sizeof(FScctSmokeField) == 52, "FScctSmokeField must match BridgeSmokeField");

/**
 * A hole a shot pushes through a cloud, as it is now: the segment it runs along in the cloud box's frame (cm from the
 * box's lowest corner), its radius and soft edge, and how much smoke it holds back now (1 open, 0 refilled).
 */
struct FScctSmokeHole
{
	float StartX, StartY, StartZ, Radius;
	float EndX, EndY, EndZ, Strength;
	float Feather;
	uint32 Sequence;
};
static_assert(sizeof(FScctSmokeHole) == 40, "FScctSmokeHole must match BridgeSmokeHole");

struct FScctSmokeApi
{
	int32 Version;

	/** Fills the state. Returns 1, or 0 without a session. */
	int32 (__cdecl* GetState)(FScctSmokeState* OutState);
	/** Writes the clouds of the client's world, by emitter id. Returns the count written. */
	int32 (__cdecl* GetFields)(FScctSmokeField* OutFields, int32 Capacity);
	/**
	 * Writes one cloud's density on a Resolution^3 grid over its box (X fastest, then Y, then Z; 0..255 for 0..1),
	 * each value sampled at its grid cell's centre. Returns 1 when written, 0 when the cloud is gone or Bytes is short.
	 */
	int32 (__cdecl* GetDensity)(int32 EmitterId, int32 Resolution, uint8* OutDensity, int32 Bytes);
	/**
	 * Writes one cloud's open shot holes, newest first. A hole is finer than any grid, so the drawing carves it
	 * exactly; a blast's wide clearing is in the grid. Returns the count written.
	 */
	int32 (__cdecl* GetHoles)(int32 EmitterId, FScctSmokeHole* OutHoles, int32 Capacity);
};
