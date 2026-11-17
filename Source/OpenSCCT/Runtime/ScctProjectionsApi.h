#pragma once

#include "CoreMinimal.h"

// The abandoned projections of the client's world (Projector.AbandonProjector): the footprints of dirty feet, the
// burn mark a flare leaves where it caught and the shotgun's pellet projectors. Each is placed by the bridge on the
// surface it covers when it is first seen and drawn until its time is up. Mirrors
// Bridge/OpenScct.UnrealBridge/Events/ProjectionExports.cs; change both together. Names are the events feature's
// name ids (FScctEventsApi::GetName).

constexpr int32 ScctProjectionsApiVersion = 1;

namespace EScctProjectionFlags
{
	/** Drawn in the tint's colour with the texture's alpha times the tint's (a footprint's material). */
	constexpr int32 Tinted = 1;
	/** The texture is upside down (a right foot). */
	constexpr int32 FlipV = 2;
}

struct FScctProjection
{
	/** Stays the same for one projection until it goes. */
	int32 Serial;
	/** Name id of the texture's object path (sfx.FootPrints.step01). */
	int32 TextureId;
	/** Name id of the imported texture asset's name (sfx_step01); 0 when the content does not list the texture. */
	int32 AssetId;
	/** Name id of the projector's class (SBase.SFootPrint). */
	int32 ClassId;
	/** Simulation id of the actor it lies on (a door, a lift), 0 for the level's own geometry (FScctMarkSurfaceApi). */
	int32 SurfaceActorId;
	int32 Flags;
	/** Where it lies, the surface's normal there (out of the surface) and the way the texture's U runs on it. */
	float X, Y, Z;
	float NormalX, NormalY, NormalZ;
	float UX, UY, UZ;
	/** Its size on the surface, cm. */
	float Width, Height;
	/** The colour and opacity of a tinted projection (sRGB, 0..1); white otherwise. */
	float TintR, TintG, TintB, TintA;
	/** Seconds it is drawn in all and seconds left; how much of it is drawn now (it fades out at the end), 0..1. */
	float Life, Remaining, Opacity;
};

static_assert(sizeof(FScctProjection) == 96, "FScctProjection must match BridgeProjection");

struct FScctProjectionsApi
{
	int32 Version;

	/** Writes the projections drawn now, oldest first. Returns the count written. */
	int32 (__cdecl* GetProjections)(FScctProjection* OutProjections, int32 Capacity);
	/** Projections placed since the session started, and ones whose line met no surface. Returns 0 without a session. */
	int32 (__cdecl* GetCounts)(int32* OutPlaced, int32* OutMissed);
};
