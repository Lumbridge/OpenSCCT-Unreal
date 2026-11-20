using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Loadout;

namespace OpenScct.UnrealBridge;

/// <summary>The picker as Unreal draws it (<see cref="GearEditor"/>): the side shown and its draft kit.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeGearMenuState
{
    public const int PathCapacity = BridgeLoadout.PathCapacity;
    public const int NameCapacity = BridgeGearItem.NameCapacity;
    public const int RuleCapacity = 256;
    public const int Slots = LoadoutChoice.Slots;

    /// <summary>0 spy, 1 merc.</summary>
    public int Side;
    public int GadgetSlots;
    public int CurrentLevel;
    public int Unrestricted;
    public int SidearmsEnabled;
    public int RiflesEnabled;

    /// <summary>1: the player's own team in the running round decided the rules.</summary>
    public int FromTeam;

    /// <summary>1: a draft of either side differs from the saved kit.</summary>
    public int Changed;

    /// <summary>1: the side shown is the one the local player plays in the running round (APPLY asks the server).</summary>
    public int OwnSide;

    /// <summary>What the picker was opened for: 0 the menus, 1 the lobby, 2 a match.</summary>
    public int Context;

    /// <summary>Per slot: 1 when it holds a gadget the spy throws with a quick-use key (slots 1 and 2 only).</summary>
    public fixed int QuickUse[Slots];

    public fixed char Gadgets[Slots * PathCapacity];
    public fixed char Weapon[PathCapacity];
    public fixed char Sidearm[PathCapacity];
    public fixed char GadgetNames[Slots * NameCapacity];
    public fixed char WeaponName[NameCapacity];
    public fixed char SidearmName[NameCapacity];
    public fixed char Rule[RuleCapacity];
}

/// <summary>One card of the picker's catalogue.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeGearOption
{
    public const int SubCapacity = 48;

    public fixed char Path[BridgeLoadout.PathCapacity];
    public fixed char Name[BridgeGearItem.NameCapacity];
    public fixed char Sub[SubCapacity];
    public int Flags;
    public int RequiredLevel;
}

/// <summary>The picker's intel panel for one piece of gear.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeGearIntel
{
    public const int NameCapacity = BridgeGearItem.NameCapacity;
    public const int DescriptionCapacity = 512;
    public const int RestrictionCapacity = 96;
    public const int NoteCapacity = 160;
    public const int FileCapacity = 260;
    public const int MaxStats = 10;
    public const int StatCapacity = 32;

    public fixed char Name[NameCapacity];
    public fixed char Role[NameCapacity];
    public fixed char Description[DescriptionCapacity];
    public fixed char Restriction[RestrictionCapacity];
    public fixed char Note[NoteCapacity];
    public fixed char IconFile[FileCapacity];

    /// <summary>The tile's corner in pixels and its size; IconSize 0 means the whole image.</summary>
    public int IconU;
    public int IconV;
    public int IconSize;
    public int StatCount;
    public fixed char StatCaption[MaxStats * StatCapacity];
    public fixed char StatValue[MaxStats * StatCapacity];
}

public static class GearOptionFlags
{
    public const int Allowed = 1;
    public const int LevelLocked = 2;
    public const int HostDisabled = 4;
    public const int Equipped = 8;
    public const int Selected = 16;
    public const int QuickUse = 32;
    public const int Suppressed = 64;
}

/// <summary>
/// The gear picker's table (Source/OpenSCCT/Runtime/ScctGearMenuApi.h mirrors it field for field): open the picker
/// for a side and a context, read the draft, list a slot's catalogue, choose, reset, read an item's intel, apply
/// (saves the kit through <see cref="LoadoutStore"/>) and the saved kit as one line for the lobby's summary.
/// Asking the server to put the kit on stays with <see cref="LoadoutExports"/> (RequestGearChange).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct GearMenuApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, int, int> Open;
    public delegate* unmanaged[Cdecl]<int, int> SetSide;
    public delegate* unmanaged[Cdecl]<BridgeGearMenuState*, int> GetState;
    public delegate* unmanaged[Cdecl]<int, int, BridgeGearOption*, int, int> ListOptions;
    public delegate* unmanaged[Cdecl]<int, int, char*, int> Choose;
    public delegate* unmanaged[Cdecl]<void> Reset;
    public delegate* unmanaged[Cdecl]<char*, BridgeGearIntel*, int> GetIntel;
    public delegate* unmanaged[Cdecl]<int> Apply;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> Summary;
    public delegate* unmanaged[Cdecl]<char*, int, int> SavedJson;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
    public delegate* unmanaged[Cdecl]<char*, int> GetAttachmentMask;
    public delegate* unmanaged[Cdecl]<char*, int, int> SetAttachmentMask;
}

