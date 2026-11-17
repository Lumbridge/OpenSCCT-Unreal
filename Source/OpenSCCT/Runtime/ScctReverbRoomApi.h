#pragma once

#include "CoreMinimal.h"

// The C# bridge's reverb room table: which zone a point is in and the way to it from the listener, and the bounded
// room-size probe, for the source-room and adaptive reverb of Play/ScctAudioRooms.cpp (ports of the Godot client's
// ReverbBuses.SourceRooms, ReverbBuses.Geometry and ReverbGeometryProbe). Mirrors
// Bridge/OpenScct.UnrealBridge/Events/ReverbRoomExports.cs; change both together and bump the version. Fetched with
// ScctFeature<FScctReverbRoomApi>() (Runtime/ScctFeatures.h).

constexpr int32 ScctReverbRoomApiVersion = 1;

namespace EScctReverbRoomFlags
{
	/** The zone has a ZoneInfo, whose place is in ZoneX..Z. */
	constexpr int32 HasZoneInfo = 1;
	/** The acoustic scene has a way to the zone from the listener. */
	constexpr int32 PathValid = 2;
}

/** Where a point is for the reverb (BridgeReverbRoom). */
struct FScctReverbRoom
{
	/** The BSP zone number (0: the default zone, solid space, or no collision yet). */
	int32 ZoneIndex;
	int32 Flags;                 // EScctReverbRoomFlags
	/** The zone's ZoneInfo actor's place: how the map's audio table names the zone's reverb. */
	float ZoneX, ZoneY, ZoneZ;
	/** The way from the listener through the map's openings: length, level and high-frequency gain. */
	float PathDistance, PathGain, PathHF;
	/** Openings on the way (0: the listener's own zone). */
	int32 Hops;
	/** The last opening on the way, where it enters the zone. */
	float OpeningX, OpeningY, OpeningZ;
	/** What the zone's reverb keeps on its way to the listener (Godot's AcousticScene.ReflectedGain). */
	float ReflectedGain;
};

/** The measured acoustic volume around a point (BridgeReverbGeometry, Godot's ReverbGeometry). */
struct FScctReverbGeometry
{
	int32 Valid;
	/** Cube root of the volume in metres. */
	float Size;
	float Volume, Area;
	/** Share of rays that found nothing within range. */
	float Openness;
	float Decay, HFDecayRatio, LFDecayRatio;
	float EarlyDelay, LateDelay;
	float Diffusion;
	/** How much of a reverb the space leaves (low in the open). */
	float WetGain;
	/** Rays traced and sweeps completed since the slot was reset. */
	int32 Rays, Sweeps;
};

static_assert(sizeof(FScctReverbRoom) == 52, "FScctReverbRoom must match BridgeReverbRoom");
static_assert(sizeof(FScctReverbGeometry) == 56, "FScctReverbGeometry must match BridgeReverbGeometry");

struct FScctReverbRoomApi
{
	int32 Version;

	/**
	 * Points is Count x 3 floats; writes Count rooms in their order. Listener is 3 floats; with UpdateScene 1 the
	 * acoustic scene's ways are first brought up to date from the listener (for when the acoustic table did not).
	 * Returns Count, or 0 when no session runs.
	 */
	int32 (__cdecl* Locate)(const float* Listener, float DeltaSeconds, int32 UpdateScene, const float* Points, int32 Count, FScctReverbRoom* Rooms);

	/**
	 * Traces up to MaxRays more rays of slot Slot's sweep (0..7) around Origin in zone ZoneIndex and writes the slot's
	 * latest estimate. Returns the rays traced, or -1 when no session runs.
	 */
	int32 (__cdecl* Probe)(int32 Slot, const float* Origin, int32 ZoneIndex, int32 MaxRays, FScctReverbGeometry* Geometry);

	/** Forgets a slot's sweep and estimate (-1: every slot). */
	void (__cdecl* Reset)(int32 Slot);
};
