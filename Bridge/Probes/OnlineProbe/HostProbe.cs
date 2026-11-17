using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading;
using OpenScct.Net;
using OpenScct.Net.Discovery;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;

/// <summary>
/// OnlineProbe host [content root] [map] [port] [lan port] [upnp]
///
/// Hosting from the game without Unreal: the host table (HostExports.GetApi) starts a listen server on a loopback
/// UDP port with a LAN beacon and joins its own player as the lobby's host, all through the unmanaged entries;
/// a second OnlineBridge joins it over UDP to 127.0.0.1 as another machine would. It checks, in this order:
///   1. the host table's size (ScctHostApi.h asserts it);
///   2. a host on a map that is not installed fails with a reason;
///   3. the host starts, its player reaches the lobby as host (player flag 2, CanControl), the guest joins and both
///      are listed in the host's lobby and in the host's own status text, the host first;
///   4. a LAN search on the beacon's port answers with the server's name and player count;
///   5. a host command ("players") answers as system chat lines;
///   6. the host leaves: hosting stops and the guest's visit fails with the server closing.
/// With "upnp" it also looks for the router with an SSDP search (nothing is mapped).
/// </summary>
internal static unsafe class HostProbe
{
    private static OnlineApi s_online;
    private static HostApi s_host;

    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
        string map = args.Length > 1 ? args[1] : "AquaE";
        int port = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28941;
        int lanPort = args.Length > 3 ? int.Parse(args[3], CultureInfo.InvariantCulture) : 28942;
        bool upnp = args.Contains("upnp");

        int expected = 8 + 8 * sizeof(void*);
        Console.WriteLine($"layout: HostApi {sizeof(HostApi)} bytes{(sizeof(HostApi) == expected ? string.Empty : $" BUT THE HEADER ASSERTS {expected}")}");
        if (sizeof(HostApi) != expected)
        {
            Console.WriteLine("PROBE FAILED");
            return 1;
        }

        OnlineApi online = default;
        HostApi host = default;
        int onlineVersion = ((delegate* unmanaged<OnlineApi*, int>)&OnlineExports.GetApi)(&online);
        int hostVersion = ((delegate* unmanaged<HostApi*, int>)&HostExports.GetApi)(&host);
        s_online = online;
        s_host = host;
        Console.WriteLine($"tables: online version {onlineVersion}, host version {hostVersion} (expects {HostExports.ApiVersion})");
        BridgeSession.Configure(content);
        fixed (char* name = "HostProbe")
        {
            s_online.SetIdentity(name, null, null);
        }

        if (upnp)
        {
            Console.WriteLine("upnp: " + UpnpPorts.FindRouter());
        }

