#pragma once

#include "Runtime/ScctFxTraceApi.h"

constexpr int32 ScctShadowTraceApiVersion = 1;

struct FScctShadowTraceApi
{
	int32 Version;

	/** Nearest visible level surface or mover on the segment. 1 hit, 0 clear, -1 collision unavailable. */
	int32 (__cdecl* Trace)(float StartX, float StartY, float StartZ, float EndX, float EndY, float EndZ, FScctFxHit* OutHit);
};
