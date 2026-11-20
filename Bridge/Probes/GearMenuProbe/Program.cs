using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Loadout;

// The console check of the gear picker's table (Bridge/OpenScct.UnrealBridge/Loadout/GearMenuExports.cs): the sizes
// the C++ mirror asserts; the picker before a match (both sides' drafts, the catalogue, a gadget that swaps slots, the
// merc's weapon and sidearm, RESET, the intel panel with its stats and icon); APPLY writing the kit into the file;
// level locks with the player's level counted; and the picker in a merc's round, where his own side is the played one.
// Arguments: content folder, map, scratch folder.

string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
string scratch = args.Length > 2 ? args[2] : Path.Combine(Path.GetTempPath(), "GearMenuProbe");
Directory.CreateDirectory(scratch);
string file = Path.Combine(scratch, "loadout.json");
File.Delete(file);

int failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"   {(ok ? "ok    " : "FAILED")} {what}");
    failures += ok ? 0 : 1;
}

unsafe
{
    Console.WriteLine("Sizes:");
    Check(sizeof(GearMenuApi) == 112, $"GearMenuApi {sizeof(GearMenuApi)}");
    Check(sizeof(BridgeGearMenuState) == 1912, $"BridgeGearMenuState {sizeof(BridgeGearMenuState)}");
    Check(sizeof(BridgeGearOption) == 328, $"BridgeGearOption {sizeof(BridgeGearOption)}");
    Check(sizeof(BridgeGearIntel) == 3544, $"BridgeGearIntel {sizeof(BridgeGearIntel)}");
    Check(sizeof(BridgeWeaponAttachmentState) == 68, $"BridgeWeaponAttachmentState {sizeof(BridgeWeaponAttachmentState)}");
}

BridgeSession.Configure(content);
Probe.Run(content, map, file, Check);
Console.WriteLine(failures == 0 ? "ALL OK" : $"{failures} FAILED");
return failures == 0 ? 0 : 1;

internal static unsafe class Probe
{
    private static GearMenuApi s_api;

