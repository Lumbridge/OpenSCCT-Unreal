using System;
using System.Diagnostics;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hud;

// OnlineProbe endround [content root] [map] [seconds]
//
// The round table's EndLocalRound on a local match, through the exported tables as Unreal calls them: a local match
// is started with the main table, EndLocalRound is asked for at once (it must wait for the match to begin, whose start
// resets the clock), and the HUD table must then report the round ended by time (Ended, a winner, no time left), as
// the game's -ScctEndMatch switch relies on to show the offline results.
internal static unsafe class EndRoundProbe
{
    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
        string map = args.Length > 1 ? args[1] : "AquaE";
        double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 60;

        BridgeApi bridge;
        HudApi hud;
        RoundApi round;
        ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&bridge, sizeof(BridgeApi));
        ((delegate* unmanaged<HudApi*, int>)&HudExports.GetApi)(&hud);
        ((delegate* unmanaged<RoundApi*, int>)&RoundExports.GetApi)(&round);
        Console.WriteLine($"EndLocalRound before any match -> {round.EndLocalRound()}");

        fixed (char* c = content, m = map, g = "Story")
        {
            if (bridge.Configure(c) == 0 || bridge.StartLocal(m, g, 0, 1) == 0)
            {
                Console.WriteLine("PROBE FAILED: the local match did not start");
                return 1;
            }
        }

        HudState state;
        hud.GetState(&state);
        int queued = round.EndLocalRound();
        Console.WriteLine($"EndLocalRound on the local match -> {queued}");
        var run = Stopwatch.StartNew();
        HudRound r = default;
        double next = 0;
        bool ended = false;
        while (run.Elapsed.TotalSeconds < seconds)
        {
            bridge.Update();
            hud.GetState(&state);
            hud.GetRound(&r);
            double t = run.Elapsed.TotalSeconds;
            if (t >= next)
            {
                next = t + 2;
                Console.WriteLine($"t={t:F1} round valid {r.Valid} time limit {r.TimeLimit} elapsed {r.ElapsedTime} left {r.RemainingSeconds} ended {r.Ended} winner {r.WinningTeam}");
            }

            if (r.Ended != 0)
            {
                ended = true;
                Console.WriteLine($"t={t:F1} the round ended: winner {r.WinningTeam}, time limit {r.TimeLimit} min, elapsed {r.ElapsedTime} s, left {r.RemainingSeconds} s, objectives left {r.ObjectivesLeft} of {r.NumObjectives}");
                break;
            }

            Thread.Sleep(5);
        }

        bridge.Stop();
        bool ok = queued == 1 && ended && r.WinningTeam is 0 or 1;
        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }
}
