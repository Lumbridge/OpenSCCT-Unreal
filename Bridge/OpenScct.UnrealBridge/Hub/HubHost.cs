using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using OpenScct.Core;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.Server.Hub;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// A hub hosted from the game (port of the Godot client's game/Scripts/Hub/HubHost.cs): the dedicated server's
/// <see cref="HubServer"/> on its own thread, on a UDP port of this PC, so a player without a hub server can open one
/// and others on the LAN, by address or from the master's hub list (with the master's NAT punch and relay) join it. The
/// host's own player joins it like anyone else, on 127.0.0.1. Stopping it takes it off the master's list (the hub
/// server unlists itself when disposed).
/// <para>Not ported: the host's account (HostAccount: his rank and looks in his own hub) and map downloads for the
/// pads' laptops.</para>
/// </summary>
public sealed class HubHost : IDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly ConcurrentQueue<Action<HubServer>> _work = new();
    private readonly Thread _thread;
    private bool _disposed;

    private HubHost(HubServer server, LiteNetLibTransport udp, string hostKey)
    {
        Server = server;
        Udp = udp;
        HostKey = hostKey;
        _thread = new Thread(Run) { IsBackground = true, Name = "OpenSCCT hub host" };
    }

    /// <summary>The hub; it belongs to the host thread.</summary>
    public HubServer Server { get; }

    public LiteNetLibTransport Udp { get; }

    public int Port => Udp.LocalPort;

    /// <summary>The key the host's own player logs in with (<c>?SvmHost=</c>).</summary>
    public string HostKey { get; }

    /// <summary>Where the host's own player joins.</summary>
    public string LocalAddress => "127.0.0.1:" + Port.ToString(System.Globalization.CultureInfo.InvariantCulture);

    /// <summary>The thread's failure, if it stopped.</summary>
    public Exception? Failure { get; private set; }

    /// <summary>
    /// Opens UDP <paramref name="port"/> (every interface, or the loopback addresses only) and the hub on
    /// <paramref name="maps"/>, and starts the host thread. <paramref name="log"/> receives the hub's log lines, on
    /// the host thread. Throws <see cref="InvalidOperationException"/> with a readable reason (the port is in use,
    /// the hub map is missing). With <paramref name="master"/> the hub lists itself in that master's hub list.
    /// </summary>
    public static HubHost Start(int port, string name, MapCatalog maps, NetClassRegistry classes, bool loopbackOnly, Action<string> log,
        OpenScct.Server.Online.MasterLinkSettings? master = null)
    {
        ArgumentNullException.ThrowIfNull(maps);
        ArgumentNullException.ThrowIfNull(log);
        LiteNetLibTransport udp;
        try
        {
            udp = LiteNetLibTransport.Listen(port, HubK.DefaultMaxPlayers + 8, loopbackOnly: loopbackOnly);
        }
        catch (Exception e) when (e is System.Net.Sockets.SocketException or InvalidOperationException)
        {
            throw new InvalidOperationException($"UDP port {port} could not be opened: {e.Message}", e);
        }

        try
        {
            string key = Convert.ToHexString(System.Security.Cryptography.RandomNumberGenerator.GetBytes(8)).ToLowerInvariant();
            HubServer server;
            try
            {
                var botNavigation = new OpenScct.Bots.AiNavCache();
                server = new HubServer(udp, new HubSettings
                {
                    Name = name,
                    Maps = maps,
                    Classes = classes,
                    MatchBots = OpenScct.Bots.OnlineServerBots.Factory(botNavigation),
                    PrepareMatchBots = OpenScct.Bots.OnlineServerBots.Preparation(botNavigation),
                    MatchClock = static () => new StopwatchClock(),
                    MatchLoopbackOnly = loopbackOnly,
                    Master = master,
                    HostKey = key,
                });
            }
            catch (System.IO.IOException e)
            {
                throw new InvalidOperationException("The hub map could not be loaded: " + e.Message, e);
            }

            server.Logged += log;
            log(server.PadsSummary);
            var host = new HubHost(server, udp, key);
            host._thread.Start();
            log($"hosting {name} on UDP port {udp.LocalPort}{(loopbackOnly ? " (this PC only)" : " (LAN and direct address)")}"
                + (master is null ? ", not listed on a master" : $", listing it in the hub list of {master.Url}"));
            return host;
        }
        catch
        {
            udp.Dispose();
            throw;
        }
    }

    private void Run()
    {
        try
        {
            Server.RunRealtime(new StopwatchClock(), _stop.Token, RunWork);
        }
        catch (Exception e) when (e is InvalidOperationException or ObjectDisposedException or ArgumentException or System.IO.IOException)
        {
            Failure = e;
        }
    }

    /// <summary>
    /// Runs <paramref name="work"/> on the host thread after its next tick (the hub belongs to that thread): what the
    /// game does to its own hub, such as placing the host's player for an unattended run.
    /// </summary>
    public void Post(Action<HubServer> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        _work.Enqueue(work);
    }

    private void RunWork(HubServer server)
    {
        while (_work.TryDequeue(out Action<HubServer>? work))
        {
            work(server);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _stop.Cancel();
        _thread.Join(TimeSpan.FromSeconds(3));
        Server.Dispose();
        Udp.Dispose();
        _stop.Dispose();
    }

    private sealed class StopwatchClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