/// <summary>
/// The picker's draft: one model per side, built when the picker opens, edited by the page and written into the
/// saved kit by APPLY. Both sides' drafts are kept, so a change on the other tab is not lost by switching back.
/// </summary>
public static class GearEditor
{
    public const int MenuContext = 0, LobbyContext = 1, MatchContext = 2;

    /// <summary>The line on the other side's tab during a round.</summary>
    public const string OtherSideRule = "This is the other side's kit: it goes on when you play that side.";

    private static readonly GearMenuModel?[] s_models = new GearMenuModel?[2];
    private static readonly Dictionary<string, int> s_attachmentEdits = new(StringComparer.OrdinalIgnoreCase);
    // Slate reads the lobby kit every frame. Rebuild the text only when its inputs change.
    private static (bool Spy, LoadoutChoice? Saved, LobbySetup? Setup, int Rank, bool Unrestricted) s_lobbyKit;
    private static string s_lobbyKitText = "";
    private static int s_side;
    private static int s_context;

    public static int Side => s_side;
    public static int Context => s_context;

    /// <summary>The draft of the side shown (opens the menus' picker on the spy when nothing was opened yet).</summary>
    public static GearMenuModel Current
    {
        get
        {
            if (s_models[s_side] is null)
            {
                Open(s_side, s_context);
            }

            return s_models[s_side]!;
        }
    }

    /// <summary>The side the local player plays in the running round: 0 spy, 1 merc, -1 none.</summary>
    public static int PlayedSide()
    {
        SPlayerController? pc = Exports.Current?.Session.LocalController;
        return pc?.PlayerReplicationInfo?.Team switch
        {
            STeamAttaquant => 0,
            STeamDefenseur => 1,
            _ => -1,
        };
    }

    /// <summary>
    /// Builds both sides' drafts from the saved kit for <paramref name="context"/> and shows <paramref name="side"/>
    /// (-1: the side played in the round, else the spy).
    /// </summary>
    public static void Open(int side, int context)
    {
        s_attachmentEdits.Clear();
        s_context = Math.Clamp(context, MenuContext, MatchContext);
        LoadoutChoice saved = LoadoutStore.Current;
        s_models[0] = Build(true, saved);
        s_models[1] = Build(false, saved);
        int played = PlayedSide();
        s_side = side is 0 or 1 ? side : played >= 0 ? played : 0;
    }

    public static void SetSide(int side) => s_side = side == 1 ? 1 : 0;

    public static bool IsOwnSide(int side) => s_context == MatchContext && PlayedSide() == side;

    private static GearMenuModel Build(bool spy, LoadoutChoice saved)
    {
        var visit = Online.OnlineBridge.Existing?.Client;
        if (s_context == MatchContext)
        {
            SPlayerController? pc = Exports.Current?.Session.LocalController;
            bool hub = string.Equals(Exports.Current?.Session.Settings.Map, OpenScct.Server.Hub.HubK.MapName, StringComparison.OrdinalIgnoreCase);
            var lobby = pc is not null && ReferenceEquals(visit?.Session?.World.Level, pc.XLevel) ? visit?.Lobby : null;
            if (PlayedSide() == (spy ? 0 : 1) && GearMenuModel.For(pc, saved, hub, lobby) is { } own)
            {
                return own;
            }

            return GearMenuModel.ForStock(spy, saved, OtherSideRule);
        }

        if (s_context == LobbyContext && visit is not null)
        {
            return GearMenuModel.ForLobby(spy, visit.Lobby, saved);
        }

        return GearMenuModel.ForStock(spy, saved, GearMenuModel.OfflineRule);
    }

    /// <summary>The saved kit with both drafts written in.</summary>
    public static LoadoutChoice Draft()
    {
        LoadoutChoice choice = LoadoutStore.Current.Clone();
        foreach (GearMenuModel? model in s_models)
        {
            model?.SaveTo(choice);
        }

        foreach ((string weapon, int mask) in s_attachmentEdits)
        {
            if (mask == 0) choice.AttachmentMasks.Remove(weapon);
            else choice.AttachmentMasks[weapon] = mask;
        }
        choice.Clamp();
        return choice;
    }

