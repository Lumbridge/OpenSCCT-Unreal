using System;
using System.Collections.Generic;
using System.Linq;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Loadout;

/// <summary>What one piece of gear is in the picker.</summary>
public enum GearKind
{
    Gadget = 0,
    Weapon = 1,
    Sidearm = 2,
}

/// <summary>Why a piece of gear can or cannot be picked.</summary>
[Flags]
public enum GearFlags
{
    None = 0,

    /// <summary>It can be picked now.</summary>
    Allowed = 1,

    /// <summary>The player's level is below what it requires.</summary>
    LevelLocked = 2,

    /// <summary>The host's equipment rules leave it out (or sidearms or rifles are off).</summary>
    HostDisabled = 4,

    /// <summary>The saved choice holds it.</summary>
    Equipped = 8,
}

public readonly record struct GearItem(string Path, string Name, GearKind Kind, int RequiredLevel, GearFlags Flags);

/// <summary>The side the list is for and the rules it was made under.</summary>
public readonly record struct GearRules(bool Spy, int Slots, int CurrentLevel, bool Unrestricted, bool SidearmsEnabled,
    bool RiflesEnabled, bool FromTeam);

/// <summary>
/// The gear a side can choose from, with the rules of the gear picker (the Godot client's game/Scripts/UI/GearMenuModel.cs
/// IsAllowed and Restriction): the team's gadget options the host allows and the player's level unlocks, the merc's
/// weapons (the additional rifles only when the round has them, each from its level) and the sidearms (from their
/// level). In a round the player's own team decides (its slots, the host's restrictions, his level); before one, or
/// for the other side, the team's stock rules do, with nothing locked by level.
/// </summary>
public static class LoadoutGear
{
    /// <summary>
    /// The list for a side: 0 spy, 1 merc, -1 the local player's side in the running session (the spy's when there is
    /// none).
    /// </summary>
    public static List<GearItem> List(int side, out GearRules rules)
    {
        SPlayerController? pc = Exports.Current?.Session.LocalController;
        STeamInfo? ownTeam = pc?.PlayerReplicationInfo?.Team as STeamInfo;
        bool spy = side switch
        {
            0 => true,
            1 => false,
            _ => ownTeam is not null ? ownTeam is STeamAttaquant : Exports.Current is not { LocalMerc: true },
        };

        STeamInfo team;
        bool fromTeam = ownTeam is not null && (ownTeam is STeamAttaquant) == spy;
        int level = 1;
        bool unrestricted = true, sidearms = true, rifles = false;
        if (fromTeam)
        {
            team = ownTeam!;
            level = (pc!.PlayerReplicationInfo!.SvmRank & 255);
            unrestricted = pc.Level.NetMode == ENetMode.NM_Standalone || pc.PlayerReplicationInfo.SvmSidearmsUnrestricted ||
                OpenScct.Game.Modes.PracticeRules.IsOn(pc);
            sidearms = pc.GameReplicationInfo?.SidearmsEnabled == true;
            rifles = pc.GameReplicationInfo?.RiflesEnabled == true;
        }
        else
        {
            // The team's stock rules, as a round without the host's own settings gives them.
            USPersistentData pd = USPersistentData.CreateInitialized();
            team = spy ? new STeamAttaquant() : new STeamDefenseur();
            team.iNBGadgets = spy ? pd.b_SPY_NBGadget : pd.b_MERC_NBGadget;
            team.bDisallowedInv = spy ? pd.b_SPY_GadgetRestriction : pd.b_MERC_GadgetRestriction;
            if (team is STeamAttaquant spies)
            {
                spies.AdditionalGadgets = pd.AdditionalSpyGadgets;
            }
        }

        int slots = Math.Clamp(team.iNBGadgets, 0, LoadoutChoice.Slots);
        rules = new GearRules(spy, slots, level, unrestricted, sidearms, rifles, fromTeam);
        LoadoutChoice saved = LoadoutStore.Current;
        string[] chosen = saved.GadgetsOf(spy);
        var items = new List<GearItem>();

        int options = Loadouts.GadgetOptionCount(team);
        var gadgets = new List<(string Path, bool HostAllows)>();
        for (int o = 0; o < options; o++)
        {
            string path = team.ClassInventory[o].Path ?? "";
            if (path.Length == 0 || SpyCloak.IsLegacyGadget(path))
            {
                continue;
            }

            gadgets.Add((path, ((Loadouts.GadgetMask(team) >> o) & 1) != 0));
        }

        foreach ((string path, bool hostAllows) in gadgets.OrderBy(g => EquipmentUnlocks.RequiredLevel(g.Path)))
        {
            bool levelOk = EquipmentUnlocks.LevelAllows(path, level, unrestricted);
            bool equipped = chosen.Take(slots).Any(g => Same(g, path));
            items.Add(new GearItem(path, NameOf(path), GearKind.Gadget, EquipmentUnlocks.RequiredLevel(path),
                Flags(hostAllows, levelOk, equipped)));
        }

        if (!spy)
        {
            foreach (ClassRef w in team.ClassWeapon)
            {
                string path = w.Path ?? "";
                bool rifle = MercRifles.Find(path) is not null;
                bool hostAllows = !rifle || rifles;
                bool levelOk = !rifle || EquipmentUnlocks.LevelAllows(path, level, unrestricted);
                items.Add(new GearItem(path, NameOf(path), GearKind.Weapon, EquipmentUnlocks.RequiredLevel(path),
                    Flags(hostAllows, levelOk, Same(saved.MercWeapon, path))));
            }

            foreach (SidearmDefinition pistol in Sidearms.All.OrderBy(p => p.Level))
            {
                items.Add(new GearItem(pistol.CarryId, pistol.Name, GearKind.Sidearm, pistol.Level,
                    Flags(sidearms, Sidearms.Available(pistol.Kind, level, unrestricted), Same(saved.MercSidearm, pistol.CarryId))));
            }
        }

        return items;
    }

