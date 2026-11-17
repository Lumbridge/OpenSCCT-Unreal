using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Test;

// NetProbe <content root> [profile=100] [listen|dedicated] [walk|grab|coop] [seconds=30] [stock]
// Goes through Exports.GetApiSized and TestExports.GetApi as Unreal does. Arms the link, starts SteeE (three server
// bots for a listen match; the dedicated server brings its own), runs it and prints the netcode figures every 2 s.
// Fails when the link was not applied, the measured round trip is far from the profile's, a bot resynchronised, or a
// scenario failed or did not finish.
string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string profile = args.Length > 1 ? args[1] : "100";
bool dedicated = args.Length > 2 && args[2] == "dedicated";
string scenario = args.Length > 3 ? args[3] : "walk";
double seconds = args.Length > 4 ? double.Parse(args[4], CultureInfo.InvariantCulture) : 30;
bool stock = args.Length > 5 && args[5] == "stock";

Console.WriteLine($"sizes: BridgeNetStats {Unsafe.SizeOf<BridgeNetStats>()}, BridgeNetPawn {Unsafe.SizeOf<BridgeNetPawn>()}, TestApi {Unsafe.SizeOf<TestApi>()}");
return Run(content, profile, dedicated, scenario, seconds, stock);

static unsafe int Run(string content, string profile, bool dedicated, string scenario, double seconds, bool stock)
{
    BridgeApi bridgeTable;
    TestApi testTable;
    ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&bridgeTable, sizeof(BridgeApi));
    int testVersion = ((delegate* unmanaged<TestApi*, int>)&TestExports.GetApi)(&testTable);
    BridgeApi bridge = bridgeTable;
    TestApi api = testTable;
    if (testVersion != TestExports.ApiVersion)
    {
        Console.WriteLine("FAILED: the test table did not fill");
        return 1;
    }

    char* text = stackalloc char[2048];
    string BridgeError() => new string(text, 0, Math.Min(bridge.LastError(text, 2048), 2047));
    string TestError() => new string(text, 0, Math.Min(api.LastError(text, 2048), 2047));
    string Detail() => new string(text, 0, Math.Min(api.GetNetDetail(text, 2048), 2047));

    fixed (char* c = content)
    {
        if (bridge.Configure(c) == 0)
        {
            Console.WriteLine("FAILED: " + BridgeError());
            return 1;
        }
    }

    fixed (char* p = profile)
    fixed (char* s = scenario)
    {
        if (api.SetNextMatchLink(p, stock ? 1 : 0, dedicated ? 1 : 0, s) != 1)
        {
            Console.WriteLine("FAILED: SetNextMatchLink: " + TestError());
            return 1;
        }
    }

    BridgeStartOptions options = default;
    options.Size = sizeof(BridgeStartOptions);
    options.Bots = 3;
    options.Merc = scenario == "coop" ? 1 : 0;
    const string map = "SteeE";
    for (int i = 0; i < map.Length; i++)
    {
        options.Map[i] = map[i];
    }

    var load = Stopwatch.StartNew();
    if (bridge.StartLocalEx(&options) == 0)
    {
        Console.WriteLine("FAILED: " + BridgeError());
        return 1;
    }

    Console.WriteLine($"{map} {(dedicated ? "dedicated" : "listen")} over link {profile}{(stock ? " (stock)" : "")}, {scenario}: started in {load.Elapsed.TotalSeconds:F1} s");
    BridgeNetStats stats;
    BridgeNetPawn* pawns = stackalloc BridgeNetPawn[32];
    var run = Stopwatch.StartNew();
    double nextReport = 2;
    while (run.Elapsed.TotalSeconds < seconds + 60)
    {
        bridge.Update();
        if (bridge.GetState() != (int)BridgeState.Playing)
        {
            Console.WriteLine("FAILED: " + BridgeError());
            return 1;
        }

        if (api.GetNetStats(&stats) == 0 || (stats.Flags & BridgeNetFlags.Linked) == 0)
        {
            Console.WriteLine("FAILED: the match is not linked: " + TestError());
            return 1;
        }

        bool report = run.Elapsed.TotalSeconds >= nextReport;
        if (report)
        {
            nextReport += 2;
            int count = api.GetNetPawns(pawns, 32);
            string line = $"   t={run.Elapsed.TotalSeconds:F0}s ticks={stats.ClientTicks}/{stats.ServerTicks} joined@{stats.JoinedTick} rtt={stats.RttMs:F0}ms (min {stats.MinRttMs:F0}) " +
                $"corrections={stats.CorrectionsApplied}/{stats.CorrectionsReceived} reinits={stats.ServerReinits} resets={stats.ClientResets} " +
                $"link sent={stats.LinkSent} lost={stats.LinkLost} bots={stats.BotsJoined}/{stats.BotProblems} stage={stats.Stage} {Detail()}";
            if (Exports.Current is { } current)
            {
                var play = current.Session;
                line += $"\n      client {play.Client.State} {play.Client.RejectReason} log: {string.Join(" | ", play.Log)}";
            }

            for (int i = 0; i < count; i++)
            {
                line += $"\n      s{pawns[i].ServerActorId}/c{pawns[i].ClientActorId} f{pawns[i].Flags} resets={pawns[i].Resets} drift={pawns[i].Drift:F1}";
            }

            Console.WriteLine(line);
        }

        bool scripted = scenario != "walk";
        if ((stats.Flags & BridgeNetFlags.ScenarioFailed) != 0)
        {
            Console.WriteLine("FAILED: " + Detail());
            return 1;
        }

        if (scripted ? (stats.Flags & BridgeNetFlags.ScenarioDone) != 0 : run.Elapsed.TotalSeconds >= seconds)
        {
            float expected = stats.LatencyMs;
            Console.WriteLine($"done after {run.Elapsed.TotalSeconds:F1} s: rtt {stats.RttMs:F0} ms for a {expected} ms link, corrections {stats.CorrectionsApplied}, resets {stats.ClientResets}, {Detail()}");
            if (stats.RttMs < expected * 0.7f || stats.RttMs > expected * 1.6f + 40f)
            {
                Console.WriteLine("FAILED: the measured round trip does not match the link");
                return 1;
            }

            if (stats.BotProblems != 0)
            {
                Console.WriteLine("FAILED: a bot resynchronised or left");
                return 1;
            }

            Console.WriteLine("OK");
            return 0;
        }

        Thread.Sleep(2);
    }

    Console.WriteLine("FAILED: timed out: " + Detail());
    return 1;
}
