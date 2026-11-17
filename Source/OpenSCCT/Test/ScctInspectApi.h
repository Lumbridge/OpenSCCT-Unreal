#pragma once

#include "CoreMinimal.h"

// The play inspector's reads of the C# bridge (Test/ScctPlayInspector.h): each client pawn's animation channel 0 and
// the channels whose sequence has no clip, the local pawn's snaps, the sound counts, and traces against the client
// world's collision. These mirror Bridge/OpenScct.UnrealBridge/Test/InspectExports.cs and InspectTypes.cs field for
// field; change both together and bump the version.
//
// Fetch the table through ScctFeature<FScctInspectApi>() (Runtime/ScctFeatures.h). Call it on the game thread.

constexpr int32 ScctInspectApiVersion = 1;

namespace EScctInspectPawnFlags
{
	constexpr int32 Local = 1;
	constexpr int32 Crouched = 2;
	constexpr int32 Posed = 4;      // the bridge has posed it (character file found, ticked)
	constexpr int32 Visible = 8;    // the simulation shows it
	constexpr int32 Walking = 16;   // PHYS_Walking
	constexpr int32 Falling = 32;   // PHYS_Falling
	constexpr int32 JustLanded = 64; // walking, but falling a few ticks ago: the body still blends out of the landing
}

/** The session's sound and local-snap figures. */
struct FScctInspectCounters
{
	int32 SessionId;
	int32 Snaps;        // client ticks the local pawn moved over 40 uu beyond what its velocity explains
	float WorstSnap;    // the largest unexplained one-tick move (uu)
	int32 HasSoundLibrary;
	int64 ClientTicks;
	int64 SoundEvents;  // PlaySound events the client's sound resolver saw
	int64 SoundsStarted; // of those, the ones resolved to a file and handed to Unreal
	int64 SoundsUnknown; // of those, the ones naming a sound the library does not have
	int64 LocalMovingTicks;
};
static_assert(sizeof(FScctInspectCounters) == 56, "FScctInspectCounters must match BridgeInspectCounters");

/** One client pawn as the simulation has it at the last tick. */
struct FScctInspectPawn
{
	int32 ActorId; // the client id the bodies use
	int32 Flags;   // EScctInspectPawnFlags
	int32 Channel0Name; // name id (GetName), 0 for none
	float Channel0Frame;
	float Channel0Rate;
	int32 Channel0Frames;
	int32 MissingSimClips;  // bit per channel playing a sequence with no simulation clip
	int32 MissingMeshClips; // bit per channel playing a sequence the drawn character file lacks
	int32 MissingName;      // the lowest such channel's sequence as a name id
	float X, Y, Z;
	float VelocityX, VelocityY, VelocityZ;
	float CollisionRadius, CollisionHeight;
};
static_assert(sizeof(FScctInspectPawn) == 68, "FScctInspectPawn must match BridgeInspectPawn");

struct FScctInspectApi
{
	int32 Version;

	/** Writes the session's counters. Returns its id, 0 when none runs. */
	int32 (__cdecl* GetCounters)(FScctInspectCounters* Counters);

	/** Writes every living client pawn. Returns the count written. */
	int32 (__cdecl* GetPawns)(FScctInspectPawn* Pawns, int32 Capacity);

	/** Copies the text of a name id. Returns its length. */
	int32 (__cdecl* GetName)(int32 Id, TCHAR* Buffer, int32 Capacity);

	/**
	 * Sweeps a box of half-size Extent (3 floats, or null for a line) from Start to End (3 floats each) through the
	 * client world's level geometry. 1 with the hit location and its normal's Z in Hit (4 floats), 0 for no hit, -1
	 * when there is no world to trace.
	 */
	int32 (__cdecl* Trace)(const float* Start, const float* End, const float* Extent, float* Hit);

	/** 1 when a cube of half-size Extent at At (3 floats) is inside level geometry, 0 when clear, -1 with no world. */
	int32 (__cdecl* PointInSolid)(const float* At, float Extent);

	/** Copies the last failure's text. Returns its length. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};
static_assert(sizeof(FScctInspectApi) == 56, "FScctInspectApi must match InspectApi");
