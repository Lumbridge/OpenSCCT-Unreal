#pragma once

#include "CoreMinimal.h"

// The glass check of -ScctLevelDo "breakglass": breaks a pane on the local match's server with a hit at a point and
// direction Unreal picks on the pane it draws, so the pane records that impact and every machine shapes the same
// shards around it. Mirrors Bridge/OpenScct.UnrealBridge/Devices/GlassExports.cs; change both together.

constexpr int32 ScctGlassApiVersion = 1;

struct FScctGlassApi
{
	int32 Version;

	/**
	 * Breaks the unbroken pane the map placed at (PaneX, PaneY, PaneZ) (its Location, within 2 cm) with one heavy hit
	 * at (HitX, HitY, HitZ) going along (DirX, DirY, DirZ), on the server's next world step. 1 when queued, 0 without
	 * a local server.
	 */
	int32 (__cdecl* BreakPane)(float PaneX, float PaneY, float PaneZ, float HitX, float HitY, float HitZ, float DirX, float DirY, float DirZ);

	/**
	 * Stands the local player's pawn on the server 120 cm beside the pane the map placed nearest (X, Y, Z): a pane's
	 * state reaches a client only while the pane is relevant to its player. 1 when queued, 0 without a local server.
	 */
	int32 (__cdecl* StandBeside)(float X, float Y, float Z);
};
