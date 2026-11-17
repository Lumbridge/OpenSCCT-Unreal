using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Loadout;

// The console check of the level services (Bridge/OpenScct.UnrealBridge/Loadout): the table sizes the C++ mirror
// asserts; the gear lists; a spy loadout of non-default gadgets set, saved and read back through the exported table;
// a local match on a map whose spawned pawn must carry exactly that kit; the profile choices as the simulation reads
// them; and, for a merc, a gear change asked mid-round with the server's answer and the kit after it.
// Arguments: content folder, map, scratch folder.

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
string scratch = args.Length > 2 ? args[2] : Path.Combine(Path.GetTempPath(), "LoadoutProbe");
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
    Check(sizeof(LoadoutApi) == 112, $"LoadoutApi {sizeof(LoadoutApi)}");
    Check(sizeof(BridgeLoadout) == 1280, $"BridgeLoadout {sizeof(BridgeLoadout)}");
    Check(sizeof(BridgeGearRules) == 32, $"BridgeGearRules {sizeof(BridgeGearRules)}");
    Check(sizeof(BridgeGearItem) == 236, $"BridgeGearItem {sizeof(BridgeGearItem)}");
    Check(sizeof(BridgeGearAnswer) == 200, $"BridgeGearAnswer {sizeof(BridgeGearAnswer)}");
    Check(sizeof(BridgeProfileOptions) == 16, $"BridgeProfileOptions {sizeof(BridgeProfileOptions)}");
}

BridgeSession.Configure(content);
if (!Probe.Run(content, map, file, Check))
{
    failures++;
}

Console.WriteLine(failures == 0 ? "ALL OK" : $"{failures} FAILED");
return failures == 0 ? 0 : 1;

internal static unsafe class Probe
{
    private static BridgeApi s_bridge;
    private static LoadoutApi s_api;

    public static bool Run(string content, string map, string file, Action<bool, string> check)
    {
        delegate* unmanaged<BridgeApi*, int, int> getBridge = &Exports.GetApiSized;
        delegate* unmanaged<LoadoutApi*, int> getApi = &LoadoutExports.GetApi;
        BridgeApi bridge;
        LoadoutApi api;
        getBridge(&bridge, sizeof(BridgeApi));
        int version = getApi(&api);
        s_bridge = bridge;
        s_api = api;
        check(version == LoadoutExports.ApiVersion && api.Version == version, $"GetApi version {version}");

        // The gear lists before any match: the stock rules, nothing locked by level.
        Console.WriteLine("Gear lists (no match):");
        List<(string Path, string Name, int Kind, int Level, int Flags)> spyGear = List(0, out BridgeGearRules spyRules);
        List<(string Path, string Name, int Kind, int Level, int Flags)> mercGear = List(1, out BridgeGearRules mercRules);
        Print("spy", spyRules, spyGear);
        Print("merc", mercGear.Count > 0 ? mercRules : mercRules, mercGear);
        check(spyRules.Spy == 1 && spyRules.Slots == 4 && spyGear.Count > 4, "the spy list has its options and four slots");
        check(mercGear.Any(g => g.Kind == 1) && mercGear.Any(g => g.Kind == 2), "the merc list has weapons and sidearms");

        // A spy kit of gadgets the fixed kit does not hold, in an order of its own.
        string[] defaults = LoadoutChoice.DefaultSpy();
        string[] chosen = spyGear.Where(g => g.Kind == 0 && (g.Flags & 1) != 0 && !defaults.Contains(g.Path, StringComparer.OrdinalIgnoreCase))
            .Select(g => g.Path).Reverse().Take(4).ToArray();
        if (chosen.Length < 4)
        {
            chosen = chosen.Concat(defaults.Where(d => !chosen.Contains(d))).Take(4).ToArray();
        }

        string[] mercChosen = mercGear.Where(g => g.Kind == 0 && (g.Flags & 1) != 0).Select(g => g.Path).Reverse().Take(4).ToArray();
        var choice = LoadoutStore.Fresh();
        choice.SpyGadgets = chosen;
        Console.WriteLine($"Setting spy kit [{string.Join(", ", chosen)}]");
        BridgeLoadout b = LoadoutExports.ToBridge(choice);
        check(api.SetLoadout(&b) == 1, "SetLoadout");
        fixed (char* f = file)
        {
            check(api.Save(f) == 1 && File.Exists(file), "Save writes " + file);
            // The store goes back to the defaults, so what follows is what the file holds.
            LoadoutStore.Set(LoadoutStore.Fresh());
            check(api.Load(f) == 1, "Load reads it back");
        }

        BridgeLoadout back;
        api.GetLoadout(&back);
        LoadoutChoice read = LoadoutExports.FromBridge(in back);
        check(read.SpyGadgets.SequenceEqual(chosen), $"read back [{string.Join(", ", read.SpyGadgets)}]");
        Console.WriteLine("   file: " + File.ReadAllText(file).Replace("\r", "").Replace("\n", " "));

        // The spy's match: the spawned pawn carries the saved kit.
        if (!Match(content, map, merc: false, out string spyKit, check: check))
        {
            check(false, "the spy match started");
            return false;
        }

        string[] expected = chosen.Select(p => p[(p.LastIndexOf('.') + 1)..]).ToArray();
        check(expected.All(e => spyKit.Contains(e, StringComparison.OrdinalIgnoreCase)), $"spawned spy kit [{spyKit}] holds the saved gadgets");
        check(LoadoutServed.Spy > 0, $"the spawn read the saved kit ({LoadoutServed.Spy} times)");

        // Pushed profile choices reach the simulation: objective sounds off, the wheel walking, help on.
        var pushed = new BridgeProfileOptions { ObjectiveSounds = 0, SelectedMouse = 1, DisplayHelpInGame = 1, OfflineSession = -1 };
        api.SetProfileOptions(&pushed);
        BridgeProfileOptions effective;
        api.GetProfileOptions(&effective);
        check(effective.ObjectiveSounds == 0 && effective.SelectedMouse == 1 && effective.DisplayHelpInGame == 1 && effective.OfflineSession == 0,
            $"pushed options read back (objective sounds {effective.ObjectiveSounds}, wheel {effective.SelectedMouse}, help {effective.DisplayHelpInGame}, offline with no match {effective.OfflineSession})");

        // The merc's match with a gear change: spawn with the stock kit, then ask for the other gadgets.
        if (!Match(content, map, merc: true, out string mercKit, change: mercChosen, check: check))
        {
            check(false, "the merc match started");
            return false;
        }

        return true;
    }