    public static bool Changed() => LoadoutStore.ToJson(Draft()) != LoadoutStore.ToJson(LoadoutStore.Current);

    /// <summary>The page's RESET: the side shown goes back to its default kit (the host's restrictions applied).</summary>
    public static int GetAttachmentMask(string weaponClass)
    {
        if (WeaponAttachments.Key(weaponClass) is not { } key) return 0;
        int mask = s_attachmentEdits.GetValueOrDefault(key, LoadoutStore.Current.AttachmentMasks.GetValueOrDefault(key));
        return WeaponAttachments.EarnedSelection(key, mask, LoadoutServices.MenuAttachmentUnlockKills(key));
    }

    public static bool SetAttachmentMask(string weaponClass, int mask)
    {
        if (Side != 1 || WeaponAttachments.Key(weaponClass) is not { } key ||
            !WeaponAttachments.IsValid(key, mask, LoadoutServices.MenuAttachmentUnlockKills(key))) return false;
        if (LoadoutStore.Current.AttachmentMasks.GetValueOrDefault(key) == mask) s_attachmentEdits.Remove(key);
        else s_attachmentEdits[key] = mask;
        return true;
    }

    public static void Reset()
    {
        Current.Reset();
        if (Side != 1) return;
        s_attachmentEdits.Clear();
        foreach (string key in LoadoutStore.Current.AttachmentMasks.Keys) s_attachmentEdits[key] = 0;
    }

    /// <summary>
    /// APPLY: both drafts go into the saved kit and the file; the drafts are made again from what was kept. Throws when
    /// the file cannot be written, retaining the previous saved kit and the unapplied edits.
    /// </summary>
    public static void Apply()
    {
        foreach ((string weapon, int mask) in s_attachmentEdits)
            if (!WeaponAttachments.IsValid(weapon, mask, LoadoutServices.MenuAttachmentUnlockKills(weapon)))
                throw new InvalidOperationException($"Attachment selection is no longer available for {LoadoutGear.NameOf(weapon)}.");
        LoadoutStore.SaveChoice(Draft());
        Open(s_side, s_context);
    }

    /// <summary>The catalogue of a category (0 gadgets for a slot, 1 weapons, 2 sidearms) of the side shown.</summary>
    public static List<(string Path, int Flags)> Options(int category, int slot)
    {
        GearMenuModel m = Current;
        IReadOnlyList<string> paths = category switch
        {
            1 => m.Weapons,
            2 => m.SidearmOptions,
            _ => m.Options,
        };
        string selected = category switch
        {
            1 => m.ChosenWeapon,
            2 => m.ChosenSidearm,
            _ => slot >= 0 && slot < m.Gadgets.Length ? m.Gadgets[slot] : "",
        };
        var list = new List<(string, int)>();
        foreach (string path in paths)
        {
            bool allowed = m.IsAllowed(path);
            bool levelLocked = m.IsLevelLocked(path);
            bool equipped = category == 1 ? Same(path, m.ChosenWeapon) : m.IsEquipped(path);
            int flags = (allowed ? GearOptionFlags.Allowed : 0) | (levelLocked ? GearOptionFlags.LevelLocked : 0) |
                (!allowed && !levelLocked ? GearOptionFlags.HostDisabled : 0) | (equipped ? GearOptionFlags.Equipped : 0) |
                (Same(path, selected) ? GearOptionFlags.Selected : 0) | (GearIntel.IsQuickUse(path) ? GearOptionFlags.QuickUse : 0) |
                (GearIntel.IsSuppressed(path, GetAttachmentMask(path)) ? GearOptionFlags.Suppressed : 0);
            list.Add((path, flags));
        }

        return list;
    }

    /// <summary>The page picks a card: a gadget for a slot, the weapon or the sidearm. False when it cannot be picked.</summary>
    public static bool Choose(int category, int slot, string path) => category switch
    {
        1 => Current.ChooseWeapon(path),
        2 => Current.ChooseSidearm(path),
        _ => Current.Choose(slot, path),
    };

