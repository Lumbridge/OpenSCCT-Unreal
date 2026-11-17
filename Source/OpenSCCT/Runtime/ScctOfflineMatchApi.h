#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The offline match, from the C# bridge (Bridge/OpenScct.UnrealBridge/MatchMenu/OfflineMatchExports.cs): the game
// options the local match that plays was started with, as the simulation's rules read them (PlaySettings.GameOptions:
// the mode's match page, ?SvmSidearms, ?SvmRifles, ?SvmResponsiveSmoke, ?SvmMatchPlayers and, for practice, the
// ?MaxPlayers=24 of a Visit a Map). The session subsystem logs them after a start, so what the Offline page chose
// can be checked against what reached the rules.
//
// This mirrors OfflineMatchExports.cs; change both together and bump the version.

constexpr int32 ScctOfflineMatchApiVersion = 1;

struct FScctOfflineMatchApi
{
	int32 Version;

	/** Writes the options, cut to fit and zero terminated. Returns their full length, 0 when no local match plays. */
	int32 (__cdecl* GetGameOptions)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctOfflineMatchApi) == 16, "FScctOfflineMatchApi must match OfflineMatchApi (64-bit)");

template <> struct TScctFeatureInfo<FScctOfflineMatchApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.OfflineMatchExports"); }
	static constexpr int32 Version = ScctOfflineMatchApiVersion;
};
