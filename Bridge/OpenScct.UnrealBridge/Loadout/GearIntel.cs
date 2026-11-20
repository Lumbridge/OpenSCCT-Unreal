using System;
using System.Collections.Generic;
using System.IO;
using OpenScct.Core.Engine;
using OpenScct.Game.Devices;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Loadout;

/// <summary>A piece of gear's picture: a whole image, or a 64 x 64 tile of an atlas (Size 0 for the whole image).</summary>
public readonly record struct GearIcon(string File, int U, int V, int Size);

/// <summary>What the gear picker's intel panel says about one piece of gear.</summary>
public sealed record GearIntelInfo(string Name, string Role, string Description, string Restriction,
    IReadOnlyList<(string Caption, string Value)> Stats, string Note, GearIcon Icon);

/// <summary>
/// The texts of the gear picker's cards and its "Equipment intel" panel, ported from the Godot-free parts of the
/// Godot client's game/Scripts/UI/GearMenu.cs (AddOption's status and sub line, ShowDetails, the stock weapons'
/// descriptions and the icon lookup): the description, the restriction, the weapon's damage at range, magazine, rate
/// of fire, reload and sights, a gadget's uses and the level it unlocks at.
/// </summary>
public static class GearIntel
{
    private static readonly Dictionary<string, Inventory?> s_items = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>The card's small line under the name.</summary>
    public static string SubLine(GearMenuModel model, string path, int attachments) =>
        !model.IsAllowed(path) && model.RequiredLevel(path) > 0 ? "REQUIRES LEVEL " + model.RequiredLevel(path) :
        MercRifles.Find(path) is { } rifle ? $"{WeaponAttachments.MagazineCapacity(path, attachments)} RDS  /  {rifle.Rpm} RPM" :
        Sidearms.Find(path) is { } sidearm ? $"{sidearm.Magazine} RDS  /  LEVEL {sidearm.Level}" :
        WeaponAttachments.Key(path) != null ? $"{WeaponAttachments.MagazineCapacity(path, attachments)} RDS" :
        IsQuickUse(path) ? "QUICK USE" : "FIELD EQUIPMENT";

    /// <summary>The gadget can be thrown with a quick-use key (the spy's first two slots).</summary>
    public static bool IsQuickUse(string path) => Item(path) is { CanQuickLaunch: true };

    /// <summary>A rifle that is suppressed (the card says so).</summary>
    public static bool IsSuppressed(string path, int attachments) => (attachments & WeaponAttachments.Suppressor) != 0 ||
        Sidearms.Find(path)?.Kind == SidearmKind.Suppressed || MercRifles.Find(path)?.Suppressed == true;

    public static GearIntelInfo Describe(GearMenuModel model, string path, int attachments)
    {
        ArgumentNullException.ThrowIfNull(model);
        path ??= "";
        MercRifleDefinition? rifle = MercRifles.Find(path);
        string description = rifle?.Description ?? StockDescription(path) ?? Sidearms.Find(path)?.Description ??
            (Item(path)?.ItemDescription is { Length: > 0 } text ? text : null) ??
            (path.Length == 0 ? "This slot is empty." : "Select equipment to see its details.");
        string restriction = path.Length > 0 && !model.IsAllowed(path) ? model.Restriction(path) : "";
        var stats = new List<(string, string)>();
        string note = "";
        if (WeaponInfo(path) is { } weapon)
        {
            foreach (int metres in new[] { 0, 10, 25, 50 })
            {
                int body = rifle is { HeadDamage: > 0 } ? rifle.Damage : WeaponRules.DistanceDamage(weapon, metres * 100f);
                int head = rifle is { HeadDamage: > 0 } ? rifle.HeadDamage : body * 3;
                if (weapon is SWeaponDefense_Shotgun shotgun)
                {
                    body = (int)(body * 9 * shotgun.TotalShotgunDamageMultipler);
                    head = (int)(head * 9 * shotgun.TotalShotgunDamageMultipler);
                }

                stats.Add(("DAMAGE " + metres + " M", metres * 100f > weapon.ShootingRange ? "Out of range" : $"{head} head / {body} body"));
            }

            note = rifle is { HeadDamage: > 0 }
                ? "Head and body damage are constant throughout this weapon's effective range."
                : weapon is SWeaponDefense_Shotgun
                ? "Maximum if all 9 pellets hit the same zone. Spread usually reduces damage."
                : "Against a standing Spy. Limbs deal less damage. Damage falls continuously with distance.";
            int magazine = WeaponAttachments.MagazineCapacity(path, attachments);
            stats.Add(("MAGAZINE / RESERVE", $"{magazine} / {weapon.MaxAmmo - weapon.ClipMaxAmmo}"));
            stats.Add(("RATE OF FIRE", rifle?.BurstDefault == true ? "667 auto / 800 burst RPM" : MathF.Round(60f / weapon.RateOfFire) + " RPM"));
            float reload = rifle?.ReloadSeconds ?? Sidearms.Find(path)?.ReloadSeconds ?? ViewModelClips.DataOf(weapon.AFirstReloSt).Seconds(1f);
            if (rifle?.EmptyReloadSeconds is float emptyReload)
                stats.Add(("RELOAD / EMPTY", $"{reload:0.00} / {emptyReload:0.00} s"));
            else
                stats.Add(("RELOAD", reload.ToString("0.00") + " s"));
            stats.Add(("SIGHTS", (attachments & WeaponAttachments.Acog) != 0 ? "ACOG" :
                (attachments & WeaponAttachments.RedDot) != 0 ? "Red dot" :
                rifle is { NativeOpticPower: > 1 } ? $"{rifle.NativeOpticPower:0.#}× scope" :
                rifle != null ? "Iron sights" : weapon.bCanSnipe ? "Scope" : "Hip fire"));
            if (model.RequiredLevel(path) > 0)
            {
                stats.Add(("UNLOCK", "Level " + model.RequiredLevel(path)));
            }
        }
        else if (Item(path) is SGadget gadget)
        {
            stats.Add(("USES", path == OpenScct.Game.Svm.CloneGadget.ClassPath ? "Recharges every 60 s" :
                path == OpenScct.Game.Svm.MineHackGadget.ClassPath ? "2 per life" : gadget.Charge > 0 ? gadget.Charge.ToString() : "Reusable"));
            if (path == OpenScct.Game.Svm.MineHackGadget.ClassPath)
            {
                stats.Add(("HACK TIME", "Hold for 3 s"));
            }

            if (gadget.CanQuickLaunch)
            {
                stats.Add(("QUICK USE", "In the spy's first two slots"));
            }

            if (model.RequiredLevel(path) > 0)
            {
                stats.Add(("UNLOCK", "Level " + model.RequiredLevel(path)));
            }
        }

        return new GearIntelInfo(LoadoutGear.NameOf(path), rifle?.Role ?? "", description, restriction, stats, note, Icon(path));
    }

