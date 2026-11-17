using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Test;

// TestProbe <content root> [map] [ticks]
// Goes through Exports.GetApiSized and TestExports.GetApi as Unreal does (unmanaged function pointers, raw buffers).
// The world check must step its ticks; the match (TestBox by default: the fixture the bridge ships in maps/) must
// show all four pawns on the client with replicated state, run its ticks, and once the server is stopped every server
// pawn must be on the client within 100 uu of where the server has it. Exit code 1 on a failure.
string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "TestBox";
int matchTicks = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 600;

// The sizes ScctTestApi.h asserts.
Console.WriteLine($"sizes: BridgeTestCounters {Unsafe.SizeOf<BridgeTestCounters>()}, BridgeTestPawn {Unsafe.SizeOf<BridgeTestPawn>()}, TestApi {Unsafe.SizeOf<TestApi>()}");
return Run(content, map, matchTicks);

static unsafe int Run(string content, string map, int matchTicks)
{
    const int Walkers = 4;
    const float FinalTolerance = 100f;

    BridgeApi bridgeTable;
    TestApi testTable;
    int bridgeVersion = ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&bridgeTable, sizeof(BridgeApi));
    int testVersion = ((delegate* unmanaged<TestApi*, int>)&TestExports.GetApi)(&testTable);
    BridgeApi bridge = bridgeTable;
    TestApi api = testTable;
    Console.WriteLine($"api: bridge table version {bridgeVersion}, test table version {testVersion} (table says {api.Version})");
    if (testVersion != TestExports.ApiVersion || api.Version != testVersion)
    {
        Console.WriteLine("FAILED: the test table did not fill");
        return 1;
    }

    char* text = stackalloc char[2048];
    string BridgeError()
    {
        int length = bridge.LastError(text, 2048);
        return new string(text, 0, Math.Min(length, 2047));
    }

    string TestError()
    {
        int length = api.LastError(text, 2048);
        return new string(text, 0, Math.Min(length, 2047));
    }

    int worldTicks = api.StepWorld(600);
    Console.WriteLine($"world: StepWorld(600) = {worldTicks}");
    if (worldTicks != 600)
    {
        Console.WriteLine("FAILED: " + TestError());
        return 1;
    }

    BridgeTestCounters counters;
    BridgeTestPawn* pawns = stackalloc BridgeTestPawn[32];
    if (api.GetCounters(&counters) != 0 || api.GetPawns(pawns, 32) != 0)
    {
        Console.WriteLine("FAILED: counters or pawns without a session");
        return 1;
    }

    fixed (char* c = content)
    {
        if (bridge.Configure(c) == 0)
        {
            Console.WriteLine("FAILED: " + BridgeError());
            return 1;
        }
    }

    BridgeStartOptions options = default;
    options.Size = sizeof(BridgeStartOptions);
    options.Bots = Walkers - 1;
    for (int i = 0; i < map.Length && i < BridgeStartOptions.MapCapacity - 1; i++)
    {
        options.Map[i] = map[i];
    }

    var load = Stopwatch.StartNew();
    if (bridge.StartLocalEx(&options) == 0)
    {
        Console.WriteLine("FAILED: " + BridgeError());
        return 1;
    }

    Console.WriteLine($"{map}: started in {load.Elapsed.TotalSeconds:F1} s");
    var run = Stopwatch.StartNew();
    long joinedAt = -1;
    double nextReport = 1;
    while (true)
    {
        bridge.Update();
        if (bridge.GetState() != (int)BridgeState.Playing)
        {
            Console.WriteLine("FAILED: " + BridgeError());
            return 1;
        }

        api.GetCounters(&counters);
        int count = api.GetPawns(pawns, 32);
        int replicated = 0;
        for (int i = 0; i < count; i++)
        {
            replicated += (pawns[i].Flags & BridgeTestPawnFlags.OnClient) != 0 ? 1 : 0;
        }

        if (joinedAt < 0 && replicated == Walkers)
        {
            joinedAt = counters.ClientTicks;
            Console.WriteLine($"   all {Walkers} pawns on the client at tick {joinedAt}");
        }

        if (run.Elapsed.TotalSeconds >= nextReport)
        {
            nextReport += 2;
            Console.WriteLine($"   t={run.Elapsed.TotalSeconds:F1} clientTicks={counters.ClientTicks} serverTicks~{counters.ServerTicks} pawns={replicated} state={counters.ClientState} corrections={counters.CorrectionsApplied}/{counters.CorrectionsReceived}");
        }

        if (counters.ClientTicks >= matchTicks)
        {
            break;
        }

        if (run.Elapsed.TotalSeconds > matchTicks / 20.0 + 30)
        {
            Console.WriteLine($"FAILED: only {counters.ClientTicks} client ticks in {run.Elapsed.TotalSeconds:F0} s");
            return 1;
        }

        Thread.Sleep(4);
    }

    if (joinedAt < 0)
    {
        Console.WriteLine($"FAILED: the client never had all {Walkers} pawns");
        return 1;
    }

    if (api.StopServer() != 1)
    {
        Console.WriteLine("FAILED: StopServer: " + TestError());
        return 1;
    }

    api.GetCounters(&counters);
    int final = api.GetPawns(pawns, 32);
    float maxError = 0f;
    int onServer = 0;
    for (int i = 0; i < final; i++)
    {
        BridgeTestPawn p = pawns[i];
        Console.WriteLine(FormattableString.Invariant(
            $"   pawn server {p.ServerActorId} client {p.ClientActorId} flags {p.Flags} moves {p.MovesApplied} server ({p.ServerX:F0},{p.ServerY:F0},{p.ServerZ:F0}) client ({p.ClientX:F0},{p.ClientY:F0},{p.ClientZ:F0})"));
        if ((p.Flags & BridgeTestPawnFlags.OnServer) == 0)
        {
            continue;
        }

        onServer++;
        if ((p.Flags & BridgeTestPawnFlags.OnClient) == 0)
        {
            Console.WriteLine($"FAILED: server pawn {p.ServerActorId} is not on the client");
            return 1;
        }

        float dx = p.ServerX - p.ClientX, dy = p.ServerY - p.ClientY, dz = p.ServerZ - p.ClientZ;
        maxError = Math.Max(maxError, MathF.Sqrt(dx * dx + dy * dy + dz * dz));
    }

    bridge.Stop();
    Console.WriteLine(FormattableString.Invariant(
        $"{map}: serverTicks={counters.ServerTicks} clientTicks={counters.ClientTicks} reinits={counters.Reinits} serverPawns={onServer} maxError={maxError:F2}uu"));
    if (onServer != Walkers || maxError > FinalTolerance || counters.ServerTicks < matchTicks * 95 / 100)
    {
        Console.WriteLine("FAILED: the server does not agree with the client");
        return 1;
    }

    Console.WriteLine("OK");
    return 0;
}
