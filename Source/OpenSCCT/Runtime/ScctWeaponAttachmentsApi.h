#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

namespace EScctWeaponAttachment
{
    constexpr int32 Laser = 1, RedDot = 2, Suppressor = 4, ExtendedMagazine = 8, Acog = 16;
    constexpr int32 Optics = RedDot | Acog;
}

struct FScctWeaponAttachmentState
{
    int32 Kills, Budget, Equipped, Available, Supported, BaseMagazine;
    float NativeOpticPower;
    int32 Costs[5], KillsRequired[5];
};

struct FScctWeaponAttachmentsApi
{
    int32 Version;
    int32 (__cdecl* GetState)(const TCHAR* WeaponClass, FScctWeaponAttachmentState* State);
    int32 (__cdecl* SetEquipped)(const TCHAR* WeaponClass, int32 Mask);
    int32 (__cdecl* GetLocalEquipped)();
    int32 (__cdecl* GetEquippedForActor)(int32 ActorId);
    int32 (__cdecl* GetHolsteredEquippedForActor)(int32 ActorId);
};

static_assert(sizeof(FScctWeaponAttachmentState) == 68);
static_assert(sizeof(FScctWeaponAttachmentsApi) == 48);

template <> struct TScctFeatureInfo<FScctWeaponAttachmentsApi>
{
    static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.WeaponAttachmentsExports"); }
    static constexpr int32 Version = 3;
};