    private static bool Match(string content, string map, bool merc, out string kit, string[]? change = null, Action<bool, string>? check = null)
    {
        kit = "";
        int started;
        fixed (char* c = content, m = map, mode = "Story")
        {
            started = s_bridge.Configure(c) != 0 ? s_bridge.StartLocal(m, mode, merc ? 1 : 0, 2) : 0;
        }

        if (started == 0)
        {
            Console.WriteLine("FAILED to start: " + Text(s_bridge.LastError));
            return false;
        }

        Console.WriteLine($"==== {map} as a {(merc ? "merc" : "spy")} with 2 bots");
        var run = Stopwatch.StartNew();
        bool asked = false;
        double askedAt = 0;
        string before = "";
        while (run.Elapsed.TotalSeconds < (change is null ? 5 : 9))
        {
            s_bridge.Update();
            System.Threading.Thread.Sleep(5);
            if (change is not null && !asked && run.Elapsed.TotalSeconds > 3 && Inventory().Length > 0)
            {
                before = Inventory();
                Console.WriteLine($"   kit before the change: [{before}]");
                var choice = LoadoutStore.Current.Clone();
                choice.MercGadgets = change;
                BridgeLoadout b = LoadoutExports.ToBridge(choice);
                s_api.SetLoadout(&b);
                Console.WriteLine($"   asking for merc gadgets [{string.Join(", ", change)}]: RequestGearChange {s_api.RequestGearChange()}");
                asked = true;
                askedAt = run.Elapsed.TotalSeconds;
            }

            BridgeGearAnswer answer;
            while (s_api.PollGearAnswer(&answer) == 1)
            {
                Console.WriteLine($"   t={run.Elapsed.TotalSeconds:F1} server's answer {answer.Result} wait {answer.WaitSeconds:F1}: \"{new string(answer.Text)}\"");
                check?.Invoke(asked, "the answer came after the request");
            }
        }

        string now = Inventory();
        kit = now;
        Console.WriteLine($"   kit: [{now}]");
        if (!merc && check is not null)
        {
            ZoneTips(check);
        }
        if (Exports.Current?.Session.LocalController is SPlayerController pc && pc.LocalProfileOptions() is { } profile)
        {
            Console.WriteLine($"   IProfileOptions as the simulation reads it: ObjectiveSounds={profile.ObjectiveSounds} SelectedMouse={profile.SelectedMouse} " +
                $"DisplayHelpInGame={profile.DisplayHelpInGame} OfflineSession={profile.OfflineSession} ({profile.GetType().Name})");
            check?.Invoke(profile.OfflineSession, "a local match is an offline session for the simulation");
        }
        else
        {
            Console.WriteLine("   FAILED: the local controller has no IProfileOptions");
            check?.Invoke(false, "IProfileOptions installed");
        }

        if (change is not null && check is not null)
        {
            string[] expected = change.Select(p => p[(p.LastIndexOf('.') + 1)..]).ToArray();
            check(expected.All(e => now.Contains(e, StringComparison.OrdinalIgnoreCase)), $"after the change the merc carries [{string.Join(", ", expected)}]");
        }

        s_bridge.Stop();
        return true;
    }

