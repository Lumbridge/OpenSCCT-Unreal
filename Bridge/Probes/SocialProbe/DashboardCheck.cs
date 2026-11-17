using System;
using System.Diagnostics;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Social;

/// <summary>
/// <c>SocialProbe dashboard [masterUrl] [inviteKey]</c>: the main menu dashboard against a master, without Unreal. It
/// checks the table's layout, watches the dashboard until the status, the lists and the changelog answered (or failed),
/// prints every text the menu shows, then stops watching and checks that no further request is made. The presence
/// lease is off, so the probe is never counted as a player.
/// </summary>
internal static class DashboardCheck
{
    private const string DefaultMaster = "https://master.lumbridge.co.uk";
    private const string DefaultInvite = "b7cee2a094feca62d43bdac7f80be755a98c62a85c5977a0";
    private static readonly Stopwatch Clock = Stopwatch.StartNew();

    public static int Run(string[] args)
    {
        int failures = 0;
        void Check(bool ok, string what)
        {
            if (!ok)
            {
                failures++;
                Console.WriteLine("FAILED: " + what);
            }
        }

        // The record sizes Source/OpenSCCT/Runtime/ScctDashboardApi.h asserts for its mirrors.
        unsafe
        {
            Console.WriteLine($"dashboard table: {sizeof(DashboardApi)} bytes; info {sizeof(DashboardInfoBlock)} bytes");
            Check(sizeof(DashboardApi) == 8 + 5 * sizeof(nint) && sizeof(DashboardInfoBlock) == 48, "the layout does not match Runtime/ScctDashboardApi.h");
        }

        string notes = "## Highlights\n- **Rejoin** after a [crash](https://example.invalid).\n<b>bold</b> &amp; `code`\n---\n* second";
        string plain = MenuDashboard.PlainNotes(notes);
        Console.WriteLine("plain notes: " + plain.Replace("\n", " | "));
        Check(plain == "Highlights\n• Rejoin after a crash.\nbold & code\n• second", "PlainNotes does not match the Godot model");
        Check(MenuDashboard.Preview(notes) == "• Rejoin after a crash.\n• second", "Preview should keep the bullets only");

        string url = args.Length > 0 && args[0].Length > 0 ? args[0] : DefaultMaster;
        string invite = args.Length > 1 ? args[1] : DefaultInvite;
        OnlineBridge online = OnlineBridge.Instance;
        SocialBridge social = SocialBridge.Instance;
        social.Lease.Enabled = false;
        online.Master.Configure(url, invite);
        MenuDashboard dashboard = social.Dashboard;

        void Pump(double seconds)
        {
            var until = Clock.Elapsed + TimeSpan.FromSeconds(seconds);
            while (Clock.Elapsed < until)
            {
                online.Update();
                social.Update(0.05, SocialReadiness.None);
                while (social.TakeLog(out string line))
                {
                    if (line.StartsWith("Dashboard:", StringComparison.Ordinal) || line.StartsWith("Master version:", StringComparison.Ordinal))
                    {
                        Console.WriteLine($"  [{Clock.Elapsed.TotalSeconds,6:0.00}s] {line}");
                    }
                }

                Thread.Sleep(50);
            }
        }

        DashboardInfoBlock Info()
        {
            DashboardInfoBlock block = default;
            dashboard.Fill(ref block);
            return block;
        }

        Console.WriteLine($"master {url}");
        Pump(1);
        Check(Info().Requests == 0, "nothing may be asked before the dashboard is watched");
        dashboard.SetWatched(true);
        var settle = Clock.Elapsed + TimeSpan.FromSeconds(20);
        while (Clock.Elapsed < settle && (Info().Flags & (int)(DashboardFlags.StatusLoading | DashboardFlags.ChangelogLoading)) != 0 || Info().Requests < 4)
        {
            Pump(0.25);
            if (Clock.Elapsed >= settle)
            {
                break;
            }
        }

        DashboardInfoBlock info = Info();
        Console.WriteLine($"info: flags {(DashboardFlags)info.Flags}, players {info.PlayersOnline}, games {info.GameSessions}/{info.GamePlayers}, hubs {info.HubSessions}/{info.HubPlayers}, releases {info.Releases}, requests {info.Requests}");
        foreach (DashboardText kind in Enum.GetValues<DashboardText>())
        {
            Console.WriteLine($"  {kind,-20} {dashboard.Text(kind).Replace("\n", " | ")}");
        }

        for (int i = 0; dashboard.Release(i, out string tag, out string name, out string date, out string text, out string link); i++)
        {
            Console.WriteLine($"  release {i}: {tag} \"{name}\" {date} {link} ({text.Length} chars of notes)");
        }

        Check(info.Requests >= 4, "watching should ask for the status, both lists and the changelog");
        dashboard.SetWatched(false);
        Pump(0.5);
        int stopped = Info().Requests;
        Pump(3);
        Check(Info().Requests == stopped, "no request may start once the dashboard is no longer watched");
        Console.WriteLine(failures == 0 ? "dashboard: all checks passed" : $"dashboard: {failures} check(s) failed");
        return failures == 0 ? 0 : 1;
    }
}
