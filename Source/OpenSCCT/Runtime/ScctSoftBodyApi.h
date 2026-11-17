#pragma once

#include "CoreMinimal.h"

// The map's soft bodies: the original's cloth patches (ESBPatchActor: flags, awnings, banners) and strip curtains
// (ESBStripDoorActor), which every machine simulates for itself. The bridge builds one simulation per instance of the
// map's render data, steps it once a frame against the pawns of the client's world, pushes it with the shots that
// cross it and plays its collision and shot sounds through the events feature's sounds; Unreal draws it. Mirrors
// Bridge/OpenScct.UnrealBridge/SoftBodies/SoftBodyExports.cs; change both together.

constexpr int32 ScctSoftBodyApiVersion = 1;

struct FScctSoftBodyState
{
	/** Bodies built from the map's render data. */
	int32 Count;
	/** Bodies the last update that stepped stepped (the ones drawn in the last 5 s). */
	int32 Stepped;
	/** Pawn colliders the stepped bodies were handed in that update. */
	int32 Colliders;
	/** collSound and shotSound calls made since the session started. */
	int32 CollSounds;
	int32 ShotSounds;
	/** Shot lines looked at, and how many times one crossed a body. */
	int32 ShotLines;
	int32 ShotHits;
	/** Soft body instances that could not be built (they stay plain meshes). */
	int32 Warnings;
	/** Fixed steps run so far, all bodies together. */
	int64 Steps;
	/** The farthest any particle of any body is from its rest place now, cm. */
	float MaxDisplacement;
	/** Milliseconds the last update took, and the loading of the bodies. */
	float UpdateMilliseconds;
	float LoadMilliseconds;
	int32 Reserved;
};

static_assert(sizeof(FScctSoftBodyState) == 56, "FScctSoftBodyState must match BridgeSoftBodyState");

namespace EScctSoftBodyFlags
{
	/** Pawns push it (collActor). */
	constexpr int32 CollActor = 1;
	constexpr int32 CollSound = 2;
	constexpr int32 ShotSound = 4;
}

struct FScctSoftBodyInfo
{
	/** Particles (N). The mesh has 2N vertices: vertex i and i + N (the back face) sit on particle i. */
	int32 Particles;
	int32 Vertices;
	/** Triangle indices (3 a triangle). */
	int32 Indices;
	/** 0 a cloth patch, 1 a strip curtain. */
	int32 Kind;
	/** EScctSoftBodyFlags. */
	int32 Flags;
	/** The middle of its rest bounds in the world, and the radius around it, cm. */
	float CenterX, CenterY, CenterZ, Radius;
	/** The farthest a particle is from its rest place now, cm. */
	float Displacement;
	/** +1 or -1: how the normals were turned to agree with the library mesh's. */
	float NormalSign;
};

static_assert(sizeof(FScctSoftBodyInfo) == 44, "FScctSoftBodyInfo must match BridgeSoftBodyInfo");

struct FScctSoftBodyApi
{
	int32 Version;

	/** Fills the state. Returns 1, or 0 without a session. */
	int32 (__cdecl* GetState)(FScctSoftBodyState* OutState);
	/** Fills one body's description. Returns 1, or 0 for an index out of range. */
	int32 (__cdecl* GetInfo)(int32 Index, FScctSoftBodyInfo* OutInfo);
	/** Writes the body's map actor name (UTF-16, no terminator): the level actor is tagged "Scct:" + it. Returns its length. */
	int32 (__cdecl* GetName)(int32 Index, TCHAR* OutName, int32 Capacity);
	/** Writes the triangle indices (Info.Indices) and texture coordinates (2 floats a vertex). Returns 1 when both fit. */
	int32 (__cdecl* GetMesh)(int32 Index, int32* OutIndices, int32 IndexCapacity, float* OutUVs, int32 UVCapacity);
	/**
	 * Once a frame: the frame's seconds and, a byte a body (Count of them; null for all), whether it was drawn this
	 * frame. Takes the frame's shots and steps the bodies drawn in the last 5 s. Returns the bodies stepped.
	 */
	int32 (__cdecl* Update)(float DeltaSeconds, const uint8* Seen, int32 Count);
	/** Writes the particles in the actor's local space, six floats each (position, normal). Returns the count, 0 when short. */
	int32 (__cdecl* GetVertices)(int32 Index, float* OutVertices, int32 Capacity);
	/** Unattended checks: a pawn-sized cylinder walks through the body over the seconds given. */
	int32 (__cdecl* Walk)(int32 Index, float Seconds);
	/** Unattended checks: a shot line through the body's middle. */
	int32 (__cdecl* Shoot)(int32 Index);
};
