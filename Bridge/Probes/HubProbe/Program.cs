using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hub;

// HubProbe <content root> <OpenSCCT checkout> [port] [seconds in the hub]
//
// Visits a hub the way Unreal does, through the function table HubExports.GetApi fills (the same unmanaged entries,
// called through their pointers), once a frame at about 60 Hz:
//   1. the table's version and the struct size the C++ header asserts;
//   2. a join where no hub listens ends as Failed with a reason, and Leave clears it;
//   3. hosting a hub on this PC (loopback only) and joining it: Connecting -> Loading -> InHub, the visit's session
//      in the main slot, ReportLoaded, the local pawn spawned by the hub and standing on the floor;
//   4. Leave: the hub is closed, the slot is empty and the state is Idle.
// The OpenSCCT checkout's fixtures folder is given as the hub's map root (Unreal gives the project's
// Saved/ScctImport/HubContent/svmap, which holds the same map).
return Probe.Run(args);

internal static unsafe class Probe
{
    private static HubApi s_api;
    private static readonly Stopwatch s_run = Stopwatch.StartNew();

    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
        string openScct = args.Length > 1 ? args[1] : @"C:\Users\ryans\source\repos\OSC-unreal-bridge";
        int port = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28936;
        double inHubSeconds = args.Length > 3 ? double.Parse(args[3], CultureInfo.InvariantCulture) : 6;

        HubApi api = default;
        delegate* unmanaged<HubApi*, int> getApi = &HubExports.GetApi;
        int version = getApi(&api);
        s_api = api;
        Console.WriteLine($"table: version {version} (header expects {HubBridge.ApiVersion}), {sizeof(HubApi)} bytes, status {sizeof(HubStatus)} bytes");
        if (sizeof(HubStatus) != 40 || sizeof(HubApi) != 8 + 9 * sizeof(nint))
        {
            Console.WriteLine("PROBE FAILED: the layout does not match Runtime/ScctHubApi.h");
            return 1;
        }

        BridgeSession.Configure(content);
        fixed (char* root = Path.Combine(openScct, "fixtures"))
        {
            s_api.AddMapRoot(root);
        }

        bool ok = false;
        try
        {
            ok = NobodyThere(port + 1) && HostAndVisit(port, inHubSeconds);
        }
        finally
        {
            s_api.Leave();
            Drain();
        }

        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    /// <summary>A join where nothing listens gives up with a reason; Leave clears it.</summary>
    private static bool NobodyThere(int port)
    {
        Console.WriteLine($"-- joining 127.0.0.1:{port}, where no hub listens");
        fixed (char* address = "127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture), name = "HubProbe")
        {
            if (s_api.Connect(address, name, 0) != 1)
            {
                Console.WriteLine("FAIL: the join did not start");
                return false;
            }
        }

        HubState state = Run(TimeSpan.FromSeconds(30), s => s.State == (int)HubState.Failed);
        string error = Text(HubText.Error);
        Console.WriteLine($"state {state}, error '{error}'");
        s_api.Leave();
        HubStatus after;
        s_api.GetStatus(&after);
        Console.WriteLine($"after Leave: state {(HubState)after.State}");
        return state == HubState.Failed && error.Length > 0 && after.State == (int)HubState.Idle;
    }

    private static bool HostAndVisit(int port, double inHubSeconds)
    {
        Console.WriteLine($"-- hosting a hub on UDP port {port} (loopback only) and joining it");
        fixed (char* name = "HubProbe")
        {
            if (s_api.HostAndJoin(port, name, 1, 1) != 1)
            {
                Drain();
                Console.WriteLine($"FAIL: could not host: {Text(HubText.Error)}");
                return false;
            }
        }

        HubState state = Run(TimeSpan.FromSeconds(60), s => s.State is (int)HubState.InHub or (int)HubState.Failed);
        if (state != HubState.InHub)
        {
            Console.WriteLine($"FAIL: the visit ended {state}: {Text(HubText.Error)}");
            return false;
        }

        Console.WriteLine($"in the hub: '{Text(HubText.HubName)}', map to load '{Text(HubText.MapToLoad)}', status '{Text(HubText.Status)}'");
        BridgeSession? slot = Exports.Current;
        Console.WriteLine($"main slot: {(slot is null ? "empty" : $"session {slot.Id}, remote {slot.Session.IsRemote}")}");
        if (slot is null || !ReferenceEquals(slot.Session, HubBridge.Instance.Round))
        {
            Console.WriteLine("FAIL: the visit's session is not in the main slot");
            return false;
        }

        // Unreal calls this once its hub level is up and presents the session.
        s_api.ReportLoaded();
        Run(TimeSpan.FromSeconds(10), s => (s.Flags & HubStatusFlags.LocalPawn) != 0);
        double until = s_run.Elapsed.TotalSeconds + inHubSeconds;
        Run(TimeSpan.FromSeconds(inHubSeconds), _ => false);
        HubStatus status;
        s_api.GetStatus(&status);
        var pawn = HubBridge.Instance.Round?.LocalPawn;
        Console.WriteLine($"after {inHubSeconds:0} s in the hub: flags 0x{status.Flags:x}, player {status.PlayerId} team {status.Team}, {status.PlayerCount} on the roster, " +
            (pawn is null ? "no local pawn" : $"local pawn {pawn.GetType().Name} at ({pawn.Location.X:0}, {pawn.Location.Y:0}, {pawn.Location.Z:0}), physics {pawn.Physics}"));
        if ((status.Flags & HubStatusFlags.ReportedWaiting) == 0 || pawn is null)
        {
            Console.WriteLine("FAIL: the hub did not place the player");
            return false;
        }

        s_api.Leave();
        Drain();
        s_api.GetStatus(&status);
        Console.WriteLine($"after Leave: state {(HubState)status.State}, flags 0x{status.Flags:x}, main slot {(Exports.Current is null ? "empty" : "still taken")}");
        return status.State == (int)HubState.Idle && Exports.Current is null;
    }

    /// <summary>Runs frames at about 60 Hz until <paramref name="done"/> or the time is up; prints the log and state changes.</summary>
    private static HubState Run(TimeSpan limit, Func<HubStatus, bool> done)
    {
        var watch = Stopwatch.StartNew();
        HubStatus status;
        while (true)
        {
            s_api.Update();
            s_api.GetStatus(&status);
            Drain();
            if (done(status) || watch.Elapsed > limit)
            {
                return (HubState)status.State;
            }

            Thread.Sleep(16);
        }
    }

    private static void Drain()
    {
        char* buffer = stackalloc char[1024];
        while (s_api.TakeLog(buffer, 1024) >= 0)
        {
            Console.WriteLine($"[{s_run.Elapsed.TotalSeconds,6:0.00}] ScctHub: {new string(buffer)}");
        }
    }

    private static string Text(HubText kind)
    {
        char* buffer = stackalloc char[1024];
        s_api.GetText((int)kind, buffer, 1024);
        return new string(buffer);
    }
}