    /// <summary>A side's saved kit as one line by name, for the lobby's summary row.</summary>
    public static string Summary(int side)
    {
        LoadoutChoice saved = LoadoutStore.Current;
        bool spy = side != 1;
        if (Online.OnlineBridge.Existing?.Client is { State: OnlineClientState.Lobby } visit)
        {
            LobbyState lobby = visit.Lobby;
            var key = (spy, saved, lobby.Setup, lobby.Local?.Rank ?? 0, lobby.SidearmsUnrestricted);
            if (s_lobbyKit != key)
            {
                GearMenuModel model = GearMenuModel.ForLobby(spy, lobby, saved);
                string slots = string.Join(", ", model.Gadgets.Take(model.Slots).Where(g => g.Length != 0).Select(LoadoutGear.NameOf));
                string sidearm = model.SidearmsEnabled ? " + " + LoadoutGear.NameOf(model.ChosenSidearm) : "";
                s_lobbyKitText = spy ? slots : $"{LoadoutGear.NameOf(model.ChosenWeapon)}{sidearm}; {slots}";
                s_lobbyKit = key;
            }

            return s_lobbyKitText;
        }

        IEnumerable<string> gadgets = saved.GadgetsOf(spy).Where(g => !string.IsNullOrEmpty(g)).Select(LoadoutGear.NameOf);
        string kit = string.Join(", ", gadgets);
        return spy ? kit : $"{LoadoutGear.NameOf(saved.MercWeapon)} + {LoadoutGear.NameOf(saved.MercSidearm)}; {kit}";
    }

    private static bool Same(string a, string b) => string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
}

public static unsafe class GearMenuExports
{
    public const int ApiVersion = 2;

    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(GearMenuApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Open = &Open;
        api->SetSide = &SetSide;
        api->GetState = &GetState;
        api->ListOptions = &ListOptions;
        api->Choose = &Choose;
        api->Reset = &Reset;
        api->GetIntel = &GetIntel;
        api->Apply = &Apply;
        api->Summary = &Summary;
        api->SavedJson = &SavedJson;
        api->LastError = &LastError;
        api->GetAttachmentMask = &GetAttachmentMask;
        api->SetAttachmentMask = &SetAttachmentMask;
        return ApiVersion;
    }

    // ---- managed helpers (the probe calls these too) ---------------------------------------------------------------

    public static BridgeGearMenuState State()
    {
        GearMenuModel m = GearEditor.Current;
        BridgeGearMenuState s = default;
        s.Side = m.Spy ? 0 : 1;
        s.GadgetSlots = m.Slots;
        s.CurrentLevel = m.CurrentLevel;
        s.Unrestricted = m.Unrestricted ? 1 : 0;
        s.SidearmsEnabled = m.SidearmsEnabled ? 1 : 0;
        s.RiflesEnabled = m.RiflesEnabled ? 1 : 0;
        s.FromTeam = m.FromTeam ? 1 : 0;
        s.Changed = GearEditor.Changed() ? 1 : 0;
        s.OwnSide = GearEditor.IsOwnSide(s.Side) ? 1 : 0;
        s.Context = GearEditor.Context;
        for (int i = 0; i < BridgeGearMenuState.Slots; i++)
        {
            string g = m.Gadgets[i];
            s.QuickUse[i] = m.Spy && i < 2 && GearIntel.IsQuickUse(g) ? 1 : 0;
            Put(g, s.Gadgets + (i * BridgeGearMenuState.PathCapacity), BridgeGearMenuState.PathCapacity);
            Put(LoadoutGear.NameOf(g), s.GadgetNames + (i * BridgeGearMenuState.NameCapacity), BridgeGearMenuState.NameCapacity);
        }

        // The spy carries the Shocker whatever he picks; the page shows it in the weapon slot.
        string weapon = m.Spy ? OpenScct.Game.Equipment.Loadouts.SpyWeapon.Path ?? "" : m.ChosenWeapon;
        Put(weapon, s.Weapon, BridgeGearMenuState.PathCapacity);
        Put(LoadoutGear.NameOf(weapon), s.WeaponName, BridgeGearMenuState.NameCapacity);
        Put(m.ChosenSidearm, s.Sidearm, BridgeGearMenuState.PathCapacity);
        Put(m.Spy ? "" : LoadoutGear.NameOf(m.ChosenSidearm), s.SidearmName, BridgeGearMenuState.NameCapacity);
        Put(m.Rule, s.Rule, BridgeGearMenuState.RuleCapacity);
        return s;
    }