    public static void Run(string content, string map, string file, Action<bool, string> check)
    {
        delegate* unmanaged<GearMenuApi*, int> getApi = &GearMenuExports.GetApi;
        delegate* unmanaged<LoadoutApi*, int> getLoadout = &LoadoutExports.GetApi;
        GearMenuApi api;
        LoadoutApi loadout;
        int version = getApi(&api);
        getLoadout(&loadout);
        s_api = api;
        check(version == GearMenuExports.ApiVersion && api.Version == version, $"GetApi version {version}");
        string legacyFile = Path.ChangeExtension(file, "legacy.json");
        foreach ((string old, string migrated, string stock) in new[]
        {
            ("SBase.SWeaponDefense", WeaponAttachments.M4, MercPrimarySelection.Smg),
            ("sbase.sweapondefense_uzi", MercPrimarySelection.Smg, MercPrimarySelection.Smg),
            ("SBase.SWeaponDefense_Shotgun", MercPrimarySelection.Shotgun, MercPrimarySelection.Shotgun),
        })
        {
            File.WriteAllText(legacyFile, JsonSerializer.Serialize(new LoadoutChoice { MercWeapon = old }));
            LoadoutStore.Load(legacyFile);
            check(LoadoutStore.Current.MercWeapon == migrated, $"loading a saved {old} migrates to {migrated}");
            var restricted = new GearMenuModel(false, 0, [], 0,
                [old, MercPrimarySelection.Smg, MercPrimarySelection.Shotgun, WeaponAttachments.M4], LoadoutStore.Current,
                false, unrestricted: true, riflesEnabled: false);
            var draft = LoadoutStore.Current.Clone();
            restricted.SaveTo(draft);
            check(restricted.Weapons.SequenceEqual(new[] { MercPrimarySelection.Smg, MercPrimarySelection.Shotgun }) &&
                restricted.ChosenWeapon == stock && draft.MercWeapon == migrated,
                "the restricted match offers both stock primaries and preserves the saved preference on Apply");
        }
        fixed (char* f = file)
        {
            loadout.Load(f);
        }

        // ---- the menus' picker -------------------------------------------------------------------------------------
        Console.WriteLine("Spy, before any match:");
        check(api.Open(0, GearEditor.MenuContext) == 1, "Open(spy, menus)");
        Snap s = State();
        Console.WriteLine($"   slots {s.GadgetSlots}, level {s.CurrentLevel}, unrestricted {s.Unrestricted}, weapon \"{s.WeaponName}\", rule \"{s.Rule}\"");
        check(s.Side == 0 && s.GadgetSlots == 4 && s.Changed == 0 && s.OwnSide == 0, "the spy's draft is the saved kit, four slots, not played");
        List<(string Path, string Name, string Sub, int Flags)> gadgets = Options(0, 0);
        foreach (var o in gadgets)
        {
            Console.WriteLine($"      {o.Path,-36} {o.Name,-26} {o.Sub,-18} flags {o.Flags}");
        }

        check(gadgets.Count > 4 && gadgets.Count(o => (o.Flags & GearOptionFlags.Selected) != 0) == 1, $"{gadgets.Count} gadget cards, one selected for slot 1");
        check(gadgets.Count(o => (o.Flags & GearOptionFlags.Equipped) != 0) == 4, "four cards marked equipped");

        // A gadget another slot holds swaps over.
        string slot1 = s.Gadgets[0], slot3 = s.Gadgets[2];
        check(Choose(0, 0, slot3) == 1, $"slot 1 takes slot 3's {slot3}");
        s = State();
        check(s.Gadgets[0] == slot3 && s.Gadgets[2] == slot1 && s.Changed == 1, $"the two swapped ({s.Names[0]} / {s.Names[2]}), the draft is changed");

        // A gadget no slot holds goes into slot 4.
        string fresh = gadgets.Select(o => o.Path).First(p => !s.Gadgets.Contains(p));
        check(Choose(0, 3, fresh) == 1 && State().Gadgets[3] == fresh, $"slot 4 takes {fresh}");
        check(Choose(1, 0, "SBase.SWeaponDefense") == 0, "a spy cannot take a weapon");

        // The intel panel.
        Info intel = Intel(fresh);
        Console.WriteLine($"   intel {intel.Name}: \"{intel.Description}\" icon {intel.IconFile} ({intel.IconU},{intel.IconV} size {intel.IconSize})");
        for (int i = 0; i < intel.StatCount; i++)
        {
            Console.WriteLine($"      {intel.Captions[i],-20} {intel.Values[i]}");
        }

        check(intel.Name.Length > 0 && intel.Description.Length > 0 && intel.StatCount > 0, "the gadget has a name, a description and stats");
        check(File.Exists(intel.IconFile), "its icon is a file of the content");

        // ---- the merc's tab --------------------------------------------------------------------------------------
        Console.WriteLine("Merc tab:");
        api.SetSide(1);
        s = State();
        List<(string Path, string Name, string Sub, int Flags)> weapons = Options(1, 0);
        List<(string Path, string Name, string Sub, int Flags)> sidearms = Options(2, 0);
        Console.WriteLine($"   weapon {s.WeaponName}, sidearm {s.SidearmName}; weapons [{string.Join(", ", weapons.Select(w => w.Name + " " + w.Sub))}]");
        Console.WriteLine($"   sidearms [{string.Join(", ", sidearms.Select(w => w.Name + " " + w.Sub))}]");
        check(s.Side == 1 && weapons.Count == 8 &&
            !weapons.Any(w => MercPrimarySelection.IsLegacy(w.Path)) && sidearms.Count == Sidearms.All.Count,
            "the merc can choose two stock guns, six optional rifles and every sidearm");
        check(s.WeaponName == GearMenuModel.NameOf(WeaponAttachments.M4) &&
            Choose(1, 0, "SBase.SWeaponDefense") == 0 && Choose(1, 0, "SBase.SWeaponDefense_Uzi") == 0 &&
            Choose(1, 0, "SBase.SWeaponDefense_Shotgun") == 0,
            "new profiles start on the M4 and cannot choose the removed catalogue entries");
        string shotgun = weapons.First(w => w.Path.Contains("Shotgun", StringComparison.OrdinalIgnoreCase)).Path;
        string magnum = sidearms.First(w => w.Path.Contains("magnum", StringComparison.OrdinalIgnoreCase)).Path;
        check(Choose(1, 0, shotgun) == 1 && Choose(2, 0, magnum) == 1, "the merc takes the shotgun and the .44 Magnum");
        Info gun = Intel(shotgun);
        Console.WriteLine($"   intel {gun.Name}: {gun.StatCount} stats, note \"{gun.Note}\"");
        for (int i = 0; i < gun.StatCount; i++)
        {
            Console.WriteLine($"      {gun.Captions[i],-20} {gun.Values[i]}");
        }

        check(gun.StatCount >= 8, "the shotgun shows damage at four ranges, magazine, rate of fire, reload and sights");

        // The spy's edits survive the trip to the other tab.
        api.SetSide(0);
        check(State().Gadgets[3] == fresh, "back on the spy tab the draft still holds the change");

        // APPLY.
        check(api.Apply() == 1, "Apply");
        string json = File.ReadAllText(file);
        Console.WriteLine("   file: " + json.Replace("\r", "").Replace("\n", " "));
        LoadoutChoice saved = LoadoutStore.Current;
        check(saved.SpyGadgets[0] == slot3 && saved.SpyGadgets[3] == fresh && saved.MercWeapon == shotgun &&
            saved.MercSidearm.Equals(magnum, StringComparison.OrdinalIgnoreCase), "the saved kit holds both sides' edits");
        check(json.Contains(fresh) && json.Contains(shotgun), "and so does the file");
        check(State().Changed == 0, "after APPLY nothing is waiting");
        char* line = stackalloc char[256];
        api.Summary(1, line, 256);
        Console.WriteLine($"   merc summary: {new string(line)}");
        check(new string(line).Contains(".44"), "the merc summary names his sidearm");

        // RESET puts the side's default kit back in the draft (not saved until APPLY).
        api.Reset();
        s = State();
        check(s.Gadgets.SequenceEqual(LoadoutChoice.DefaultSpy()) && s.Changed == 1,
            "RESET: the spy draft is the fixed kit again, waiting for APPLY");
        api.Open(0, GearEditor.MenuContext);

        // ---- level locks -----------------------------------------------------------------------------------------
        Console.WriteLine("Level locks at level 1:");
        var team = new STeamAttaquant();
        USPersistentData pd = USPersistentData.CreateInitialized();
        team.iNBGadgets = pd.b_SPY_NBGadget;
        team.bDisallowedInv = pd.b_SPY_GadgetRestriction;
        team.AdditionalGadgets = pd.AdditionalSpyGadgets;
        var options = Enumerable.Range(0, Loadouts.GadgetOptionCount(team)).Select(i => team.ClassInventory[i].Path ?? "").ToList();
        var model = new GearMenuModel(true, 4, options, Loadouts.GadgetMask(team), [], LoadoutStore.Current, hub: false, currentLevel: 1, unrestricted: false);
        var locked = model.Options.Where(model.IsLevelLocked).ToList();
        Console.WriteLine($"   locked: [{string.Join(", ", locked.Select(p => LoadoutGear.NameOf(p) + " " + model.RequiredLevel(p)))}]");
        check(locked.Count > 0 && locked.All(p => !model.IsAllowed(p) && model.Restriction(p).StartsWith("Requires level")), "locked gadgets cannot be picked and say the level");
        check(locked.Count == 0 || !model.Choose(0, locked[0]), "choosing a locked gadget is refused");

        // ---- in a merc's round -----------------------------------------------------------------------------------
        // The Gear screen must keep per-weapon edits in its draft until Apply, including while switching tabs.
        string attachmentFolder = Path.Combine(Path.GetDirectoryName(file)!, "attachments");
        Directory.CreateDirectory(attachmentFolder);
        string attachmentKit = Path.Combine(attachmentFolder, "loadout.json");
        File.Delete(attachmentKit);
        int[] earned = new int[WeaponCareerCounters.Count];
        earned[WeaponCareerCounters.IndexOf(WeaponAttachments.M4)] = 50;
        File.WriteAllText(Path.Combine(attachmentFolder, "weapon-kills-0.json"), JsonSerializer.Serialize(earned));
        LoadoutStore.Load(attachmentKit);
        api.Open(1, GearEditor.MenuContext);
        fixed (char* m4 = WeaponAttachments.M4, other = "Svm.AK47")
        {
            int two = WeaponAttachments.RedDot | WeaponAttachments.ExtendedMagazine;
            check(api.SetAttachmentMask(other, WeaponAttachments.RedDot) == 0, "M4 kills do not unlock another gun's sight");
            check(api.SetAttachmentMask(m4, two | WeaponAttachments.Suppressor) == 0, "four points exceed the three-point budget");
            check(api.SetAttachmentMask(m4, two) != 0 && State().Changed == 1, "editing just attachments marks the kit as changed");
            api.SetSide(0);
            check(api.GetAttachmentMask(m4) == two && !File.Exists(attachmentKit), "changing side preserves the unsaved attachment draft");
            api.Open(1, GearEditor.MenuContext);
            check(api.GetAttachmentMask(m4) == 0 && State().Changed == 0, "reopening discards unapplied attachments");
            check(api.SetAttachmentMask(m4, two) != 0 && api.Apply() != 0, "Apply saves the magazine and sight together");
            LoadoutStore.Load(attachmentKit);
            api.Open(1, GearEditor.MenuContext);
            Info configured = Intel(WeaponAttachments.M4);
            check(api.GetAttachmentMask(m4) == two && configured.Values.Contains("30 / 80") && configured.Values.Contains("Red dot"),
                "a reloaded M4 shows its equipped sight and 30-round magazine");
            int acogAndMagazine = WeaponAttachments.Acog | WeaponAttachments.ExtendedMagazine;
            check(api.SetAttachmentMask(m4, two | WeaponAttachments.Acog) == 0 && api.GetAttachmentMask(m4) == two,
                "trying to fit both optics leaves the current setup intact");
            check(api.SetAttachmentMask(m4, acogAndMagazine) == 1 && api.Apply() == 1 &&
                Intel(WeaponAttachments.M4).Values.Contains("ACOG"), "replacing the red dot with the ACOG consumes its full two points");
            api.Reset();
            check(api.GetAttachmentMask(m4) == 0 && api.Apply() != 0 &&
                Intel(WeaponAttachments.M4).Values.Contains("20 / 80"), "resetting merc gear restores the original 20-round magazine");
        }
        CheckAttachmentCatalogue(attachmentFolder, check);
        LoadoutStore.Load(file);

        delegate* unmanaged<BridgeApi*, int, int> getBridge = &Exports.GetApiSized;
        BridgeApi bridge;
        getBridge(&bridge, sizeof(BridgeApi));
        int started;
        fixed (char* c = content, m = map, mode = "Story")
        {
            started = bridge.Configure(c) != 0 ? bridge.StartLocal(m, mode, 1, 2) : 0;
        }

        check(started != 0, $"a local match on {map} as a merc");
        if (started == 0)
        {
            return;
        }

        var run = Stopwatch.StartNew();
        while (run.Elapsed.TotalSeconds < 4)
        {
            bridge.Update();
            System.Threading.Thread.Sleep(5);
        }

        Console.WriteLine("In the round:");
        api.Open(-1, GearEditor.MatchContext);
        s = State();
        Console.WriteLine($"   side {s.Side}, own {s.OwnSide}, from team {s.FromTeam}, slots {s.GadgetSlots}, rule \"{s.Rule}\"");
        check(s.Side == 1 && s.OwnSide == 1 && s.FromTeam == 1, "the picker opens on the side played, decided by his team");
        var matchWeapons = Options(1, 0).Select(o => o.Path).ToArray();
        check(matchWeapons.Contains(MercPrimarySelection.Smg) && matchWeapons.Contains(MercPrimarySelection.Shotgun) &&
            !matchWeapons.Any(MercPrimarySelection.IsLegacy) &&
            matchWeapons.Any(MercPrimarySelection.RequiresRifleOption) == GearEditor.Current.RiflesEnabled,
            "the live match offers the replacement stock guns and obeys the room's optional-rifle setting");
        var armoury = LoadoutGear.List(1, out _).Where(item => item.Kind == GearKind.Weapon).ToArray();
        check(armoury.Length == 8 && !armoury.Any(item => MercPrimarySelection.IsLegacy(item.Path)) &&
            armoury.Single(item => item.Flags.HasFlag(GearFlags.Equipped)).Path == GearEditor.Current.ChosenWeapon,
            "the Armoury catalogue agrees with Change Gear on the equipped primary");
        api.SetSide(0);
        s = State();
        check(s.OwnSide == 0 && s.Rule.Contains("other side"), "the spy tab is the other side's kit");
        bridge.Stop();
    }

