#pragma once

#include "CoreMinimal.h"

// The light parity measurement's interface of the C# bridge: how lit the simulation believes a pawn at a point is,
// the original's baked light under the point, a choice of walkable sample spots, and a way to stand the local
// player on one. These mirror Bridge/OpenScct.UnrealBridge/Test/LightProbeExports.cs field for field; change both
// together and bump the version. Every field is 4 bytes wide, so the structs have no padding.
//
// Fetch the table through ScctFeature<FScctLightProbeApi>() (Runtime/ScctFeatures.h), on the game thread, while a
// match plays. Simulation units are Unreal centimetres, in Unreal's axes.

constexpr int32 ScctLightProbeApiVersion = 1;

namespace EScctLightSpotKind
{
	constexpr int32 Dark = 0;     // among the darkest walkable spots by the simulation's level
	constexpr int32 Bright = 1;   // among the brightest
	constexpr int32 Spread = 2;   // the levels between
	constexpr int32 EdgeLit = 3;  // the lit side of a sharp step in the level
	constexpr int32 EdgeDark = 4; // the dark side of the same step (the spot written just before it is its lit side)
}

struct FScctLightReading
{
	/**
	 * The bots' light level for a pawn at the point lit from every side, animated lights at their mean: the
	 * luminance of the original's pawn light term (0 black, 1 a white texture drawn at full white, at most 2).
	 */
	float Level;
	/** The same on the side of the pawn that faces the viewer, now: what the bots' sight uses. */
	float LevelFacing;
	/** Luminance of the zone ambient at the point (part of both levels). */
	float Ambient;
	/** The original's baked light term of the floor under the point (2 x the stored lightmap), and its luminance. */
	float BakedR, BakedG, BakedB, Baked;
	/** Height of the floor found under the point. */
	float FloorZ;
	/** The point's zone index, -1 unknown. */
	int32 Zone;
	/** 1 when the floor under the point is lightmapped level geometry (the Baked values are known). */
	int32 HasBaked;
};

static_assert(sizeof(FScctLightReading) == 40, "FScctLightReading must match BridgeLightReading");

struct FScctLightSpot
{
	/** The floor point. */
	float X, Y, Z;
	/** The bots' level for a standing pawn here. */
	float Level;
	/** EScctLightSpotKind */
	int32 Kind;
};

static_assert(sizeof(FScctLightSpot) == 20, "FScctLightSpot must match BridgeLightSpot");

struct FScctLightProbeApi
{
	int32 Version;

	/** Reads the map's lights and baked light: how many lights, 0 when the map has no light data, -1 without a loaded map. */
	int32 (__cdecl* Prepare)();

	/** The reading for a pawn of bounding radius Radius centred at (X, Y, Z), seen from the viewer; 1 when written. */
	int32 (__cdecl* Read)(float X, float Y, float Z, float Radius, float ViewerX, float ViewerY, float ViewerZ, FScctLightReading* OutReading);

	/**
	 * Chooses the sample spots: PerKind dark, bright and spread, and PerKind more in pairs across steps. Returns how
	 * many were written. Samples the map's floor first, which takes some seconds.
	 */
	int32 (__cdecl* FindSpots)(int32 PerKind, FScctLightSpot* OutSpots, int32 Capacity);

	/** Stands the local player's pawn with its centre at the point on the server's next step (a local match); 1 when queued. */
	int32 (__cdecl* PlacePlayer)(float X, float Y, float Z);
};