    /// <summary>
    /// Where the picture of <paramref name="path"/> is in the converted content: the OpenSCCT rifles' and sidearms'
    /// own icons, the stock weapons' first-person icons, else the item's tile of the HUD's weapon or gadget atlas.
    /// The file is "" when nothing is found.
    /// </summary>
    public static GearIcon Icon(string path)
    {
        string? shared = OpenScct.Server.ContentPaths.Shared;
        if (shared is null || string.IsNullOrEmpty(path))
        {
            return new GearIcon("", 0, 0, 0);
        }

        string? file = MercRifles.Find(path) is { } rifle
            ? Path.Combine(shared, "chars", "OpenScctRifles", "textures", rifle.Id.ToLowerInvariant() + "_icon.png")
            : Sidearms.Find(path) is { } sidearm
                ? Path.Combine(shared, "chars", "OpenScctSidearms", "textures", sidearm.Id + "_icon.png")
                : StockAsset(path) is { } stock ? Path.Combine(shared, "chars", "SWeaponFirst", "textures", stock + "_icon.png") : null;
        if (file is not null && File.Exists(file))
        {
            return new GearIcon(file, 0, 0, 0);
        }

        if (Item(path) is { } item)
        {
            string atlas = Path.Combine(shared, "ui", "textures", "SHudTextures", item is SWeapon ? "Icones_Weapons.png" : "Icones_Gadgets.png");
            if (File.Exists(atlas))
            {
                return new GearIcon(atlas, item.IconU, item.IconV, 64);
            }
        }

        return new GearIcon("", 0, 0, 0);
    }

    private static string? StockAsset(string path) => path switch
    {
        "SBase.SWeaponDefense" => "M160",
        "SBase.SWeaponDefense_Uzi" => "Hk81",
        "SBase.SWeaponDefense_Shotgun" => "BM401",
        _ => null,
    };

    private static string? StockDescription(string path) => path switch
    {
        "SBase.SWeaponDefense" => "The FN2000 is the standard Merc assault rifle. Switch between automatic and three-round burst fire, or use its telescopic scope for precise single shots.",
        "SBase.SWeaponDefense_Uzi" => "A compact submachine gun with fast automatic fire. Strong at close range, with faster damage loss over distance.",
        "SBase.SWeaponDefense_Shotgun" => "A pump-action shotgun firing nine pellets in a circular spread. Devastating nearby when the pellets connect; spread and damage loss limit its reach.",
        _ => null,
    };

    private static SWeaponDefense? WeaponInfo(string path)
    {
        if (Sidearms.Find(path) is { } sidearm)
        {
            var pistol = new MercSidearm { Kind = (int)sidearm.Kind };
            pistol.Configure(true);
            return pistol;
        }

        return Item(path) as SWeaponDefense;
    }

    /// <summary>A default instance of the item (made once), for its texts, icon tile and numbers.</summary>
    private static Inventory? Item(string path)
    {
        if (string.IsNullOrEmpty(path))
        {
            return null;
        }

        lock (s_items)
        {
            if (!s_items.TryGetValue(path, out Inventory? item))
            {
                try
                {
                    item = Sidearms.Find(path) is { } pistol ? new SidearmCarry { Kind = (int)pistol.Kind } : DeviceClasses.Index.Find(path)?.Create() as Inventory;
                }
                catch (Exception)
                {
                    // A class that cannot be made outside a level shows without its numbers.
                    item = null;
                }

                s_items[path] = item;
            }

            return item;
        }
    }
}
