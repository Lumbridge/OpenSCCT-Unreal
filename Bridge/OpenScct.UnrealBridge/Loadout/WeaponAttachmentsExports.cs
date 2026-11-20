using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using OpenScct.Game.Animation;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Loadout;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeWeaponAttachmentState
{
    public int Kills, Budget, Equipped, Available, Supported, BaseMagazine;
    public float NativeOpticPower;
    public const int Count = 5;
    public fixed int Costs[Count], KillsRequired[Count];
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WeaponAttachmentsApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, BridgeWeaponAttachmentState*, int> GetState;
    public delegate* unmanaged[Cdecl]<char*, int, int> SetEquipped;
    public delegate* unmanaged[Cdecl]<int> GetLocalEquipped;
    public delegate* unmanaged[Cdecl]<int, int> GetEquippedForActor;
    public delegate* unmanaged[Cdecl]<int, int> GetHolsteredEquippedForActor;
}

public static unsafe class WeaponAttachmentsExports
{
    [UnmanagedCallersOnly]
    public static int GetApi(WeaponAttachmentsApi* api)
    {
        if (api != null)
        {
            api->Version = 3;
            api->GetState = &GetState;
            api->SetEquipped = &SetEquipped;
            api->GetLocalEquipped = &GetLocalEquipped;
            api->GetEquippedForActor = &GetEquippedForActor;
            api->GetHolsteredEquippedForActor = &GetHolsteredEquippedForActor;
        }
        return 3;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(char* weaponClass, BridgeWeaponAttachmentState* state)
    {
        try
        {
            if (state == null) return 0;
            *state = default;
            if (weaponClass == null || WeaponAttachments.Key(new string(weaponClass)) is not { } key) return 0;
            int unlockKills = LoadoutServices.MenuAttachmentUnlockKills(key);
            state->Kills = WeaponCareerView.GetKills(key);
            state->Budget = WeaponAttachments.Budget;
            state->Supported = WeaponAttachments.Supported(key);
            state->BaseMagazine = WeaponAttachments.MagazineCapacity(key, 0);
            state->NativeOpticPower = MercRifles.Find(key)?.NativeOpticPower ?? 1f;
            state->Available = WeaponAttachments.Available(key, unlockKills);
            int saved = LoadoutStore.Current.AttachmentMasks.GetValueOrDefault(key);
            state->Equipped = WeaponAttachments.EarnedSelection(key, saved, unlockKills);
            for (int i = 0; i < BridgeWeaponAttachmentState.Count; i++)
            {
                state->Costs[i] = WeaponAttachments.Points(1 << i);
                state->KillsRequired[i] = WeaponAttachments.KillsRequired(1 << i);
            }
            return 1;
        }
        catch (Exception ex) { Exports.ReportFailure(ex.ToString()); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetEquipped(char* weaponClass, int mask)
    {
        try
        {
            if (weaponClass == null || WeaponAttachments.Key(new string(weaponClass)) is not { } key ||
                !WeaponAttachments.IsValid(key, mask, LoadoutServices.MenuAttachmentUnlockKills(key))) return 0;
            var choice = LoadoutStore.Current.Clone();
            if (mask == 0) choice.AttachmentMasks.Remove(key);
            else choice.AttachmentMasks[key] = mask;
            LoadoutStore.SaveChoice(choice);
            return 1;
        }
        catch (Exception ex) { Exports.ReportFailure(ex.ToString()); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLocalEquipped()
    {
        try { return (Exports.Current?.Session.LocalController?.SBPawn?.MyWeapon as SWeaponDefense)?.AttachmentMask ?? 0; }
        catch (Exception ex) { Exports.ReportFailure(ex.ToString()); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetEquippedForActor(int actorId) => ActorMask(actorId, holstered: false);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetHolsteredEquippedForActor(int actorId) => ActorMask(actorId, holstered: true);

    private static int ActorMask(int actorId, bool holstered)
    {
        try
        {
            if (Exports.Current is not { IsDisposed: false } session ||
                session.Session.Client.World.Level.FindActor(actorId) is not SPAWNDEFENSE { bDeleteMe: false } pawn) return 0;
            bool primary = NetworkPawnAttachments.For(pawn).ActiveSlot == 0;
            return primary != holstered ? pawn.PrimaryAttachmentMask : pawn.SidearmWeapon?.AttachmentMask ?? 0;
        }
        catch (Exception ex) { Exports.ReportFailure(ex.ToString()); return 0; }
    }
}
