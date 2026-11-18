#pragma once

#include "CoreMinimal.h"

// The light parity measurement's interface of the C# bridge: how lit the simulation believes a pawn at a point is,
// the original's baked light under the point, a choice of walkable sample spots, and a way to stand the local
// player on one. These mirror Bridge/OpenScct.UnrealBridge/Test/LightProbeExports.cs field for field; change both
// together and bump the version. Every field is 4 bytes wide, so the structs have no padding.
//
// Fetch the table through ScctFeature<FScctLightProbeApi>() (Runtime/ScctFeatures.h), on the game thread, while a
// match plays. Simulation units are Unreal centimetres, in Unreal's axes.

// Version 2: FindSpots takes the floor rule and says what it sampled; PredictSphere and FindSwitchSpots added.
// Version 3: CheckRig added.
constexpr int32 ScctLightProbeApiVersion = 3;

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

/** What the last FindSpots sampled. */
struct FScctLightFloor
{
	/** Walkable nodes of the grid, those on a connected floor a map light hangs over, and those sampled. */
	int32 FloorNodes, LitFloorNodes, PlayNodes;
	/** Starts, objectives, flag zones and switches the play floor was grown from. */
	int32 Anchors;
	/** 1 when the play floor was sampled, 0 when any connected floor under a map light was. */
	int32 PlayRule;
};

static_assert(sizeof(FScctLightFloor) == 20, "FScctLightFloor must match BridgeLightFloor");

/** A switched light of the map (an STriggerLight) and the two spots of the switch test. */
struct FScctLightSwitch
{
	/** The light's name in lighting.json (its actor id), zero terminated. */
	char Name[48];
	float LightX, LightY, LightZ;
	/** How far the light reaches (cm) and its LightBrightness. */
	float Radius, Brightness;
	/** The floor point where a standing pawn's level changes most, its level now and with every switched light toggled. */
	float NearX, NearY, NearZ, NearLevel, NearToggled;
	/** A floor point beyond the radius where the level does not change (only when HasFar is 1), and its level. */
	float FarX, FarY, FarZ, FarLevel;
	int32 HasFar;
	/** 1 when the light is on now. */
	int32 On;
	/** The light's LightEffect (22 a projector). */
	int32 Effect;
};

static_assert(sizeof(FScctLightSwitch) == 116, "FScctLightSwitch must match BridgeLightSwitch");

/** How the lighting plan's actor light rig (Runtime/ScctLightPlanApi.h) agrees with the bots' light field over the sampled floor. */
struct FScctLightRigCheck
{
	/** Floor nodes compared (a standing pawn's centre over each). */
	int32 Nodes;
	/**
	 * The rig asked as the field is built (map lights only, where the map placed them): the largest difference of the
	 * level and of the level facing a viewer, and how many nodes differ at all. Both run the same rules: 0 is expected.
	 */
	float MaxLevel, MaxFacing;
	int32 Differ;
	/** The rig as the game asks it (lights where they are now, runtime lights in): the largest difference of the level, and how many nodes differ. */
	float LiveMaxLevel;
	int32 LiveDiffer;
	/** Nodes whose rig is full (four lights), and nodes where a chosen light has a blocked line to the pawn. */
	int32 Full, Occluded;
};

static_assert(sizeof(FScctLightRigCheck) == 32, "FScctLightRigCheck must match BridgeLightRigCheck");

struct FScctLightProbeApi
{
	int32 Version;

	/** Reads the map's lights and baked light: how many lights, 0 when the map has no light data, -1 without a loaded map. */
	int32 (__cdecl* Prepare)();

	/** The reading for a pawn of bounding radius Radius centred at (X, Y, Z), seen from the viewer; 1 when written. */
	int32 (__cdecl* Read)(float X, float Y, float Z, float Radius, float ViewerX, float ViewerY, float ViewerZ, FScctLightReading* OutReading);

	/**
	 * Chooses the sample spots: PerKind dark, bright and spread, and PerKind more in pairs across steps. Returns how
	 * many were written. Samples the map's floor first, which takes some seconds. Only the play floor is sampled (what
	 * can be walked or dropped to from a start, an objective, a flag zone or a switch) unless AllFloor is not 0 (any
	 * connected floor a map light hangs over, the first version's rule). OutFloor, when given, says what was sampled.
	 */
	int32 (__cdecl* FindSpots)(int32 PerKind, int32 AllFloor, FScctLightSpot* OutSpots, int32 Capacity, FScctLightFloor* OutFloor);

	/** Stands the local player's pawn with its centre at the point on the server's next step (a local match); 1 when queued. */
	int32 (__cdecl* PlacePlayer)(float X, float Y, float Z);
	/**
	 * The display colour (0 to 1, three floats) the original's pawn lighting gives the middle of a matte sphere
	 * centred at (X, Y, Z) and seen from the viewer: a texture of display value AlbedoDisplay under the simulation's
	 * term per pixel (zone ambient, the four strongest lights by N.L, projectors, capped at 2), over the disc within
	 * DiscShare of the sphere's radius. The picture the lighting plan gives a body-class surface. 1 when written.
	 */
	int32 (__cdecl* PredictSphere)(float X, float Y, float Z, float Radius, float ViewerX, float ViewerY, float ViewerZ, float AlbedoDisplay,
		float DiscShare, float* OutRgb);

	/** The switched lights of the map with the spots of the switch test, on the floor the last FindSpots sampled; how many were written. */
	int32 (__cdecl* FindSwitchSpots)(FScctLightSwitch* OutSwitches, int32 Capacity);

	/** Compares the plan's actor light rig with the bots' light field on every floor node the last FindSpots sampled; 1 when written. */
	int32 (__cdecl* CheckRig)(FScctLightRigCheck* OutCheck);
};
