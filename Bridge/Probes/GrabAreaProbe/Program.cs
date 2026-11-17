using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Test;

// GrabAreaProbe <content root> [map] [seconds]
// Goes through Exports.GetApiSized and GrabAreaExports.GetApi as Unreal does (unmanaged function pointers, raw
// buffers). A practice match must report at least one mercenary with a sane grab window; a normal match must report
// that it is not a practice round and give nothing. Exit code 1 on a failure.
string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], CultureInfo.InvariantCulture) : 8;

// The sizes ScctGrabAreaApi.h asserts.
Console.WriteLine($"sizes: BridgeGrabArea {Unsafe.SizeOf<BridgeGrabArea>()}, GrabAreaApi {Unsafe.SizeOf<GrabAreaApi>()}");
return Run(content, map, seconds);

static unsafe int Run(string content, string map, double seconds)
{
    BridgeApi bridgeTable;
    GrabAreaApi grabTable;
    int bridgeVersion = ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&bridgeTable, sizeof(BridgeApi));
    int grabVersion = ((delegate* unmanaged<GrabAreaApi*, int>)&GrabAreaExports.GetApi)(&grabTable);
    BridgeApi bridge = bridgeTable;
    GrabAreaApi api = grabTable;
    Console.WriteLine($"api: bridge table version {bridgeVersion}, grab-area table version {grabVersion} (table says {api.Version})");
    if (grabVersion != GrabAreaExports.ApiVersion || api.Version != grabVersion)
    {
        Console.WriteLine("FAILED: the grab-area table did not fill");
        return 1;
    }

    char* text = stackalloc char[1024];
    string Error()
    {
        int length = bridge.LastError(text, 1024);
        return new string(text, 0, Math.Min(length, 1023));
    }

    BridgeGrabArea* areas = stackalloc BridgeGrabArea[32];
    int idle = api.GetGrabAreas(areas, 32);
    Console.WriteLine($"api: no session: IsPracticeRound {api.IsPracticeRound()}, GetGrabAreas {idle}, GetGrabAreas(null) {api.GetGrabAreas(null, 32)}");
    if (idle != 0)
    {
        Console.WriteLine("FAILED: grab areas without a session");
        return 1;
    }

    fixed (char* c = content)
    {
        if (bridge.Configure(c) == 0)
        {
            Console.WriteLine("FAILED: " + Error());
            return 1;
        }
    }

    // A practice round: the spy against three bots.
    if (!Start(bridge, map, practice: true))
    {
        Console.WriteLine("FAILED: " + Error());
        return 1;
    }

    // The client world may still be joining when the start returns: the practice flag is asked once it plays.
    var join = Stopwatch.StartNew();
    int practice = 0;
    while (join.Elapsed.TotalSeconds < 10 && (practice = api.IsPracticeRound()) == 0)
    {
        bridge.Update();
        Thread.Sleep(4);
    }

    Console.WriteLine($"practice {map}: IsPracticeRound {practice} after {join.Elapsed.TotalSeconds:F1} s");
    if (practice != 1)
    {
        Console.WriteLine("FAILED: a ?Practice=1 match is not a practice round (PracticeRules off)");
        return 1;
    }

    var run = Stopwatch.StartNew();
    int best = 0;
    double nextReport = 1;
    bool sane = false;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        bridge.Update();
        if (bridge.GetState() != (int)BridgeState.Playing)
        {
            Console.WriteLine("FAILED: " + Error());
            return 1;
        }

        int count = api.GetGrabAreas(areas, 32);
        best = Math.Max(best, count);
        for (int i = 0; i < count; i++)
        {
            BridgeGrabArea a = areas[i];
            bool ok = a.Radius is > 50f and < 1000f && a.HalfAngle is > 0f and <= 180f &&
                a.TurnedHalfAngle >= a.HalfAngle && a.TurnedHalfAngle <= 180.001f &&
                a.State is >= GrabAreaState.GrabNow and <= GrabAreaState.Refused && a.ActorId != 0 &&
                float.IsFinite(a.X) && float.IsFinite(a.Y) && float.IsFinite(a.Z);
            if (!ok)
            {
                Console.WriteLine($"FAILED: insane entry {Describe(a)}");
                return 1;
            }

            sane = true;
        }

        if (run.Elapsed.TotalSeconds >= nextReport)
        {
            nextReport += 2;
            Console.WriteLine($"   t={run.Elapsed.TotalSeconds:F1} {count} mercs");
            for (int i = 0; i < count; i++)
            {
                Console.WriteLine("      " + Describe(areas[i]));
            }
        }

        Thread.Sleep(4);
    }

    if (!sane || best < 1)
    {
        Console.WriteLine($"FAILED: no mercenary entry in {seconds:F0} s of a practice match");
        return 1;
    }

    // A normal match: the table refuses, as the overlay must.
    if (!Start(bridge, map, practice: false))
    {
        Console.WriteLine("FAILED: " + Error());
        return 1;
    }

    var normal = Stopwatch.StartNew();
    int leaked = 0;
    while (normal.Elapsed.TotalSeconds < 3)
    {
        bridge.Update();
        leaked = Math.Max(leaked, api.GetGrabAreas(areas, 32));
        Thread.Sleep(4);
    }

    int normalPractice = api.IsPracticeRound();
    Console.WriteLine($"normal {map}: IsPracticeRound {normalPractice}, most entries {leaked}");
    bridge.Stop();
    if (normalPractice != 0 || leaked != 0)
    {
        Console.WriteLine("FAILED: a normal match reports grab areas");
        return 1;
    }

    Console.WriteLine($"OK: practice match gave up to {best} mercenary grab windows; a normal match gave none");
    return 0;
}

static unsafe bool Start(BridgeApi bridge, string map, bool practice)
{
    BridgeStartOptions options = default;
    options.Size = sizeof(BridgeStartOptions);
    options.Merc = 0;
    options.Bots = 3;
    options.Flags = practice ? BridgeStartFlags.OfflineRules : 0;
    for (int i = 0; i < map.Length && i < BridgeStartOptions.MapCapacity - 1; i++)
    {
        options.Map[i] = map[i];
    }

    return bridge.StartLocalEx(&options) != 0;
}

static string Describe(BridgeGrabArea a)
{
    string state = a.State switch { GrabAreaState.GrabNow => "grab now", GrabAreaState.OutOfPosition => "out of position", _ => "refused" };
    return FormattableString.Invariant(
        $"merc {a.ActorId} at ({a.X:F0}, {a.Y:F0}, {a.Z:F0}) facing {a.FacingYaw:F0}, window {a.SectorYaw:F0} +-{a.HalfAngle:F1} (turned +-{a.TurnedHalfAngle:F1}) radius {a.Radius:F0}: {state}");
}
