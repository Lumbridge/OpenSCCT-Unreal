#pragma once

#include "CoreMinimal.h"

// Mirrors Equipment/LaserTraceExports.cs. Both points and the direction are in the game's world space.
struct FScctMountedLaserRay
{
	float HolderX, HolderY, HolderZ;
	float OriginX, OriginY, OriginZ;
	float DirX, DirY, DirZ;
};

static_assert(sizeof(FScctMountedLaserRay) == 36);

constexpr int32 ScctLaserTraceApiVersion = 1;

struct FScctLaserTraceApi
{
	int32 Version;

	/** Negative if the emitter is obstructed or unavailable, 0 if clear to maximum range, 1 on a surface. */
	int32 (__cdecl* Trace)(int32 HolderId, const FScctMountedLaserRay* Ray, float* OutDistance);
};