        OnlineBridge? guest = null;
        bool ok = true;
        try
        {
            // 2. A map that is not here.
            int refused = Host("Nowhere game", "?MapName=NoSuchMap?Mode=Story", port, lanPort);
            OnlineStatus status = Frame();
            Console.WriteLine($"host on a missing map: returned {refused}, state {(OnlineState)status.State}: '{Text(OnlineText.Error)}', hosting {s_host.IsHosting()}");
            ok &= refused == 0 && status.State == (int)OnlineState.Failed && s_host.IsHosting() == 0;
            s_online.Leave();

            // 3. The host and its player.
            int started = Host("Probe host", $"?MapName={map}?Mode=Story?TimeLimit=5", port, lanPort);
            Console.WriteLine($"host: returned port {started}, hosting {s_host.IsHosting()}");
            ok &= started == port;
            status = WaitFor(s => s.State == (int)OnlineState.Lobby && s.LocalId >= 0 && s.PlayerCount >= 1, 15);
            Console.WriteLine($"host's player: state {(OnlineState)status.State}, local id {status.LocalId}, can control {(status.Flags & OnlineStatusFlags.CanControl) != 0}, server '{Text(OnlineText.ServerName)}', setup {Text(OnlineText.SetupDescription)}");
            ok &= status.State == (int)OnlineState.Lobby && (status.Flags & OnlineStatusFlags.CanControl) != 0;

            guest = new OnlineBridge();
            guest.SetIdentity("ProbeGuest", null, null);
            guest.Join("127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture), null, preferredTeam: 1, loopbackOnly: true);
            OnlineBridge g = guest;
            status = WaitFor(s => s.PlayerCount >= 2 && g.State == OnlineState.Lobby, 20, g);
            Console.WriteLine($"guest: state {g.State}{(g.Error is { } e ? ": " + e : string.Empty)}");
            var players = new OnlinePlayer[8];
            int count;
            fixed (OnlinePlayer* p = players)
            {
                count = s_online.GetPlayers(p, players.Length);
            }

            for (int i = 0; i < count; i++)
            {
                string pname;
                fixed (char* n = players[i].Name)
                {
                    pname = new string(n);
                }

                Console.WriteLine($"lobby player {i}: id {players[i].Id} '{pname}' team {players[i].Team} flags {players[i].Flags}{((players[i].Flags & 2) != 0 ? " HOST" : string.Empty)}");
            }

            ok &= count == 2 && (players[0].Flags & 2) != 0 && (players[1].Flags & 2) == 0;
            // The status text the host thread writes about once a second.
            Thread.Sleep(1200);
            Frame(g);
            Console.WriteLine($"host status: {HostText(OpenScct.UnrealBridge.HostText.Status)}");
            Console.WriteLine($"host players: {HostText(OpenScct.UnrealBridge.HostText.Players)}");
            Console.WriteLine($"host master: {HostText(OpenScct.UnrealBridge.HostText.Master)}");
            Console.WriteLine($"host upnp: {HostText(OpenScct.UnrealBridge.HostText.Upnp)}");
            ok &= HostText(OpenScct.UnrealBridge.HostText.Players).Contains("ProbeGuest", StringComparison.Ordinal);

            // 4. LAN discovery.
            ok &= Lan(lanPort, g);

            // 5. A host command.
            fixed (char* c = "players")
            {
                ok &= s_host.HostCommand(c) == 1;
            }

            int replies = 0;
            var wait = Stopwatch.StartNew();
            while (wait.Elapsed.TotalSeconds < 3 && replies < 2)
            {
                Frame(g);
                OnlineChatLine line;
                while (s_online.TakeChat(&line) == 1)
                {
                    string text = new string(line.Text);
                    Console.WriteLine($"chat{(line.System != 0 ? " [system]" : string.Empty)}: {text}");
                    // The "players" answer lists each player on a line of its own (the join notices name them too).
                    replies += line.System != 0 && !text.Contains("joined", StringComparison.Ordinal) && (text.Contains("HostProbe", StringComparison.Ordinal) || text.Contains("ProbeGuest", StringComparison.Ordinal)) ? 1 : 0;
                }

                Thread.Sleep(10);
            }

            ok &= replies >= 2;

            // The host's log, as Unreal drains it.
            char* buffer = stackalloc char[512];
            int length;
            while ((length = s_host.TakeHostLine(buffer, 512)) >= 0)
            {
                Console.WriteLine("host log: " + new string(buffer));
            }

            // 6. The host leaves.
            s_online.Leave();
            int hostingAfter = s_host.IsHosting();
            wait.Restart();
            while (wait.Elapsed.TotalSeconds < 8 && g.State is not (OnlineState.Failed or OnlineState.Left or OnlineState.Idle))
            {
                g.Update();
                Thread.Sleep(10);
            }

            Console.WriteLine($"after the host left: hosting {hostingAfter}, host state {(OnlineState)Frame().State}, guest {g.State}: '{g.Error}'");
            ok &= hostingAfter == 0 && g.State == OnlineState.Failed;
        }
        finally
        {
            guest?.Dispose();
            OnlineBridge.Instance.Dispose();
        }

        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    private static int Host(string name, string options, int port, int lanPort)
    {
        fixed (char* n = name, o = options)
        {
            return s_host.Host(n, null, port, 8, o, HostFlags.Lan | HostFlags.LoopbackOnly, lanPort, null);
        }
    }

    private static bool Lan(int lanPort, OnlineBridge guest)
    {
        using var query = new ServerQuery(new ProbeClock(), loopbackOnly: true);
        query.SearchLan(lanPort);
        var wait = Stopwatch.StartNew();
        while (wait.Elapsed.TotalSeconds < 4 && query.Results.Count == 0)
        {
            query.Poll();
            Frame(guest);
            Thread.Sleep(10);
        }

        foreach (ServerQueryResult r in query.Results)
        {
            Console.WriteLine($"LAN answer from {r.From}: '{r.Info.Name}' game port {r.GameEndPoint.Port}, {r.Info.Players}/{r.Info.MaxPlayers} players, map {r.Info.Map}, ping {r.PingMs} ms");
        }

        // The server tags its name with the host's install id ("Probe host #2b27").
        return query.Results.Any(r => r.Info.Name.StartsWith("Probe host", StringComparison.Ordinal) && r.Info.Players == 2);
    }

    private static OnlineStatus Frame(OnlineBridge? guest = null)
    {
        s_online.Update();
        guest?.Update();
        OnlineStatus status;
        s_online.GetStatus(&status);
        return status;
    }

    private static OnlineStatus WaitFor(Func<OnlineStatus, bool> done, double seconds, OnlineBridge? guest = null)
    {
        var wait = Stopwatch.StartNew();
        OnlineStatus status = Frame(guest);
        while (!done(status) && wait.Elapsed.TotalSeconds < seconds)
        {
            Thread.Sleep(10);
            status = Frame(guest);
        }

        return status;
    }

    private static string Text(OnlineText kind)
    {
        char* buffer = stackalloc char[1024];
        s_online.GetText((int)kind, buffer, 1024);
        return new string(buffer);
    }

    private static string HostText(HostText kind)
    {
        char* buffer = stackalloc char[1024];
        s_host.GetHostText((int)kind, buffer, 1024);
        return new string(buffer);
    }

    private sealed class ProbeClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