    private static GearFlags Flags(bool hostAllows, bool levelOk, bool equipped) =>
        (hostAllows && levelOk ? GearFlags.Allowed : 0) | (levelOk ? 0 : GearFlags.LevelLocked) |
        (hostAllows ? 0 : GearFlags.HostDisabled) | (equipped ? GearFlags.Equipped : 0);

    /// <summary>The name a class path shows (its localized ItemName), as the Godot gear picker names it.</summary>
    public static string NameOf(string path)
    {
        if (MercRifles.Find(path) is { } rifle)
        {
            return rifle.Name;
        }

        if (Sidearms.Find(path) is { } pistol)
        {
            return pistol.Name;
        }

        if (string.IsNullOrEmpty(path))
        {
            return "Empty";
        }

        if (path.StartsWith("Svm.", StringComparison.OrdinalIgnoreCase) &&
            OpenScct.Game.Devices.DeviceClasses.Index.Find(path)?.Create() is Inventory gadget)
        {
            return gadget.ItemName;
        }

        int dot = path.LastIndexOf('.');
        string fallback = dot >= 0 ? path[(dot + 1)..] : path;
        return UcText.Get(path, "ItemName", fallback) is { Length: > 0 } name ? name : fallback;
    }

    /// <summary>
    /// The kit the local pawn carries now, as "gadget, gadget, ... | weapon[, sidearm]" by class name; "" when there
    /// is no local pawn.
    /// </summary>
    public static string LocalInventory()
    {
        if (Exports.Current?.Session.LocalPawn is not { } pawn)
        {
            return "";
        }

        var gadgets = new List<string>();
        for (SGadget? g = pawn.MyGadget; g is not null && gadgets.Count < 32; g = g.MyGadget)
        {
            if (g.IsAPadUpGadget || g is SidearmCarry)
            {
                continue;
            }

            gadgets.Add(g.GetType().Name);
        }

        var weapons = new List<string>();
        if ((pawn.PrimaryWeapon ?? pawn.MyWeapon) is { } weapon)
        {
            weapons.Add(weapon.GetType().Name);
        }

        if (pawn.SidearmWeapon is { } sidearm)
        {
            weapons.Add(sidearm.GetType().Name);
        }

        return (pawn is SPAWNDEFENSE ? "merc " : "spy ") + string.Join(", ", gadgets) + " | " + string.Join(", ", weapons);
    }

    private static bool Same(string a, string b) => string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
}