    private static void CheckAttachmentCatalogue(string folder, Action<bool, string> check)
    {
        string[] rifles = ["SBase.SWeaponDefense", "SBase.SWeaponDefense_Uzi", .. MercRifles.All.Select(r => r.ClassPath).Where(p => p is not ("Svm.PumpShotgun" or "Svm.BoltRifle"))];
        var catalogue = rifles.Select(w => (Weapon: w, Capacity: 20, CanExtend: true))
            .Append(("SBase.SWeaponDefense_Shotgun", 6, false))
            .Append(("Svm.PumpShotgun", 6, false))
            .Append(("Svm.BoltRifle", 5, false))
            .Concat(Sidearms.All.Select(s => (s.CarryId, s.Magazine, false))).ToArray();
        string profile = Path.Combine(folder, "all-guns");
        Directory.CreateDirectory(profile);
        string kit = Path.Combine(profile, "loadout.json");
        File.Delete(kit);
        File.WriteAllText(Path.Combine(profile, "weapon-kills-0.json"), JsonSerializer.Serialize(
            Enumerable.Repeat(50, WeaponCareerCounters.Count).ToArray()));
        LoadoutStore.Load(kit);
        s_api.Open(1, GearEditor.MenuContext);
        delegate* unmanaged<WeaponAttachmentsApi*, int> getApi = &WeaponAttachmentsExports.GetApi;
        WeaponAttachmentsApi attachments;
        check(getApi(&attachments) == 3 && attachments.Version == 3, "attachment bridge ABI v3");
        foreach ((string weapon, int capacity, bool canExtend) in catalogue)
        {
            fixed (char* path = weapon)
            {
                BridgeWeaponAttachmentState state;
                bool read = attachments.GetState(path, &state) == 1;
                bool thresholds = read && state.KillsRequired[0] == 20 && state.KillsRequired[1] == 10 &&
                    state.KillsRequired[2] == 30 && state.KillsRequired[3] == 40 && state.KillsRequired[4] == 50;
                bool support = read && (state.Supported & WeaponAttachments.Acog) != 0 && state.Costs[4] == 2 &&
                    ((state.Supported & WeaponAttachments.ExtendedMagazine) != 0) == canExtend;
                bool stock = state.BaseMagazine == capacity && Intel(weapon).Values.Any(v => v.StartsWith(capacity + " / "));
                check(thresholds && support && stock, $"{weapon} shows the correct magazine, unlocks and ACOG cost");
                if (weapon is "Svm.CompactSMG" or "Svm.PumpShotgun" or "Svm.BoltRifle" or "Svm.DesignatedMarksman")
                {
                    string picture = Intel(weapon).IconFile;
                    check(File.Exists(picture) && Path.GetFileName(picture) == weapon[4..].ToLowerInvariant() + "_icon.png",
                        $"{weapon} uses its own rendered equipment picture");
                }
                if (weapon is "Svm.BoltRifle" or "Svm.DesignatedMarksman")
                {
                    string damage = weapon == "Svm.BoltRifle" ? "100 head / 70 body" : "60 head / 35 body";
                    int zoom = weapon == "Svm.BoltRifle" ? 6 : 4;
                    Info baseIntel = Intel(weapon);
                    check(baseIntel.Values.Count(v => v == damage) == 4 && state.NativeOpticPower == zoom &&
                        baseIntel.Values.Contains($"{zoom}× scope"), $"{weapon} displays its fixed damage at every range and its native scope");
                }
                int choice = WeaponAttachments.Acog | (canExtend ? WeaponAttachments.ExtendedMagazine : 0);
                check(s_api.SetAttachmentMask(path, choice) == 1 && Intel(weapon).Values.Contains("ACOG") &&
                    Intel(weapon).Values.Any(v => v.StartsWith((canExtend ? 30 : capacity) + " / ")),
                    $"{weapon} updates its sight and capacity from the draft");
            }
        }

        string legacy = Path.Combine(folder, "old-thresholds");
        Directory.CreateDirectory(legacy);
        string oldKit = Path.Combine(legacy, "loadout.json");
        int[] oldKills = new int[WeaponCareerCounters.Count];
        oldKills[WeaponCareerCounters.IndexOf(WeaponAttachments.M4)] = 30;
        oldKills[WeaponCareerCounters.IndexOf("Svm.BoltRifle")] = 50;
        File.WriteAllText(Path.Combine(legacy, "weapon-kills-0.json"), JsonSerializer.Serialize(oldKills));
        var old = LoadoutStore.Fresh();
        old.AttachmentMasks[WeaponAttachments.M4] = WeaponAttachments.RedDot | WeaponAttachments.ExtendedMagazine;
        old.AttachmentMasks["Svm.BoltRifle"] = WeaponAttachments.Acog | WeaponAttachments.ExtendedMagazine;
        File.WriteAllText(oldKit, LoadoutStore.ToJson(old));
        LoadoutStore.Load(oldKit);
        s_api.Open(1, GearEditor.MenuContext);
        fixed (char* bolt = "Svm.BoltRifle")
        {
            BridgeWeaponAttachmentState state;
            check(attachments.GetState(bolt, &state) == 1 && state.BaseMagazine == 5 &&
                (state.Supported & WeaponAttachments.ExtendedMagazine) == 0 && state.Equipped == WeaponAttachments.Acog &&
                s_api.GetAttachmentMask(bolt) == WeaponAttachments.Acog && Intel("Svm.BoltRifle").Values.Contains("5 / 40"),
                "a previous bolt kit keeps its scope and lists five rounds after removing the extended magazine");
        }
        fixed (char* path = WeaponAttachments.M4)
        {
            BridgeWeaponAttachmentState state;
            check(attachments.GetState(path, &state) == 1 && state.Equipped == WeaponAttachments.RedDot &&
                s_api.GetAttachmentMask(path) == WeaponAttachments.RedDot &&
                Intel(WeaponAttachments.M4).Values.Contains("20 / 80"), "an old 30-kill setup retains its earned sight and uses its standard magazine");
        }
    }

