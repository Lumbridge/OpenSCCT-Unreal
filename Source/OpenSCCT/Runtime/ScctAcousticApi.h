#pragma once

#include "CoreMinimal.h"

// The C# bridge's acoustic table: sound through walls and openings for the 3D voices Unreal plays (the bridge's
// Events/AcousticVoices, AcousticScene and AcousticRules, ports of the Godot client's SoundSystem.Transmission,
// SoundSystem.Acoustics, AcousticScene, CornerPathSearch and rule 18). Mirrors
// Bridge/OpenScct.UnrealBridge/Events/AcousticExports.cs; change both together and bump the version. Fetched with
// ScctFeature<FScctAcousticApi>() (Runtime/ScctFeatures.h).

constexpr int32 ScctAcousticApiVersion = 1;

/** How a voice's sound reaches the listener through walls (the Audio page's "Sound through walls"). */
enum class EScctAcousticMode : int32
{
	/** Godot's: the wall's material cuts level and top end, and a blocked source is heard from its opening. */
	Transmission = 0,
	/** The original's DirectSound path (rule 18): occlusion only shrinks the minimum distance. */
	Original = 1,
	Off = 2,
};

namespace EScctAcousticSourceFlags
{
	/** Never occluded (a sound at the listener). */
	constexpr int32 NeverOccluded = 1;
}

namespace EScctAcousticResultFlags
{
	/** Something blocks the line from the listener. */
	constexpr int32 Blocked = 1;
	/** Heard by a way round: through openings, or round a corner of its own zone. */
	constexpr int32 Indirect = 2;
	/** That way is a corner (no opening of the map). */
	constexpr int32 Corner = 4;
	/** The line was traced again this frame. */
	constexpr int32 Traced = 8;
}

/** One 3D voice asked about this frame (BridgeAcousticSource). */
struct FScctAcousticSource
{
	/** The voice's own handle; the bridge keeps its state by it between frames and forgets it when it is not listed. */
	int32 Key;
	/** The actor whose sound it is (SoundOcclusion None exempts it), 0 for none. */
	int32 ActorId;
	int32 Flags;      // EScctAcousticSourceFlags
	float X, Y, Z;    // where the voice is
	float MinDistance;
	/** Nothing is heard past 100 x this (0: no limit). */
	float Radius;
};

/** What the voice should sound like this frame (BridgeAcousticResult). */
struct FScctAcousticResult
{
	int32 Key;
	int32 Flags;                // EScctAcousticResultFlags
	/** Rule 18: 1 - the occlusion factor of the line from the listener; 0 in the open. */
	float Occlusion;
	/** Rule 18: the minimum distance the occlusion leaves, eased over 1 s. */
	float OccludedMinDistance;
	/** Level and high-frequency gain (at 5 kHz) that reach the listener, after wall and way. */
	float DirectGain, DirectHF;
	/** DirectHF as the cut-off of the voice's low-pass filter; 20000 for none. */
	float LowPassHz;
	/** Volume multiplier on top of the voice's own: what it should get over what the attenuation curve gives at X, Y, Z. */
	float VolumeScale;
	/** Where the voice should be heard from: its own place, or the opening it is heard through. */
	float X, Y, Z;
	/** The length of the way the sound takes (the straight distance in the open). */
	float Distance;
	/** 0 straight, 1 fully by the way round (eased). */
	float PropagationMix;
	int32 Hops;
	int32 SourceZone;
};

/** The session's acoustic scene (BridgeAcousticInfo). */
struct FScctAcousticInfo
{
	int32 Portals;
	int32 Zones;
	int32 ListenerZone;
	int32 TrackedVoices;
	int64 LineChecks;
	int64 PortalProbes;
	/** Attenuated calls whose hearing reach a wall shrank by 0.8 ^ (ZoneDist^2), and the ones that put out of reach. */
	int64 ReachShrunk;
	int64 ReachDropped;
};

static_assert(sizeof(FScctAcousticSource) == 32, "FScctAcousticSource must match BridgeAcousticSource");
static_assert(sizeof(FScctAcousticResult) == 60, "FScctAcousticResult must match BridgeAcousticResult");
static_assert(sizeof(FScctAcousticInfo) == 48, "FScctAcousticInfo must match BridgeAcousticInfo");

struct FScctAcousticApi
{
	int32 Version;

	/**
	 * One frame of every 3D voice: Listener is 3 floats, Mode an EScctAcousticMode. Writes Count results in the
	 * sources' order. Returns Count, or 0 when no session runs.
	 */
	int32 (__cdecl* Update)(const float* Listener, float DeltaSeconds, int32 Mode, const FScctAcousticSource* Sources, int32 Count,
		FScctAcousticResult* Results);

	/** Fills the scene's counts. Returns 1, or 0 when no session runs. */
	int32 (__cdecl* GetInfo)(FScctAcousticInfo* Info);
};
