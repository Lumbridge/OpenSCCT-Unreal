using System;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;

/// <summary>
/// OnlineProbe setup [content root] [map]
///
/// Create Session's rules as Unreal's page asks for them, through the host table's SetupText entry: the hostable maps
/// with their pictures and modes, a map's defaults, the page's rows, row changes with the rules between rows
/// (Merc Deathmatch's kills, Revenge's two games, the disk clamp, a change of map keeping the session-wide values)
/// and the Alternate rules. Prints what it got and checks the rules it can check from the text.
/// </summary>
internal static unsafe class SetupProbe
{
    private static HostApi s_host;

    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
        string map = args.Length > 1 ? args[1] : "AquaE";
        HostApi host = default;
        int version = ((delegate* unmanaged<HostApi*, int>)&HostExports.GetApi)(&host);
        s_host = host;
        Console.WriteLine($"host table version {version}, SetupText {(host.SetupText != null ? "present" : "MISSING")}");
        BridgeSession.Configure(content);
        bool ok = version >= 2 && host.SetupText != null;

        string maps = Text(SetupText.Maps, null, null);
        string[] mapLines = maps.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        Console.WriteLine($"maps: {mapLines.Length}");
        foreach (string line in mapLines[..Math.Min(6, mapLines.Length)])
        {
            Console.WriteLine("  " + line.Replace('\t', '|'));
        }
        ok &= mapLines.Length > 0 && Array.Exists(mapLines, l => l.StartsWith(map + "\t", StringComparison.OrdinalIgnoreCase));

        string modes = Text(SetupText.Modes, null, "AquaE");
        Console.WriteLine($"modes of AquaE: {modes}");
        ok &= modes.Contains("Story", StringComparison.Ordinal) && modes.EndsWith("MercDeathMatch", StringComparison.Ordinal);

        string setup = Text(SetupText.Defaults, null, $"?MapName={map}?Mode=Story");
        Console.WriteLine($"defaults of {map} Story: {setup}");
        ok &= setup.Contains($"MapName={map}", StringComparison.OrdinalIgnoreCase) && setup.Contains("GameMode=5", StringComparison.Ordinal);

        bool HasFeatures(string line, char value) => Array.TrueForAll(new[] { "SvmSidearms", "SvmRifles", "SvmResponsiveSmoke" },
            key => OpenScct.Core.Engine.UcOptions.ParseOption(line, key) == value.ToString());
        string old = $"?MapName={map}?GameMode=5?TimeLimit=8?SvmRifles=0?SvmResponsiveSmoke=0?SvmSidearms=0";
        string restored = Text(SetupText.NormalizeNewHost, old, null);
        ok &= Check("fresh and restored new host enable all standard features and keep the saved time limit",
            HasFeatures(setup, '1') && HasFeatures(restored, '1') && restored.Contains("?TimeLimit=8?", StringComparison.Ordinal));
        ok &= Check("live lobby normalization keeps an older server's rules", HasFeatures(Text(SetupText.Normalize, old, null), '0'));
        ok &= Check("explicit low-level host options keep the three override values",
            HasFeatures(OnlineBridge.Instance.HostSetup(old, 4).ToOptions(), '0'));
        using (var offline = BridgeSession.StartLocal("TestBox", "Story", merc: true, bots: 0))
        {
            ok &= Check("default offline match passes all three features to the level rules",
                HasFeatures(offline.Session.Loader.GameOptions, '1'));
        }

        Console.WriteLine("rows:");
        string rows = Text(SetupText.Rows, setup, null);
        foreach (string row in rows.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            Console.WriteLine("  " + row.Replace('\t', '|'));
        }
        foreach (string fixedRule in new[] { "Sidearms", "Rifles", "ResponsiveSmoke" })
        {
            ok &= Check(fixedRule + " is absent from host settings and cannot be edited",
                !rows.Contains(fixedRule + "\t", StringComparison.Ordinal)
                && Text(SetupText.Edit, setup, fixedRule + " 0") == setup);
        }
        ok &= Check("practice and alternate rules remain editable",
            rows.Contains("Practice\tcheck\t", StringComparison.Ordinal) && rows.Contains("Alternate\tcheck\t", StringComparison.Ordinal));

