#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

struct FScctPawnCarriedLookApi
{
    int32 Version;
    int32 (__cdecl* GetPattern)(int32 ActorId);
};

static_assert(sizeof(FScctPawnCarriedLookApi) == 16);

template <> struct TScctFeatureInfo<FScctPawnCarriedLookApi>
{
    static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.PawnCarriedLookExports"); }
    static constexpr int32 Version = 1;
};