    internal sealed record Snap(int Side, int GadgetSlots, int CurrentLevel, int Unrestricted, int Changed, int OwnSide, int FromTeam,
        string[] Gadgets, string[] Names, string WeaponName, string SidearmName, string Rule);

    internal sealed record Info(string Name, string Description, string Note, string IconFile, int IconU, int IconV, int IconSize,
        int StatCount, string[] Captions, string[] Values);

    private static Snap State()
    {
        BridgeGearMenuState s;
        s_api.GetState(&s);
        var gadgets = new string[4];
        var names = new string[4];
        for (int i = 0; i < 4; i++)
        {
            gadgets[i] = new string(s.Gadgets + (i * BridgeGearMenuState.PathCapacity));
            names[i] = new string(s.GadgetNames + (i * BridgeGearMenuState.NameCapacity));
        }

        return new Snap(s.Side, s.GadgetSlots, s.CurrentLevel, s.Unrestricted, s.Changed, s.OwnSide, s.FromTeam, gadgets, names,
            new string(s.WeaponName), new string(s.SidearmName), new string(s.Rule));
    }

    private static int Choose(int category, int slot, string path)
    {
        fixed (char* p = path)
        {
            return s_api.Choose(category, slot, p);
        }
    }

    private static Info Intel(string path)
    {
        BridgeGearIntel b;
        fixed (char* p = path)
        {
            s_api.GetIntel(p, &b);
        }

        var captions = new string[b.StatCount];
        var values = new string[b.StatCount];
        for (int i = 0; i < b.StatCount; i++)
        {
            captions[i] = new string(b.StatCaption + (i * BridgeGearIntel.StatCapacity));
            values[i] = new string(b.StatValue + (i * BridgeGearIntel.StatCapacity));
        }

        return new Info(new string(b.Name), new string(b.Description), new string(b.Note), new string(b.IconFile), b.IconU, b.IconV,
            b.IconSize, b.StatCount, captions, values);
    }

    private static List<(string Path, string Name, string Sub, int Flags)> Options(int category, int slot)
    {
        var items = new BridgeGearOption[64];
        int count;
        fixed (BridgeGearOption* p = items)
        {
            count = s_api.ListOptions(category, slot, p, items.Length);
        }

        var list = new List<(string, string, string, int)>();
        for (int i = 0; i < Math.Min(count, items.Length); i++)
        {
            fixed (char* path = items[i].Path, name = items[i].Name, sub = items[i].Sub)
            {
                list.Add((new string(path), new string(name), new string(sub), items[i].Flags));
            }
        }

        return list;
    }

}
