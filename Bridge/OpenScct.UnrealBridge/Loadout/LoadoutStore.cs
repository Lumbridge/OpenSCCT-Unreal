using System;
using System.IO;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;

namespace OpenScct.UnrealBridge.Loadout;

/// <summary>
/// The player's chosen gear per side (an OpenSCCT addition, spec/additions/gear-change.md): the class paths of the
/// spy's four gadget slots, the merc's four, his weapon and his sidearm. The Godot client keeps the same block in its
/// settings file (game/Scripts/Options/GameSettings.cs LoadoutSettings) under the same JSON names, so the file reads the
/// same in both. New profiles use the fixed gadget kits, the M4 primary and the unsuppressed pistol.
/// </summary>
public sealed class LoadoutChoice
{
    public const int Slots = 4;

    [JsonPropertyName("spy_gadgets")]
    public string[] SpyGadgets { get; set; } = DefaultSpy();

    [JsonPropertyName("merc_gadgets")]
    public string[] MercGadgets { get; set; } = DefaultMerc();

    [JsonPropertyName("merc_weapon")]
    public string MercWeapon { get; set; } = WeaponAttachments.M4;

    [JsonPropertyName("merc_sidearm")]
    public string MercSidearm { get; set; } = DefaultSidearm;

    [JsonPropertyName("weapon_attachments")]
    public Dictionary<string, int> AttachmentMasks { get; set; } = new(StringComparer.OrdinalIgnoreCase);

    public static string DefaultSidearm => Sidearms.Get(SidearmKind.Unsuppressed).CarryId;

    public static string[] DefaultSpy() => Array.ConvertAll(Loadouts.SpyFixedGadgets, c => c.Path ?? "");

    public static string[] DefaultMerc() => Array.ConvertAll(Loadouts.MercFixedGadgets, c => c.Path ?? "");

    /// <summary>The slots of a side.</summary>
    public string[] GadgetsOf(bool spy) => spy ? SpyGadgets : MercGadgets;

    public LoadoutChoice Clone() => new()
    {
        SpyGadgets = (string[])SpyGadgets.Clone(),
        MercGadgets = (string[])MercGadgets.Clone(),
        MercWeapon = MercWeapon,
        MercSidearm = MercSidearm,
        AttachmentMasks = new(AttachmentMasks ?? []),
    };

    /// <summary>
    /// Puts a read or handed-over choice in shape, as the Godot client's LoadoutSettings.Clamp does: four slots a side
    /// (missing ones empty), a spy's retired cloak gadget replaced by a default gadget no slot holds, a pistol found in
    /// a merc gadget slot (older files) moved to the sidearm, and the sidearm a known one.
    /// </summary>
    public void Clamp()
    {
        var masks = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        foreach (var (weapon, mask) in AttachmentMasks ?? [])
        {
            if (mask == 0 || WeaponAttachments.Key(weapon) is not { } key) continue;
            int fitted = WeaponAttachments.EarnedSelection(key, mask, int.MaxValue);
            if (fitted != 0) masks[key] = fitted;
        }
        AttachmentMasks = masks;
        SpyGadgets = Fit(SpyGadgets, DefaultSpy());
        for (int i = 0; i < SpyGadgets.Length; i++)
        {
            if (!SpyCloak.IsLegacyGadget(SpyGadgets[i]))
            {
                continue;
            }

            SpyGadgets[i] = FirstFree(SpyGadgets, DefaultSpy());
        }

        MercGadgets = Fit(MercGadgets, DefaultMerc());
        MercWeapon = MercPrimarySelection.SavedPreference(MercWeapon);
        MercSidearm = (MercSidearm ?? "").Trim();
        bool migrated = false;
        for (int i = 0; i < MercGadgets.Length; i++)
        {
            if (Sidearms.Find(MercGadgets[i]) is not { } pistol)
            {
                continue;
            }

            if (!migrated)
            {
                MercSidearm = pistol.CarryId;
            }

            migrated = true;
            MercGadgets[i] = "";
            MercGadgets[i] = FirstFree(MercGadgets, DefaultMerc());
        }

        if (Sidearms.Find(MercSidearm) is not { } sidearm)
        {
            MercSidearm = DefaultSidearm;
        }
        else
        {
            MercSidearm = sidearm.CarryId;
        }
    }

