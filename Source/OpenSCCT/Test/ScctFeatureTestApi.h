#pragma once

#include "CoreMinimal.h"

// The bridge's entry points for the per-feature checks (Test/Features) that no other table has: where the next local
// start puts the local player (the traversal check's TestRange stations). Mirrors
// Bridge/OpenScct.UnrealBridge/Test/FeatureTestExports.cs field for field; change both together and bump the version.
//
// Fetch the table through ScctFeature<FScctFeatureTestApi>() (Runtime/ScctFeatures.h). Call it on the game thread.

constexpr int32 ScctFeatureTestApiVersion = 1;

struct FScctFeatureTestApi
{
	int32 Version;

	/**
	 * Arms the next local start: the local player first spawns at (X, Y, Z) (the pawn's centre, uu) facing Yaw (65536 a
	 * turn), with the editor's Play Level rules. The next start uses it up. Returns 1.
	 */
	int32 (__cdecl* SetNextStart)(float X, float Y, float Z, int32 Yaw);

	/** Drops an armed start that was not used. Returns 1. */
	int32 (__cdecl* ClearNextStart)();

	/** Copies the last failure's text. Returns its length. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};
static_assert(sizeof(FScctFeatureTestApi) == 32, "FScctFeatureTestApi must match FeatureTestApi");
