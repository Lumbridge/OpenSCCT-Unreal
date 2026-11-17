#pragma once

#include "CoreMinimal.h"

// The particle collision trace for the effects: the imported level carries no Unreal collision (the simulation owns
// it), so a casing that bounces asks the client world's collision where its line meets the level and world geometry,
// as the Godot client's particles do. Mirrors Bridge/OpenScct.UnrealBridge/Events/FxTraceExports.cs; change both
// together.

constexpr int32 ScctFxTraceApiVersion = 1;

struct FScctFxHit
{
	/** The point hit and the surface's normal there. */
	float X, Y, Z, NormalX, NormalY, NormalZ;
	/** Simulation id of the actor hit, 0 for the level's own geometry. */
	int32 ActorId;
	/** How far along the line the hit is, 0 at its start and 1 at its end. */
	float Time;
};

static_assert(sizeof(FScctFxHit) == 32, "FScctFxHit must match BridgeFxHit");

struct FScctFxTraceApi
{
	int32 Version;

	/** The first surface on the line from start to end; 1 when it meets one, 0 when clear or there is no session. */
	int32 (__cdecl* Trace)(float StartX, float StartY, float StartZ, float EndX, float EndY, float EndZ, FScctFxHit* OutHit);
};
