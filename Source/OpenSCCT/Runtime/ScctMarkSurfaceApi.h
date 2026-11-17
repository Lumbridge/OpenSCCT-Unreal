#pragma once

#include "CoreMinimal.h"

// Which map actor an impact mark landed on and whether it is drawn now, so the effects can keep a mark on a door
// that opens and drop it with a pane that breaks. The events hand a mark's surface as a simulation actor id
// (FScctImpactMark::SurfaceActorId, 0 for the level's own geometry); this turns it into the world table's name id
// (Runtime/ScctWorldApi.h, the level state's GetName) and the surface's state. Mirrors
// Bridge/OpenScct.UnrealBridge/Events/MarkSurfaceExports.cs; change both together.

constexpr int32 ScctMarkSurfaceApiVersion = 1;

namespace EScctMarkSurfaceFlags
{
	/** The surface is an actor of the client's level that has not been destroyed. */
	constexpr int32 Exists = 1;
	/** The surface is drawn now (not hidden, not drawn as nothing): a mark on it is drawn too. */
	constexpr int32 Drawn = 2;
}

struct FScctMarkSurface
{
	/** The world table's name id of the followed map actor that stands for the surface; 0 when it is not followed. */
	int32 NameId;
	int32 Flags;
	/** Where the surface is in the simulation now, and its turn in degrees. */
	float X, Y, Z, Pitch, Yaw, Roll;
};

static_assert(sizeof(FScctMarkSurface) == 32, "FScctMarkSurface must match BridgeMarkSurface");

struct FScctMarkSurfaceApi
{
	int32 Version;

	/** Describes the surface with this simulation actor id; returns its flags (0: no session, id 0, or gone). */
	int32 (__cdecl* GetSurface)(int32 ActorId, FScctMarkSurface* OutSurface);
};
