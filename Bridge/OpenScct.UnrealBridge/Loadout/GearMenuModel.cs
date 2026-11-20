using System;
using System.Collections.Generic;
using System.Linq;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Loadout;

/// <summary>
/// The gear picker's rules, ported from the Godot client's game/Scripts/UI/GearMenuModel.cs (an OpenSCCT addition,
/// spec/additions/gear-change.md, laid out as the original's <c>Edit_Profile</c> and <c>Edit_Gadgets</c> pages): the
/// side's gadget slots, each set from the team's options the host allows and the player's level unlocks (a gadget
/// another slot holds swaps over), the merc's weapon and sidearm. Saving writes the choice into a
/// <see cref="LoadoutChoice"/>; the server decides when it goes on. Unreal draws it (Source/OpenSCCT/Menu/SScctGearPage)
/// through <see cref="GearMenuExports"/>.
/// </summary>
public sealed class GearMenuModel
{
    public const string Empty = "Empty";

    public static readonly string MatchRule =
        $"New gear goes on now if you spawned less than {GearK.SwapWindowSeconds:0} s ago and have not used anything; " +
        "otherwise it goes on at your next spawn.";

    public const string HubRule = "New gear goes on now; you can change it every 10 s.";

    /// <summary>A practice round's rule (an OpenSCCT addition): any change goes on at once.</summary>
    public const string PracticeRule = "Practice: new gear goes on now.";

    /// <summary>The lobby's line (the round has not started).</summary>
    public const string LobbyRule = "Your gear goes on when the round starts; you can still change it in the round.";

    /// <summary>The line before any match (the offline page and the main menu): Unreal's own addition.</summary>
    public const string OfflineRule = "Your gear goes on when the match starts.";

    private readonly IReadOnlyList<string> _options;
    private readonly int _allowed;
    private string _preferredWeapon;

    public GearMenuModel(bool spy, int slots, IReadOnlyList<string> options, int allowedMask, IReadOnlyList<string> weapons,
        LoadoutChoice saved, bool hub, int currentLevel = 1, bool unrestricted = false, bool riflesEnabled = false,
        bool sidearmsEnabled = true)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(weapons);
        ArgumentNullException.ThrowIfNull(saved);
        Spy = spy;
        Slots = Math.Clamp(slots, 0, LoadoutChoice.Slots);
        _options = options.ToArray();
        CurrentLevel = currentLevel;
        Unrestricted = unrestricted;
        _allowed = allowedMask;
        RiflesEnabled = riflesEnabled;
        SidearmsEnabled = sidearmsEnabled;
        Weapons = MercPrimarySelection.Catalogue(weapons, riflesEnabled);
        Hub = hub;
        string[] held = saved.GadgetsOf(spy);
        Gadgets = new string[LoadoutChoice.Slots];
        for (int i = 0; i < Gadgets.Length; i++)
        {
            Gadgets[i] = i < held.Length ? held[i] ?? "" : "";
        }

        _preferredWeapon = spy ? "" : MercPrimarySelection.SavedPreference(saved.MercWeapon);
        ChosenWeapon = _preferredWeapon;
        ChosenSidearm = spy ? "" : saved.MercSidearm;
        bool migrated = false;
        for (int i = 0; !spy && i < Gadgets.Length; i++)
        {
            if (Sidearms.Find(Gadgets[i]) is not { } pistol)
            {
                continue;
            }

            if (!migrated)
            {
                ChosenSidearm = pistol.CarryId;
            }

            migrated = true;
            Gadgets[i] = LoadoutChoice.DefaultMerc().FirstOrDefault(g => !Gadgets.Contains(g)) ?? "";
        }