    private static string FirstFree(string[] slots, string[] candidates)
    {
        foreach (string candidate in candidates)
        {
            if (!Array.Exists(slots, g => string.Equals(g, candidate, StringComparison.OrdinalIgnoreCase)))
            {
                return candidate;
            }
        }

        return "";
    }

    private static string[] Fit(string[]? slots, string[] defaults)
    {
        if (slots is null)
        {
            return defaults;
        }

        var fitted = new string[Slots];
        for (int i = 0; i < Slots; i++)
        {
            fitted[i] = i < slots.Length ? (slots[i] ?? "").Trim() : "";
        }

        return fitted;
    }
}

/// <summary>
/// The saved gear in use. Unreal names the file (Saved/Controls/loadout.json, beside controls.json) and loads it once;
/// the gear picker sets a new choice and saves it. The choice is replaced, never edited in place, so the simulation
/// reading it on a spawn always sees a whole one.
/// </summary>
public static class LoadoutStore
{
    private static readonly JsonSerializerOptions s_json = new() { WriteIndented = true };

    private static LoadoutChoice s_current = Fresh();

    /// <summary>The choice in use (a spawn reads it). Never null.</summary>
    public static LoadoutChoice Current => System.Threading.Volatile.Read(ref s_current);

    /// <summary>The file loaded or saved last ("" before either).</summary>
    public static string FilePath { get; private set; } = "";

    /// <summary>Puts <paramref name="choice"/> in use (a cleaned copy).</summary>
    public static void Set(LoadoutChoice choice)
    {
        ArgumentNullException.ThrowIfNull(choice);
        LoadoutChoice copy = choice.Clone();
        copy.Clamp();
        System.Threading.Volatile.Write(ref s_current, copy);
    }

    /// <summary>The kit a new player starts with.</summary>
    public static LoadoutChoice Fresh()
    {
        var choice = new LoadoutChoice();
        choice.Clamp();
        return choice;
    }

    /// <summary>
    /// Reads <paramref name="path"/> into use and remembers it as the file. A missing file puts the defaults in use and
    /// is not an error; a file that cannot be read keeps the defaults and throws.
    /// </summary>
    public static bool Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        FilePath = Path.GetFullPath(path);
        if (!File.Exists(FilePath))
        {
            Set(Fresh());
            return false;
        }

        LoadoutChoice? read = JsonSerializer.Deserialize<LoadoutChoice>(File.ReadAllText(FilePath), s_json);
        Set(read ?? Fresh());
        return true;
    }

    /// <summary>Writes the choice in use to <paramref name="path"/> (null or empty: the file loaded last).</summary>
    public static void Save(string? path)
    {
        Write(Current, path);
    }

    public static void SaveChoice(LoadoutChoice choice)
    {
        var copy = choice.Clone();
        copy.Clamp();
        Write(copy, null);
        System.Threading.Volatile.Write(ref s_current, copy);
    }

    private static void Write(LoadoutChoice choice, string? path)
    {
        string target = string.IsNullOrWhiteSpace(path) ? FilePath : Path.GetFullPath(path);
        if (target.Length == 0)
        {
            throw new InvalidOperationException("No loadout file was named: load one first or pass a path.");
        }

        string? folder = Path.GetDirectoryName(target);
        if (!string.IsNullOrEmpty(folder))
        {
            Directory.CreateDirectory(folder);
        }

        // Written beside and moved over, so a crash mid-write cannot leave half a file.
        string temp = target + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(choice, s_json));
        File.Move(temp, target, overwrite: true);
        FilePath = target;
    }

    public static string ToJson(LoadoutChoice choice) => JsonSerializer.Serialize(choice, s_json);
}