    public static BridgeGearIntel Intel(string path)
    {
        GearIntelInfo info = GearIntel.Describe(GearEditor.Current, path, GearEditor.GetAttachmentMask(path));
        BridgeGearIntel b = default;
        Put(info.Name, b.Name, BridgeGearIntel.NameCapacity);
        Put(info.Role, b.Role, BridgeGearIntel.NameCapacity);
        Put(info.Description, b.Description, BridgeGearIntel.DescriptionCapacity);
        Put(info.Restriction, b.Restriction, BridgeGearIntel.RestrictionCapacity);
        Put(info.Note, b.Note, BridgeGearIntel.NoteCapacity);
        Put(info.Icon.File, b.IconFile, BridgeGearIntel.FileCapacity);
        b.IconU = info.Icon.U;
        b.IconV = info.Icon.V;
        b.IconSize = info.Icon.Size;
        b.StatCount = Math.Min(info.Stats.Count, BridgeGearIntel.MaxStats);
        for (int i = 0; i < b.StatCount; i++)
        {
            Put(info.Stats[i].Caption, b.StatCaption + (i * BridgeGearIntel.StatCapacity), BridgeGearIntel.StatCapacity);
            Put(info.Stats[i].Value, b.StatValue + (i * BridgeGearIntel.StatCapacity), BridgeGearIntel.StatCapacity);
        }

        return b;
    }

    // ---- entry points --------------------------------------------------------------------------------------------

    /// <summary>Opens the picker: side 0 spy, 1 merc, -1 the side played; context 0 menus, 1 lobby, 2 match. Returns 1 or 0.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Open(int side, int context) => Guard(() =>
    {
        GearEditor.Open(side, context);
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetSide(int side) => Guard(() =>
    {
        GearEditor.SetSide(side);
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(BridgeGearMenuState* state) => Guard(() =>
    {
        if (state == null)
        {
            return 0;
        }

        *state = State();
        return 1;
    });

    /// <summary>A category's cards (0 gadgets for <paramref name="slot"/>, 1 weapons, 2 sidearms). Returns the full count.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ListOptions(int category, int slot, BridgeGearOption* options, int capacity) => Guard(() =>
    {
        List<(string Path, int Flags)> list = GearEditor.Options(category, slot);
        GearMenuModel m = GearEditor.Current;
        for (int i = 0; options != null && i < list.Count && i < capacity; i++)
        {
            BridgeGearOption o = default;
            Put(list[i].Path, o.Path, BridgeLoadout.PathCapacity);
            Put(LoadoutGear.NameOf(list[i].Path), o.Name, BridgeGearItem.NameCapacity);
            Put(GearIntel.SubLine(m, list[i].Path, GearEditor.GetAttachmentMask(list[i].Path)), o.Sub, BridgeGearOption.SubCapacity);
            o.Flags = list[i].Flags;
            o.RequiredLevel = m.RequiredLevel(list[i].Path);
            options[i] = o;
        }

        return list.Count;
    });

    /// <summary>Picks a card. Returns 1 taken, 0 refused (locked, or not of that category).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Choose(int category, int slot, char* path) => Guard(() => GearEditor.Choose(category, slot, Text(path)) ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAttachmentMask(char* path) => Guard(() => GearEditor.GetAttachmentMask(Text(path)));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetAttachmentMask(char* path, int mask) => Guard(() => GearEditor.SetAttachmentMask(Text(path), mask) ? 1 : 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Reset() => Guard(() =>
    {
        GearEditor.Reset();
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetIntel(char* path, BridgeGearIntel* intel) => Guard(() =>
    {
        if (intel == null)
        {
            return 0;
        }

        *intel = Intel(Text(path));
        return 1;
    });

    /// <summary>Saves both drafts as the kit (and its file). Returns 1, or 0 with the reason in LastError.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Apply() => Guard(() =>
    {
        GearEditor.Apply();
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Summary(int side, char* buffer, int capacity) => Guard(() => Copy(GearEditor.Summary(side), buffer, capacity));

    /// <summary>The saved kit as the file holds it (JSON).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SavedJson(char* buffer, int capacity) => Guard(() => Copy(LoadoutStore.ToJson(LoadoutStore.Current), buffer, capacity));

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