        Normalize();
    }

    public bool Spy { get; }
    public int CurrentLevel { get; }
    public bool Unrestricted { get; }
    public bool Hub { get; }

    /// <summary>Gadget slots the host gives this side.</summary>
    public int Slots { get; }

    /// <summary>The chosen class path of each of the four slots ("" for empty).</summary>
    public string[] Gadgets { get; }

    /// <summary>The side's weapons (empty for spies: they only have the Shocker).</summary>
    public IReadOnlyList<string> Weapons { get; }

    public string ChosenWeapon { get; private set; }
    public bool SidearmsEnabled { get; }
    public bool RiflesEnabled { get; }
    public string ChosenSidearm { get; private set; }

    /// <summary>The player's own team in a running round decided the rules (not the team's stock ones).</summary>
    public bool FromTeam { get; init; }

    public IReadOnlyList<string> SidearmOptions =>
        Spy || !SidearmsEnabled ? [] : Sidearms.All.OrderBy(p => p.Level).Select(p => p.CarryId).ToArray();

    public string Rule => (Spy ? "Cloak is standard spy equipment and uses no gadget slot. " : "") + (RuleText ?? (Hub ? HubRule : MatchRule));

    /// <summary>The line about when new gear goes on, when it is neither the match's nor the hub's.</summary>
    public string? RuleText { get; init; }

    /// <summary>
    /// The picker for the player's own side in a running round: the team's slots, options, the host's restrictions
    /// and his level. Null before the player has a team.
    /// </summary>
    public static GearMenuModel? For(SPlayerController? pc, LoadoutChoice saved, bool hub, LobbyState? lobby = null)
    {
        if (pc?.PlayerReplicationInfo?.Team is not STeamInfo team)
        {
            return null;
        }

        bool practice = OpenScct.Game.Modes.PracticeRules.IsOn(pc) || lobby?.Setup.Practice == true;
        // The roster already carries the server-verified rank while the new pawn is still replicating.
        int rank = pc.PlayerReplicationInfo.SvmRank;
        if (rank == 0) rank = lobby?.Local?.Rank ?? 0;
        return Build(team, saved, hub, Math.Max(1, rank & 255),
            pc.Level.NetMode == ENetMode.NM_Standalone || pc.PlayerReplicationInfo.SvmSidearmsUnrestricted || lobby?.SidearmsUnrestricted == true || practice,
            pc.GameReplicationInfo?.RiflesEnabled == true, hub || pc.GameReplicationInfo?.SidearmsEnabled == true,
            !hub && practice ? PracticeRule : null, fromTeam: true);
    }

    /// <summary>
    /// The picker of the lobby's Equipment button: the slots and allowed gadgets the session's setup will give that
    /// side (its Alternate Mode values), as the round's team info will hold them. Side 0 spy, 1 merc.
    /// </summary>
    public static GearMenuModel ForLobby(bool spy, LobbyState lobby, LoadoutChoice saved)
    {
        ArgumentNullException.ThrowIfNull(lobby);
        LobbySetup setup = lobby.Setup;
        var pd = new USPersistentData();
        setup.Alternate.ApplyTo(pd, setup.Mode);
        int level = Math.Max(1, (lobby.Local?.Rank ?? 0) & 255);
        return Build(StockTeam(spy, pd), saved, hub: false, level, lobby.SidearmsUnrestricted || setup.Practice,
            setup.Rifles, setup.Sidearms, LobbyRule, fromTeam: false);
    }

    /// <summary>
    /// The picker before any match (the offline page, or the other side's tab in a round): the team's stock rules,
    /// nothing locked by level, sidearms and rifles shown.
    /// </summary>
    public static GearMenuModel ForStock(bool spy, LoadoutChoice saved, string ruleText)
    {
        USPersistentData pd = USPersistentData.CreateInitialized();
        return Build(StockTeam(spy, pd), saved, hub: false, 1, unrestricted: true, riflesEnabled: true, sidearmsEnabled: true,
            ruleText, fromTeam: false);
    }

    private static STeamInfo StockTeam(bool spy, USPersistentData pd)
    {
        STeamInfo team = spy ? new STeamAttaquant() : new STeamDefenseur();
        team.iNBGadgets = spy ? pd.b_SPY_NBGadget : pd.b_MERC_NBGadget;
        team.bDisallowedInv = spy ? pd.b_SPY_GadgetRestriction : pd.b_MERC_GadgetRestriction;
        if (team is STeamAttaquant spies)
        {
            spies.AdditionalGadgets = pd.AdditionalSpyGadgets;
        }

        return team;
    }

    private static GearMenuModel Build(STeamInfo team, LoadoutChoice saved, bool hub, int level, bool unrestricted,
        bool riflesEnabled, bool sidearmsEnabled, string? ruleText, bool fromTeam)
    {
        bool spy = team is STeamAttaquant;
        var options = new List<string>();
        for (int i = 0; i < Loadouts.GadgetOptionCount(team); i++)
        {
            options.Add(team.ClassInventory[i].Path ?? "");
        }

        var weapons = new List<string>();
        if (!spy)
        {
            foreach (ClassRef w in team.ClassWeapon)
            {
                weapons.Add(w.Path ?? "");
            }
        }

        return new GearMenuModel(spy, team.iNBGadgets, options, Loadouts.GadgetMask(team), weapons, saved, hub, level,
            unrestricted, riflesEnabled, sidearmsEnabled)
        {
            RuleText = ruleText,
            FromTeam = fromTeam,
        };
    }

    /// <summary>The name a class path shows (its localized ItemName), or "Empty".</summary>
    public static string NameOf(string path) => LoadoutGear.NameOf(path);

    /// <summary>The HUD line for the server's answer.</summary>
    public static string ResultText(GearChangeResult result, float waitSeconds) => result switch
    {
        GearChangeResult.Now => "Gear changed.",
        GearChangeResult.Cooldown => $"Wait {Math.Max(1, (int)Math.Ceiling(waitSeconds))} s before changing gear again.",
        _ => "Gear will change on your next spawn.",
    };

    /// <summary>Whether the host and the player's level allow <paramref name="path"/>.</summary>
    public bool IsAllowed(string path)
    {
        if (SpyCloak.IsLegacyGadget(path))
        {
            return false;
        }

        if (Sidearms.Find(path) is { } pistol)
        {
            return !Spy && SidearmsEnabled && Sidearms.Available(pistol.Kind, CurrentLevel, Unrestricted);
        }

        if (MercRifles.Find(path) is not null)
        {
            return !Spy && ContainsWeapon(path) && EquipmentUnlocks.LevelAllows(path, CurrentLevel, Unrestricted);
        }

        if (ContainsWeapon(path))
        {
            return !Spy;
        }

        int option = IndexOf(path);
        return option >= 0 && ((_allowed >> option) & 1) != 0 && EquipmentUnlocks.LevelAllows(path, CurrentLevel, Unrestricted);
    }

    /// <summary>The player's level is below what <paramref name="path"/> needs (and levels count here).</summary>
    public bool IsLevelLocked(string path) => !Unrestricted && Math.Max(1, CurrentLevel) < RequiredLevel(path);

    public int RequiredLevel(string path) => Sidearms.Find(path)?.Level ?? EquipmentUnlocks.RequiredLevel(path);

    public string Restriction(string path) => IsLevelLocked(path)
        ? "Requires level " + RequiredLevel(path) + "."
        : "Disabled by the host's equipment rules.";

    /// <summary>The team's options in order of the level they unlock at (the gadget grid).</summary>
    public IReadOnlyList<string> Options => _options.Where(path => path.Length > 0 && !SpyCloak.IsLegacyGadget(path)).OrderBy(RequiredLevel).ToArray();

    /// <summary>Whether a slot (or the sidearm) holds <paramref name="option"/> (the grid's "Equipped").</summary>
    public bool IsEquipped(string option)
    {
        if (!Spy && Same(ChosenSidearm, option))
        {
            return true;
        }

        for (int i = 0; i < Slots; i++)
        {
            if (option.Length > 0 && Same(Gadgets[i], option))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// <c>Edit_Gadgets.ButtonClick</c>: slot <paramref name="slot"/> takes <paramref name="option"/>; a slot that
    /// held it takes the slot's old gadget (the two swap). Options the host bans cannot be picked. Returns whether it was taken.
    /// </summary>
    public bool Choose(int slot, string option)
    {
        if (slot < 0 || slot >= Slots || Sidearms.Find(option) is not null || !IsAllowed(option))
        {
            return false;
        }

        for (int i = 0; i < Slots; i++)
        {
            if (i != slot && Same(Gadgets[i], option))
            {
                Gadgets[i] = Gadgets[slot];
            }
        }

        Gadgets[slot] = option;
        return true;
    }

    /// <summary><c>Edit_Profile</c>'s Reset: the side's default kit (DefaultProfile.ini), then the host's restrictions.</summary>
    public void Reset()
    {
        string[] defaults = Spy ? LoadoutChoice.DefaultSpy() : LoadoutChoice.DefaultMerc();
        Array.Copy(defaults, Gadgets, Math.Min(defaults.Length, Gadgets.Length));
        if (!Spy)
        {
            _preferredWeapon = WeaponAttachments.M4;
            ChosenSidearm = LoadoutChoice.DefaultSidearm;
        }

        Normalize();
    }

    public bool ChooseWeapon(string option)
    {
        if (Spy || !ContainsWeapon(option) || !IsAllowed(option))
        {
            return false;
        }

        _preferredWeapon = ChosenWeapon = option;
        return true;
    }

    public bool ChooseSidearm(string option)
    {
        if (Spy || Sidearms.Find(option) is not { } pistol || !IsAllowed(option))
        {
            return false;
        }

        ChosenSidearm = pistol.CarryId;
        return true;
    }

    /// <summary>Writes the choice into <paramref name="target"/> (slots past the host's count keep what they held).</summary>
    public void SaveTo(LoadoutChoice target)
    {
        ArgumentNullException.ThrowIfNull(target);
        string[] slots = target.GadgetsOf(Spy);
        if (!Spy)
        {
            for (int i = Slots; i < slots.Length; i++)
            {
                if (Sidearms.Find(slots[i]) is not null)
                {
                    slots[i] = Gadgets[i];
                }
            }
        }

        for (int i = 0; i < Slots && i < slots.Length; i++)
        {
            slots[i] = Gadgets[i];
        }

        if (!Spy)
        {
            target.MercSidearm = ChosenSidearm;
        }

        if (!Spy && _preferredWeapon.Length > 0)
        {
            target.MercWeapon = _preferredWeapon;
        }
    }

    /// <summary>
    /// What a spawn would give: a slot holding an option the host bans, or one an earlier slot holds, takes the first
    /// allowed option no slot holds. A restricted primary takes the same legal replacement as it would on spawn.
    /// </summary>
    private void Normalize()
    {
        if (!Spy && SidearmsEnabled && (Sidearms.Find(ChosenSidearm) is null || !IsAllowed(ChosenSidearm)))
        {
            ChosenSidearm = LoadoutChoice.DefaultSidearm;
        }

        for (int s = 0; s < Slots; s++)
        {
            string g = Gadgets[s];
            if (Sidearms.Find(g) is not null)
            {
                if (!IsAllowed(g) || Array.FindIndex(Gadgets, 0, s, x => Sidearms.Find(x) is not null) >= 0)
                {
                    Gadgets[s] = "";
                }

                continue;
            }

            bool duplicate = false;
            for (int e = 0; e < s; e++)
            {
                duplicate |= g.Length > 0 && Same(Gadgets[e], g);
            }

            if (g.Length == 0 || (IsAllowed(g) && !duplicate))
            {
                continue;
            }

            Gadgets[s] = "";
            foreach (string option in _options)
            {
                if (option.Length > 0 && IsAllowed(option) && !HeldElsewhere(option, s))
                {
                    Gadgets[s] = option;
                    break;
                }
            }
        }

        // The host can restrict this match while the profile keeps its preferred primary for the next match.
        if (!Spy) ChosenWeapon = MercPrimarySelection.Choose(_preferredWeapon, Weapons, IsAllowed);
    }

    private bool ContainsWeapon(string path) => Weapons.Any(w => Same(w, path));

    private bool HeldElsewhere(string option, int slot)
    {
        for (int i = 0; i < Slots; i++)
        {
            if (i != slot && Same(Gadgets[i], option))
            {
                return true;
            }
        }

        return false;
    }

    private int IndexOf(string path)
    {
        for (int i = 0; i < _options.Count; i++)
        {
            if (Same(_options[i], path))
            {
                return i;
            }
        }

        return -1;
    }

    private static bool Same(string a, string b) => string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
}
