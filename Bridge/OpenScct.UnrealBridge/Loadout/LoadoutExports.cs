using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Loadout;

namespace OpenScct.UnrealBridge;

/// <summary>The saved gear as it crosses to Unreal: class paths, UTF-16, zero terminated, cut to fit.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeLoadout
{
    public const int PathCapacity = 64;
    public const int Slots = LoadoutChoice.Slots;

    public fixed char SpyGadgets[Slots * PathCapacity];
    public fixed char MercGadgets[Slots * PathCapacity];
    public fixed char MercWeapon[PathCapacity];
    public fixed char MercSidearm[PathCapacity];
}

/// <summary>The rules a gear list was made under (<see cref="GearRules"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeGearRules
{
    public int Spy;
    public int Slots;
    public int CurrentLevel;
    public int Unrestricted;
    public int SidearmsEnabled;
    public int RiflesEnabled;

    /// <summary>1 when the player's own team in the running round decided, 0 for the team's stock rules.</summary>
    public int FromTeam;
    public int Count;
}

/// <summary>One piece of gear of a list (<see cref="GearItem"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeGearItem
{
    public const int NameCapacity = 48;

    public fixed char Path[BridgeLoadout.PathCapacity];
    public fixed char Name[NameCapacity];
    public int Kind;
    public int RequiredLevel;
    public int Flags;
}

/// <summary>One answer of the server to a gear change.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeGearAnswer
{
    public const int TextCapacity = 96;

    /// <summary>0 now, 1 at the next spawn, 2 too soon (wait WaitSeconds).</summary>
    public int Result;
    public float WaitSeconds;
    public fixed char Text[TextCapacity];
}

/// <summary>The profile choices the simulation reads. Pushed: -1 leaves a "follow" value following.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeProfileOptions
{
    public int ObjectiveSounds;

    /// <summary>0 the wheel changes gadgets, 1 it walks and runs; pushed -1 follows the controls' mouse_wheel.</summary>
    public int SelectedMouse;
    public int DisplayHelpInGame;

    /// <summary>Pushed -1 follows whether a local match runs.</summary>
    public int OfflineSession;
}

/// <summary>
/// The saved gear, the gear change and the profile choices the simulation reads (Bridge/OpenScct.UnrealBridge/Loadout).
/// Unreal asks for <see cref="LoadoutExports.GetApi"/> once (type
/// <c>OpenScct.UnrealBridge.LoadoutExports, OpenScct.UnrealBridge</c>) and keeps the table;
/// Source/OpenSCCT/Runtime/ScctLoadoutApi.h mirrors it field for field. The level services themselves are installed
/// when the bridge loads (<see cref="LoadoutServices.Hook"/>), whether or not this table is fetched.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LoadoutApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int> Load;
    public delegate* unmanaged[Cdecl]<char*, int> Save;
    public delegate* unmanaged[Cdecl]<BridgeLoadout*, void> GetLoadout;
    public delegate* unmanaged[Cdecl]<BridgeLoadout*, int> SetLoadout;
    public delegate* unmanaged[Cdecl]<BridgeLoadout*, void> GetDefaults;
    public delegate* unmanaged[Cdecl]<int, BridgeGearRules*, BridgeGearItem*, int, int> ListGear;
    public delegate* unmanaged[Cdecl]<int> RequestGearChange;
    public delegate* unmanaged[Cdecl]<BridgeGearAnswer*, int> PollGearAnswer;
    public delegate* unmanaged[Cdecl]<BridgeProfileOptions*, void> SetProfileOptions;
    public delegate* unmanaged[Cdecl]<BridgeProfileOptions*, void> GetProfileOptions;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetLocalInventory;
    public delegate* unmanaged[Cdecl]<int*, int*, void> GetKitRequests;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

public static unsafe class LoadoutExports
{
    public const int ApiVersion = 1;

    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(LoadoutApi* api)
    {
        LoadoutServices.Hook();
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Load = &Load;
        api->Save = &Save;
        api->GetLoadout = &GetLoadout;
        api->SetLoadout = &SetLoadout;
        api->GetDefaults = &GetDefaults;
        api->ListGear = &ListGear;
        api->RequestGearChange = &RequestGearChange;
        api->PollGearAnswer = &PollGearAnswer;
        api->SetProfileOptions = &SetProfileOptions;
        api->GetProfileOptions = &GetProfileOptions;
        api->GetLocalInventory = &GetLocalInventory;
        api->GetKitRequests = &GetKitRequests;
        api->LastError = &LastError;
        return ApiVersion;
    }