        // Row changes.
        string edited = Edit(setup, "TimeLimit 12");
        ok &= Check("time limit 12", edited.Contains("TimeLimit=12", StringComparison.Ordinal));
        edited = Edit(edited, "TimeLimit 99");
        ok &= Check("time limit clamps to 20", edited.Contains("TimeLimit=20", StringComparison.Ordinal));
        edited = Edit(edited, "Revenge 1");
        ok &= Check("revenge forces two games", edited.Contains("Revenge=True", StringComparison.Ordinal) && edited.Contains("GamesInARow=2", StringComparison.Ordinal));
        edited = Edit(edited, "Bots 1");
        edited = Edit(edited, "BotsPerSide 3");
        ok &= Check("bots per side 3", edited.Contains("SvmBots=1", StringComparison.Ordinal) && edited.Contains("SvmBotsPerSide=3", StringComparison.Ordinal));
        edited = Edit(edited, "Mode MercDeathMatch");
        edited = Edit(edited, "SpyLives 15");
        ok &= Check("merc deathmatch: kills to win", edited.Contains("GameMode=7", StringComparison.Ordinal) && edited.Contains("ScoreLimit=15", StringComparison.Ordinal)
            && edited.Contains("SvmBots=1", StringComparison.Ordinal));
        edited = Edit(edited, "Voice 3");
        ok &= Check("voice open", edited.Contains("SvmVoice=3", StringComparison.Ordinal));

        string other = Array.Find(mapLines, l => !l.StartsWith(map + "\t", StringComparison.OrdinalIgnoreCase))?.Split('\t')[0] ?? map;
        string moved = Edit(edited, "Map " + other);
        ok &= Check($"map {other} keeps the session-wide values", moved.Contains("MapName=" + other, StringComparison.Ordinal) && moved.Contains("SvmVoice=3", StringComparison.Ordinal)
            && moved.Contains("SvmBots=1", StringComparison.Ordinal));
        Console.WriteLine("describe: " + Text(SetupText.Describe, moved, null));

        // Disk Hunt, where a map has it.
        foreach (string line in mapLines)
        {
            string[] parts = line.Split('\t');
            if (parts.Length > 3 && parts[3].Contains("DiskHunt", StringComparison.Ordinal))
            {
                string disks = Text(SetupText.Defaults, null, $"?MapName={parts[0]}?Mode=DiskHunt");
                disks = Edit(disks, "ActiveDisks 1");
                ok &= Check($"disk hunt on {parts[0]}: disks to take never above active disks", disks.Contains("SvmDisks=1?", StringComparison.Ordinal) && disks.Contains("SvmDisksToWin=1?", StringComparison.Ordinal));
                break;
            }
        }

        // Alternate rules.
        string alt = Edit(setup, "Alternate 1");
        alt = EditAlt(alt, "ability 4 0");
        alt = EditAlt(alt, "gadget 1 0 0");
        alt = EditAlt(alt, "slots 1 2");
        Console.WriteLine("alternate rows:");
        string altRows = Text(SetupText.Alternate, alt, null);
        foreach (string row in altRows.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            Console.WriteLine("  " + row.Replace('\t', '|'));
        }
        ok &= Check("alternate: no spy grab, merc gadget 0 off, two merc slots",
            altRows.Contains("ability|4|0|Grab Player|0".Replace('|', '\t'), StringComparison.Ordinal)
            && altRows.Contains("slots\t1\t2\t", StringComparison.Ordinal) && altRows.Contains("gadget\t1\t0\t", StringComparison.Ordinal)
            && Text(SetupText.Describe, alt, null).Contains("alternate", StringComparison.Ordinal));

        Console.WriteLine(ok ? "PROBE PASSED" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    private static bool Check(string what, bool passed)
    {
        Console.WriteLine($"{what}: {(passed ? "ok" : "WRONG")}");
        return passed;
    }

    private static string Edit(string options, string change)
    {
        string next = Text(SetupText.Edit, options, change);
        Console.WriteLine($"edit '{change}': {next}");
        return next;
    }

    private static string EditAlt(string options, string change) => Text(SetupText.EditAlternate, options, change);

    private static string Text(SetupText kind, string? options, string? argument, int maxPlayers = 0)
    {
        fixed (char* o = options, a = argument)
        {
            int length = s_host.SetupText((int)kind, o, a, maxPlayers, null, 0);
            char[] buffer = new char[length + 1];
            fixed (char* b = buffer)
            {
                s_host.SetupText((int)kind, o, a, maxPlayers, b, buffer.Length);
                return new string(b);
            }
        }
    }
}