    /// <summary>
    /// The map's zone tips with the local pawn put into them by hand (their Touch, as walking in calls it): a tip shows
    /// only while the simulation reads OfflineSession as true, and a help-only tip only with DisplayHelpInGame.
    /// </summary>
    private static void ZoneTips(Action<bool, string> check)
    {
        if (Exports.Current?.Session is not { } session || session.LocalPawn is not { } pawn || pawn.Controller is not SPlayerController pc ||
            pc.myHUD is not SHud hud)
        {
            check(false, "a local pawn with a HUD for the zone tips");
            return;
        }

        var touch = typeof(SZoneTips).GetMethod("Touch", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
        var untouch = typeof(SZoneTips).GetMethod("UnTouch", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
        var tips = new List<SZoneTips>();
        foreach (SZoneTips t in session.Client.World.Level.AllActors<SZoneTips>())
        {
            tips.Add(t);
        }

        // A tip any offline player sees when there is one; most maps only have help-only ones (AquaE: all of them).
        SZoneTips? tip = tips.FirstOrDefault(t => !t.bShowOnlyWithIngameHelp) ?? tips.FirstOrDefault();
        Console.WriteLine($"   zone tips on the map: {tips.Count} ({tips.Count(t => t.bShowOnlyWithIngameHelp)} help-only); the pawn is at {pawn.Location}");
        foreach (SZoneTips t in tips)
        {
            Console.WriteLine($"      tip at {t.Location} radius {t.CollisionRadius:F0} height {t.CollisionHeight:F0}");
        }
        if (tip is null || touch is null || untouch is null)
        {
            check(false, "the map has a zone tip to stand in");
            return;
        }

        int helpOnly = tip.bShowOnlyWithIngameHelp ? 1 : 0;

        string Shown()
        {
            touch.Invoke(tip, [pawn]);
            string text = hud.sTutoMap;
            untouch.Invoke(tip, [pawn]);
            hud.sTutoMap = "";
            return text;
        }

        BridgeProfileOptions offline = new() { ObjectiveSounds = 1, SelectedMouse = -1, DisplayHelpInGame = helpOnly, OfflineSession = 1 };
        s_api.SetProfileOptions(&offline);
        string shown = Shown();
        Console.WriteLine($"   offline, standing in the tip at {tip.Location}: \"{shown}\"");
        check(shown.Length > 0, "an offline session shows the zone tip");

        BridgeProfileOptions online = offline with { OfflineSession = 0 };
        s_api.SetProfileOptions(&online);
        shown = Shown();
        check(shown.Length == 0, $"pushed OfflineSession=0, the same tip does not show (\"{shown}\")");

        if (tips.FirstOrDefault(t => t.bShowOnlyWithIngameHelp) is { } helpTip)
        {
            tip = helpTip;
            BridgeProfileOptions noHelp = offline with { DisplayHelpInGame = 0 };
            s_api.SetProfileOptions(&noHelp);
            string without = Shown();
            BridgeProfileOptions help = offline with { DisplayHelpInGame = 1 };
            s_api.SetProfileOptions(&help);
            string with = Shown();
            check(without.Length == 0 && with.Length > 0, $"a help-only tip shows only with DisplayHelpInGame (without \"{without}\", with \"{with}\")");
        }

        BridgeProfileOptions back = offline with { DisplayHelpInGame = 0, OfflineSession = -1 };
        s_api.SetProfileOptions(&back);
    }

    private static string Inventory()
    {
        char* text = stackalloc char[512];
        s_api.GetLocalInventory(text, 512);
        return new string(text);
    }

    private static string Text(delegate* unmanaged[Cdecl]<char*, int, int> source)
    {
        char* text = stackalloc char[2048];
        source(text, 2048);
        return new string(text);
    }

    private static List<(string Path, string Name, int Kind, int Level, int Flags)> List(int side, out BridgeGearRules rules)
    {
        var items = new BridgeGearItem[64];
        int count;
        BridgeGearRules r;
        fixed (BridgeGearItem* p = items)
        {
            count = s_api.ListGear(side, &r, p, items.Length);
        }

        rules = r;
        var list = new List<(string, string, int, int, int)>();
        for (int i = 0; i < Math.Min(count, items.Length); i++)
        {
            fixed (char* path = items[i].Path, name = items[i].Name)
            {
                list.Add((new string(path), new string(name), items[i].Kind, items[i].RequiredLevel, items[i].Flags));
            }
        }

        return list;
    }

    private static void Print(string side, BridgeGearRules rules, List<(string Path, string Name, int Kind, int Level, int Flags)> items)
    {
        Console.WriteLine($"  {side}: {items.Count} items, {rules.Slots} slots, level {rules.CurrentLevel}, unrestricted {rules.Unrestricted}, from team {rules.FromTeam}");
        foreach (var item in items)
        {
            Console.WriteLine($"     {(item.Kind == 0 ? "gadget " : item.Kind == 1 ? "weapon " : "sidearm")} {item.Path,-40} {item.Name,-28} level {item.Level,2} flags {item.Flags}");
        }
    }
}
