using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hub;

// HubProbe <content root> <OpenSCCT checkout> [port] [seconds in the hub] [master url] [invite key] [host [hold seconds] | join [hub name]]
//
// Visits a hub the way Unreal does, through the function table HubExports.GetApi fills (the same unmanaged entries,
// called through their pointers), once a frame at about 60 Hz:
//   1. the table's version and the struct size the C++ header asserts;
//   2. a join where no hub listens ends as Failed with a reason, and Leave clears it;
//   3. hosting a hub on this PC (loopback only) and joining it: Connecting -> Loading -> InHub, the visit's session
//      in the main slot, ReportLoaded, the local pawn spawned by the hub and standing on the floor;
//   4. Leave: the hub is closed, the slot is empty and the state is Idle;
//   5. with a master url: the master's hub list; a hub hosted on every interface and listed there (its listing line
//      in the log), the list asked until it shows that hub, held open for the hold seconds, and off the list after
//      Leave. With "join", only this: the hub of that name (default "HubProbe's hub", which a second probe hosts)
//      is joined through the master's join answer (its ways in tried in turn) and the player stands in it.
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
        string? masterUrl = args.Length > 4 ? args[4] : null;
        string? inviteKey = args.Length > 5 ? args[5] : null;
        string mode = args.Length > 6 ? args[6] : "host";
        string? joinName = args.Length > 7 ? args[7] : null;
        double holdSeconds = mode == "host" && args.Length > 7 ? double.Parse(args[7], CultureInfo.InvariantCulture) : 0;

        HubApi api = default;
        delegate* unmanaged<HubApi*, int> getApi = &HubExports.GetApi;
        int version = getApi(&api);
        s_api = api;
        Console.WriteLine($"table: version {version} (header expects {HubBridge.ApiVersion}), {sizeof(HubApi)} bytes, status {sizeof(HubStatus)} bytes, list entry {sizeof(HubListEntry)} bytes");
        if (sizeof(HubStatus) != 40 || sizeof(HubApi) != 8 + 15 * sizeof(nint) || sizeof(HubListEntry) != 368)
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
            ok = mode == "join" && masterUrl is not null
                ? ListedJoin(masterUrl, inviteKey, joinName ?? HostedName, inHubSeconds)
                : NobodyThere(port + 1) && HostAndVisit(port, inHubSeconds) && (masterUrl is null || ListedHost(port, masterUrl, inviteKey, holdSeconds));
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

    /// <summary>
    /// The master's hub list; a hub hosted on every interface and listed there, seen in the list, kept open for
    /// <paramref name="holdSeconds"/> (another probe can join it through the list then), and gone from the list after
    /// Leave.
    /// </summary>
    private static bool ListedHost(int port, string masterUrl, string? inviteKey, double holdSeconds)
    {
        Console.WriteLine($"-- the hub list of {masterUrl}");
        fixed (char* url = masterUrl, key = inviteKey ?? string.Empty)
        {
            s_api.ConfigureMaster(url, key, null);
        }

        if (!AskList(out _))
        {
            return false;
        }

        Console.WriteLine($"-- hosting a hub on UDP port {port} on every interface, listed on the master");
        s_api.SetHostListed(1);
        fixed (char* name = "HubProbe")
        {
            if (s_api.HostAndJoin(port, name, 0, 0) != 1)
            {
                Drain();
                Console.WriteLine($"FAIL: could not host: {Text(HubText.Error)}");
                return false;
            }
        }

        HubState state = Run(TimeSpan.FromSeconds(60), s => s.State is (int)HubState.InHub or (int)HubState.Failed);
        HubStatus status;
        s_api.GetStatus(&status);
        Console.WriteLine($"host: state {state}, flags 0x{status.Flags:x} (listed {(status.Flags & HubStatusFlags.HostListed) != 0})");
        if (state != HubState.InHub || (status.Flags & HubStatusFlags.HostListed) == 0)
        {
            Console.WriteLine($"FAIL: the hosted hub is not in or not listed: {Text(HubText.Error)}");
            return false;
        }

        // The hub lists itself on its first heartbeat; the list is asked until it shows it.
        s_api.ReportLoaded();
        int index = -1;
        for (int attempt = 0; attempt < 8 && index < 0; attempt++)
        {
            Run(TimeSpan.FromSeconds(4), _ => false);
            if (!AskList(out _))
            {
                return false;
            }

            index = FindHub(HostedName);
        }

        if (index < 0)
        {
            Console.WriteLine("FAIL: the hosted hub never showed in the master's hub list");
            return false;
        }

        Console.WriteLine($"the hosted hub is listed at index {index}; holding it open {holdSeconds:0} s");
        Run(TimeSpan.FromSeconds(holdSeconds), s => s.State == (int)HubState.Failed);
        s_api.GetStatus(&status);
        Console.WriteLine($"after holding: state {(HubState)status.State}, {status.PlayerCount} on the roster");
        s_api.Leave();
        Drain();
        AskList(out _);
        bool gone = FindHub(HostedName) < 0;
        Console.WriteLine(gone ? "after Leave the hub is off the list" : "FAIL: the hub is still listed after Leave");
        return status.State == (int)HubState.InHub && gone;
    }

    /// <summary>Joins the listed hub named <paramref name="hubName"/> through the master's join answer, into the hub.</summary>
    private static bool ListedJoin(string masterUrl, string? inviteKey, string hubName, double inHubSeconds)
    {
        fixed (char* url = masterUrl, key = inviteKey ?? string.Empty)
        {
            s_api.ConfigureMaster(url, key, null);
        }

        int index = -1;
        for (int attempt = 0; attempt < 8 && index < 0; attempt++)
        {
            if (!AskList(out _))
            {
                return false;
            }

            index = FindHub(hubName);
            if (index < 0)
            {
                Run(TimeSpan.FromSeconds(4), _ => false);
            }
        }

        if (index < 0)
        {
            Console.WriteLine($"FAIL: '{hubName}' is not in the hub list");
            return false;
        }

        Console.WriteLine($"-- joining '{hubName}' (index {index}) through the master");
        fixed (char* name = "HubProbeGuest")
        {
            if (s_api.JoinListed(index, name, 1) != 1)
            {
                Console.WriteLine($"FAIL: the listed join did not start: {Text(HubText.Error)}");
                return false;
            }
        }

        HubStatus status;
        s_api.GetStatus(&status);
        Console.WriteLine($"asking the master: state {(HubState)status.State}, flags 0x{status.Flags:x}, status '{Text(HubText.Status)}'");
        HubState state = Run(TimeSpan.FromSeconds(90), s => s.State is (int)HubState.InHub or (int)HubState.Failed);
        s_api.GetStatus(&status);
        Console.WriteLine($"listed join: {state}, flags 0x{status.Flags:x}, address '{Text(HubText.Address)}', hub '{Text(HubText.HubName)}' {Text(HubText.Error)}");
        if (state != HubState.InHub || (status.Flags & HubStatusFlags.ListedHub) == 0)
        {
            return false;
        }

        s_api.ReportLoaded();
        Run(TimeSpan.FromSeconds(inHubSeconds), _ => false);
        s_api.GetStatus(&status);
        Console.WriteLine($"in the listed hub: flags 0x{status.Flags:x}, player {status.PlayerId}, {status.PlayerCount} on the roster");
        bool placed = (status.Flags & HubStatusFlags.LocalPawn) != 0;
        s_api.Leave();
        Drain();
        return placed;
    }

    private const string HostedName = "HubProbe's hub";
    private static bool AskList(out int count)
    {
        s_api.RequestList();
        Run(TimeSpan.FromSeconds(20), _ => s_api.GetListState() is (int)HubListState.Ready or (int)HubListState.Failed);
        int listState = s_api.GetListState();
        count = s_api.GetList(null, 0);
        Console.WriteLine($"list: {(HubListState)listState}, {count} hub(s), '{Text(HubText.ListMessage)}'");
        HubListEntry* entries = stackalloc HubListEntry[Math.Max(count, 1)];
        s_api.GetList(entries, count);
        for (int i = 0; i < count; i++)
        {
            Console.WriteLine($"  {i}: {new string(entries[i].Name)} region '{new string(entries[i].Region)}' {entries[i].Players}/{entries[i].MaxPlayers} ping {entries[i].PingMs} flags 0x{entries[i].Flags:x} {new string(entries[i].Problem)}");
        }

        if (listState != (int)HubListState.Ready)
        {
            Console.WriteLine("FAIL: the master's hub list did not come");
            return false;
        }

        return true;
    }

    private static int FindHub(string name)
    {
        int count = s_api.GetList(null, 0);
        HubListEntry* entries = stackalloc HubListEntry[Math.Max(count, 1)];
        s_api.GetList(entries, count);
        for (int i = 0; i < count; i++)
        {
            if (new string(entries[i].Name) == name)
            {
                return i;
            }
        }

        return -1;
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