    // ---- managed helpers (the probe calls these too) ---------------------------------------------------------------

    public static BridgeLoadout ToBridge(LoadoutChoice choice)
    {
        // A choice made in code may have short or missing slots: crossed as the store would hold it.
        choice = choice.Clone();
        choice.Clamp();
        BridgeLoadout b = default;
        for (int i = 0; i < BridgeLoadout.Slots; i++)
        {
            Put(choice.SpyGadgets[i], b.SpyGadgets + (i * BridgeLoadout.PathCapacity), BridgeLoadout.PathCapacity);
            Put(choice.MercGadgets[i], b.MercGadgets + (i * BridgeLoadout.PathCapacity), BridgeLoadout.PathCapacity);
        }

        Put(choice.MercWeapon, b.MercWeapon, BridgeLoadout.PathCapacity);
        Put(choice.MercSidearm, b.MercSidearm, BridgeLoadout.PathCapacity);
        return b;
    }

    public static LoadoutChoice FromBridge(in BridgeLoadout b)
    {
        var choice = new LoadoutChoice { SpyGadgets = new string[BridgeLoadout.Slots], MercGadgets = new string[BridgeLoadout.Slots] };
        fixed (BridgeLoadout* p = &b)
        {
            for (int i = 0; i < BridgeLoadout.Slots; i++)
            {
                choice.SpyGadgets[i] = Take(p->SpyGadgets + (i * BridgeLoadout.PathCapacity), BridgeLoadout.PathCapacity);
                choice.MercGadgets[i] = Take(p->MercGadgets + (i * BridgeLoadout.PathCapacity), BridgeLoadout.PathCapacity);
            }

            choice.MercWeapon = Take(p->MercWeapon, BridgeLoadout.PathCapacity);
            choice.MercSidearm = Take(p->MercSidearm, BridgeLoadout.PathCapacity);
        }

        return choice;
    }

    /// <summary>
    /// Asks the server to put the saved kit on the local player now (SPlayerController.ServerChangeGear); its answer
    /// comes back through the gear feedback. False when there is no local player to ask for.
    /// </summary>
    public static bool AskGearChange(out string error)
    {
        if (Exports.Current?.Session.LocalController is not { } pc)
        {
            error = "No match is playing: there is no player to change the gear of.";
            return false;
        }

        pc.ServerChangeGear();
        error = "";
        return true;
    }

    // ---- entry points --------------------------------------------------------------------------------------------

    /// <summary>Names the loadout file and reads it (defaults when it does not exist). Returns 1 read, 2 no file yet, 0 failed.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Load(char* path) => Guard(() => LoadoutStore.Load(Text(path)) ? 1 : 2);

    /// <summary>Writes the choice in use (path null or empty: the file loaded). Returns 1, or 0 with the reason.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Save(char* path) => Guard(() =>
    {
        LoadoutStore.Save(Text(path));
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetLoadout(BridgeLoadout* loadout)
    {
        if (loadout != null)
        {
            Guard(() =>
            {
                *loadout = ToBridge(LoadoutStore.Current);
                return 1;
            });
        }
    }

    /// <summary>Puts a choice in use, cleaned as a read file is (a spawn reads it). Returns 1, or 0 with the reason.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetLoadout(BridgeLoadout* loadout) => Guard(() =>
    {
        if (loadout == null)
        {
            s_lastError = "No loadout was handed over.";
            return 0;
        }

        LoadoutChoice choice = FromBridge(in *loadout);
        choice.AttachmentMasks = LoadoutStore.Current.AttachmentMasks;
        LoadoutStore.Set(choice);
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetDefaults(BridgeLoadout* loadout)
    {
        if (loadout != null)
        {
            Guard(() =>
            {
                *loadout = ToBridge(LoadoutStore.Fresh());
                return 1;
            });
        }
    }

    /// <summary>
    /// The gear a side can choose from (side 0 spy, 1 merc, -1 the local player's), with the rules it was made under.
    /// Returns the full count; items past <paramref name="capacity"/> are not written.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ListGear(int side, BridgeGearRules* rules, BridgeGearItem* items, int capacity) => Guard(() =>
    {
        List<GearItem> list = LoadoutGear.List(side, out GearRules r);
        if (rules != null)
        {
            *rules = new BridgeGearRules
            {
                Spy = r.Spy ? 1 : 0,
                Slots = r.Slots,
                CurrentLevel = r.CurrentLevel,
                Unrestricted = r.Unrestricted ? 1 : 0,
                SidearmsEnabled = r.SidearmsEnabled ? 1 : 0,
                RiflesEnabled = r.RiflesEnabled ? 1 : 0,
                FromTeam = r.FromTeam ? 1 : 0,
                Count = list.Count,
            };
        }

        for (int i = 0; items != null && i < list.Count && i < capacity; i++)
        {
            BridgeGearItem item = default;
            Put(list[i].Path, item.Path, BridgeLoadout.PathCapacity);
            Put(list[i].Name, item.Name, BridgeGearItem.NameCapacity);
            item.Kind = (int)list[i].Kind;
            item.RequiredLevel = list[i].RequiredLevel;
            item.Flags = (int)list[i].Flags;
            items[i] = item;
        }

        return list.Count;
    });

    /// <summary>Asks the server to put the saved kit on now. Returns 1 asked, or 0 (no match) with the reason.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RequestGearChange() => Guard(() =>
    {
        if (AskGearChange(out string error))
        {
            return 1;
        }

        s_lastError = error;
        return 0;
    });

    /// <summary>Takes the oldest answer of the server not read yet. Returns 1 with it, 0 when there is none.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PollGearAnswer(BridgeGearAnswer* answer) => Guard(() =>
    {
        if (answer == null || !QueuedGearFeedback.TryTake(out GearAnswer taken))
        {
            return 0;
        }

        BridgeGearAnswer b = default;
        b.Result = (int)taken.Result;
        b.WaitSeconds = taken.WaitSeconds;
        Put(taken.Text, b.Text, BridgeGearAnswer.TextCapacity);
        *answer = b;
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetProfileOptions(BridgeProfileOptions* options)
    {
        if (options == null)
        {
            return;
        }

        ProfileOptionValues.ObjectiveSounds = options->ObjectiveSounds != 0;
        ProfileOptionValues.DisplayHelpInGame = options->DisplayHelpInGame != 0;
        ProfileOptionValues.SelectedMousePushed = options->SelectedMouse;
        ProfileOptionValues.OfflineSessionPushed = options->OfflineSession;
    }

    /// <summary>The values the simulation reads now ("follow" values worked out).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetProfileOptions(BridgeProfileOptions* options)
    {
        if (options == null)
        {
            return;
        }

        Guard(() =>
        {
            *options = new BridgeProfileOptions
            {
                ObjectiveSounds = ProfileOptionValues.ObjectiveSounds ? 1 : 0,
                SelectedMouse = ProfileOptionValues.SelectedMouse,
                DisplayHelpInGame = ProfileOptionValues.DisplayHelpInGame ? 1 : 0,
                OfflineSession = ProfileOptionValues.OfflineSession ? 1 : 0,
            };
            return 1;
        });
    }

    /// <summary>The kit the local pawn carries ("spy SGadA, SGadB | SStickyShocker"). Returns the full length, 0 without a pawn.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLocalInventory(char* buffer, int capacity) => Guard(() => Copy(LoadoutGear.LocalInventory(), buffer, capacity));

    /// <summary>How many times a spawn on this machine read the saved kit, spy and merc (they only grow).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetKitRequests(int* spy, int* merc)
    {
        if (spy != null)
        {
            *spy = LoadoutServed.Spy;
        }

        if (merc != null)
        {
            *merc = LoadoutServed.Merc;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        try
        {
            return Copy(s_lastError, buffer, capacity);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    private static string Text(char* text) => text == null ? "" : new string(text);

    private static int Guard(Func<int> body)
    {
        try
        {
            return body();
        }
        catch (Exception e)
        {
            // Nothing may cross into native code; a fault here never ends a match.
            s_lastError = e.ToString();
            return 0;
        }
    }

    private static void Put(string? text, char* target, int capacity)
    {
        text ??= "";
        int count = Math.Min(text.Length, capacity - 1);
        text.AsSpan(0, count).CopyTo(new Span<char>(target, count));
        new Span<char>(target + count, capacity - count).Clear();
    }

    private static string Take(char* source, int capacity)
    {
        int length = 0;
        while (length < capacity && source[length] != '\0')
        {
            length++;
        }

        return new string(source, 0, length);
    }

    /// <summary>Copies <paramref name="text"/> with its terminator, as much as fits. Returns the full length.</summary>
    private static int Copy(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}
