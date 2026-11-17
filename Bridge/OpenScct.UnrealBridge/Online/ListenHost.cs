using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.Linq;
using System.Threading;
using OpenScct.Net;
using OpenScct.Net.Discovery;
using OpenScct.Server;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// What a player hosts: the server's name, the first match and the hosting choices (the Godot client's
/// game/Scripts/Online/ListenHost.cs HostRequest).
/// </summary>
public sealed record HostRequest
{
    public string Name { get; init; } = "OpenSCCT game";

    public LobbySetup Setup { get; init; } = new();

    public int Port { get; init; } = BuildInfo.DefaultGamePort;

    public int MaxPlayers { get; init; } = LobbySetup.DefaultMaxPlayers;

    public string? Password { get; init; }

    /// <summary>List on the master (when one is set).</summary>
    public bool Listed { get; init; } = true;

    public bool Upnp { get; init; } = true;

    public string? MasterUrl { get; init; }

    public string? InviteKey { get; init; }

    /// <summary>Only signed-in players (with the master's ticket) may join; needs a master.</summary>
    public bool RequireAccounts { get; init; }

    /// <summary>The host's install id, shared with its player so guests use the same tag in server lists.</summary>
    public string? HostInstallId { get; init; }

    /// <summary>The folder each round is recorded into as a demo, or null to record nothing.</summary>
    public string? DemoFolder { get; init; }

    /// <summary>Bind to loopback only (the probe and unattended test runs: no firewall prompt).</summary>
    public bool LoopbackOnly { get; init; }

    /// <summary>The LAN discovery port (0: no LAN beacon).</summary>
    public int LanPort { get; init; } = BuildInfo.DefaultLanPort;
}

/// <summary>
/// A listen server from the game, ported from the Godot client's game/Scripts/Online/ListenHost.cs without Godot: the
/// dedicated server's online session on its own thread, taking the host's own player over loopback and everyone else
/// over UDP (and through the master's introducer and relay when it is listed), the router's port mapped with UPnP.
/// The host's player joins like any other through <see cref="LocalTarget"/> with <see cref="HostKey"/>, which makes it
/// the lobby's host.
/// <para>The session belongs to the host thread. The game thread talks to it only through <see cref="Command"/>
/// (answers come back through <see cref="TryTakeReply"/>), reads the log lines through <see cref="TryTakeLog"/> and the
/// status the thread writes about once a second (<see cref="StatusLine"/>, <see cref="PlayersLine"/>,
/// <see cref="MasterLine"/>).</para>
/// </summary>
public sealed class ListenHost : IDisposable
{
    public const string LoopbackAddress = "listen-server";

    /// <summary>Log lines kept for the game thread when nobody takes them (the oldest go first).</summary>
    private const int LogCapacity = 256;

    /// <summary>Server ticks between two status snapshots (about a second).</summary>
    private const int StatusEveryTicks = 60;

    private readonly LoopbackNetwork _network;
    private readonly IClock _clock;
    private readonly ConcurrentQueue<string> _commands = new();
    private readonly ConcurrentQueue<string> _replies = new();
    private readonly ConcurrentQueue<string> _log = new();
    private readonly CancellationTokenSource _stop = new();
    private readonly Thread _thread;
    private readonly UpnpPorts _upnp = new();
    private readonly bool _tickLog = Environment.GetEnvironmentVariable("SVM_TICK_LOG") == "1";
    private int _localPlayers;
    private long _ticksPrintedAt;
    private int _statusTicks;
    private bool _disposed;
    private readonly object _exitGate = new();
    private bool _exited;
    private bool _disposeOnExit;

    private ListenHost(HostRequest request, OnlineServer server, LiteNetLibTransport udp, LoopbackNetwork network, IClock clock, string hostKey)
    {
        Request = request;
        Server = server;
        Udp = udp;
        _network = network;
        _clock = clock;
        HostKey = hostKey;
        Port = udp.LocalPort;
        _thread = new Thread(Run) { IsBackground = true, Name = "OpenSCCT listen host" };
    }

    public HostRequest Request { get; }

    /// <summary>The session; it belongs to the host thread (talk to it through <see cref="Command"/>).</summary>
    public OnlineServer Server { get; }

    public LiteNetLibTransport Udp { get; }

    /// <summary>Proves the host's own player is the host.</summary>
    public string HostKey { get; }

    /// <summary>The thread's failure, if it stopped.</summary>
    public Exception? Failure { get; private set; }

    public bool IsRunning => _thread.IsAlive;

    /// <summary>The UDP port the session listens on (the asked one, or the one the system gave for port 0).</summary>
    public int Port { get; }

    /// <summary>The UPnP mapping's result as a sentence, or null while the router is still being asked (or UPnP is off).</summary>
    public string? UpnpLine { get; private set; }

    /// <summary>The session's phase and player count, written by the host thread about once a second.</summary>
    public string StatusLine { get; private set; } = "starting";

    /// <summary>The players of the session, one "id name (side, flags)" per player, separated by "; ".</summary>
    public string PlayersLine { get; private set; } = string.Empty;

    /// <summary>The master listing and the LAN beacon, as a sentence.</summary>
    public string MasterLine { get; private set; } = string.Empty;

    /// <summary>Players in the session (bots included), as of the last snapshot.</summary>
    public int PlayerCount { get; private set; }

    /// <summary>
    /// Opens the UDP port and the session (the lobby with <paramref name="request"/>'s setup) and starts the host thread.
    /// Throws <see cref="InvalidOperationException"/> with a readable reason (the port is in use, the map is missing).
    /// </summary>
    public static ListenHost Start(HostRequest request, MapCatalog maps, NetClassRegistry classes)
    {
        ArgumentNullException.ThrowIfNull(request);
        // Reserve peers for a lobby enlarged through Session Options after hosting starts.
        LiteNetLibTransport udp = LiteNetLibTransport.Listen(request.Port,
            Math.Max(request.MaxPlayers, LobbySetup.MaxMaxPlayers) + 6, request.LoopbackOnly);
        try
        {
            var clock = new HostClock();
            string key = Convert.ToHexString(System.Security.Cryptography.RandomNumberGenerator.GetBytes(8)).ToLowerInvariant();
            var network = new LoopbackNetwork();
            var botNavigation = new OpenScct.Bots.AiNavCache();

            // The composite's first transport is the loopback the host's own player connects over.
            var composite = new CompositeTransport(network.Bind(LoopbackAddress), udp);
            var settings = new OnlineServerSettings
            {
                Name = request.Name,
                Dedicated = false,
                Profile = NetProfile.Enhanced,
                MaxPlayers = request.MaxPlayers,
                Password = string.IsNullOrEmpty(request.Password) ? null : request.Password,
                Setup = request.Setup,
                Maps = maps,
                // Where a hosted map downloaded from SCCT-Maps came from, told to the players with the setup.
                MapSource = MapSources.SourceOf,
                Classes = classes,
                HostKey = key,
                Bots = OpenScct.Bots.OnlineServerBots.Factory(botNavigation),
                PrepareBots = OpenScct.Bots.OnlineServerBots.Preparation(botNavigation),
                LanPort = request.LanPort,
                LanLoopbackOnly = request.LoopbackOnly,
                RequireAccounts = request.RequireAccounts,
                DemoFolder = request.DemoFolder,
                HostInstallId = request.HostInstallId,

                // The lobby's Ban keeps a banned player out for the rest of this hosting (no file: nothing is kept after).
                Bans = new BanList(null, static () => DateTimeOffset.UtcNow),
                Master = request.MasterUrl is { Length: > 0 } url
                    ? new MasterLinkSettings { Url = url, InviteKey = request.InviteKey, Listed = request.Listed }
                    : null,
            };
            var server = new OnlineServer(composite, settings, clock.NowMicroseconds, udp);
            var host = new ListenHost(request, server, udp, network, clock, key);
            server.Logged += host.Log;
            host.Snapshot(server);
            host._thread.Start();
            if (request.Upnp && !request.LoopbackOnly)
            {
                host.UpnpLine = null;
                host._upnp.MapAsync(host.Port).ContinueWith(t =>
                {
                    host.UpnpLine = t.IsCompletedSuccessfully ? t.Result : "UPnP: the port mapping failed.";
                    host.Log(host.UpnpLine);
                }, System.Threading.Tasks.TaskScheduler.Default);
            }
            else
            {
                host.UpnpLine = request.LoopbackOnly ? "UPnP: not used (loopback only)." : "UPnP: off.";
                host.Log(host.UpnpLine);
            }

            return host;
        }
        catch
        {
            udp.Dispose();
            throw;
        }
    }

    /// <summary>The host's own player's way in: a loopback connection to the session.</summary>
    public JoinTarget LocalTarget => JoinTarget.Custom(Request.Name, () =>
        _network.Bind("local-player-" + Interlocked.Increment(ref _localPlayers).ToString(CultureInfo.InvariantCulture)), LoopbackAddress);

    /// <summary>Runs an admin command on the host thread (kick, map, say...); its answers come out of <see cref="TryTakeReply"/>.</summary>
    public void Command(string line) => _commands.Enqueue(line);

    /// <summary>An answer of a <see cref="Command"/>, oldest first.</summary>
    public bool TryTakeReply([System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out string? reply) => _replies.TryDequeue(out reply);

    /// <summary>A line of the session's log (joins, launches, the master, UPnP), oldest first.</summary>
    public bool TryTakeLog([System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out string? line) => _log.TryDequeue(out line);

    private void Log(string line)
    {
        if (_log.Count >= LogCapacity)
        {
            _log.TryDequeue(out _);
        }

        _log.Enqueue(line);
    }

    private void Run()
    {
        try
        {
            Server.RunRealtime(_clock, _stop.Token, s =>
            {
                if (++_statusTicks >= StatusEveryTicks)
                {
                    _statusTicks = 0;
                    Snapshot(s);
                }

                // SVM_TICK_LOG=1: what the ticks cost, every 10 s of a round (the host is new on each map).
                TickTimes ticks = s.Host.TickTimes;
                long count = _tickLog ? ticks.Count : 0;
                if (count < _ticksPrintedAt) _ticksPrintedAt = 0;
                if (s.Phase != SessionPhase.Lobby && count - _ticksPrintedAt >= 10 * OpenScct.Core.Sim.Hz)
                {
                    _ticksPrintedAt = count;
                    Log($"server ticks ({s.Host.Players.Count} players): {ticks.Recent(10 * OpenScct.Core.Sim.Hz)}");
                }

                while (_commands.TryDequeue(out string? line))
                {
                    foreach (string reply in s.Execute(line))
                    {
                        _replies.Enqueue(reply);
                    }

                    Snapshot(s);
                }
            });
        }
        catch (OperationCanceledException) when (_stop.IsCancellationRequested)
        {
            // Stop() cancelled the run: not a failure.
        }
#pragma warning disable CA1031 // An exception leaving this thread ends the whole game; any failure of a tick stops the host with a message instead.
        catch (Exception e)
#pragma warning restore CA1031
        {
            Failure = e;
            Log("The listen host stopped: " + e);
        }
        finally
        {
            StatusLine = Failure is null ? "closed" : "stopped: " + Failure.Message;
            // Dispose gave up waiting (a map load can outlast its wait): the server is disposed here, once this
            // thread is done with it, not under it.
            lock (_exitGate)
            {
                _exited = true;
                if (_disposeOnExit)
                {
                    Server.Dispose();
                    _stop.Dispose();
                }
            }
        }
    }

    /// <summary>The status the game thread reads, taken on the host thread (the session's lists belong to it).</summary>
    private void Snapshot(OnlineServer s)
    {
        PlayerCount = s.Players.Count;
        StatusLine = string.Create(CultureInfo.InvariantCulture,
            $"{s.Phase} on {s.Setup.Map} ({LobbySetup.ModeName(s.Setup.Mode)}), {s.Players.Count}/{s.Settings.MaxPlayers} players, {s.RoundsStarted} round(s) started");
        PlayersLine = string.Join("; ", s.Players.Select(p => string.Create(CultureInfo.InvariantCulture,
            $"{p.Id} {p.Name} ({(p.IsSpectator ? "watching" : p.Team == 1 ? "merc" : "spy")}{(p.IsHost ? ", host" : "")}{(p.Ready ? ", ready" : "")}{(p.IsBot ? ", bot" : "")}{(p.Slot is null && s.Phase != SessionPhase.Lobby ? ", loading" : "")})")));

        string master = s.Master switch
        {
            null => "master: none (join by address or LAN only)",
            { Registered: true } m => $"master: registered as {m.ServerId}{(m.Settings.Listed ? ", listed" : ", unlisted")}, reached {m.Reachability}{(m.PublicAddress is { } a ? " at " + a : "")}",
            { LastError: { } error } => "master: not registered: " + error,
            _ => "master: registering",
        };
        string lan = s.Lan is { } beacon
            ? string.Create(CultureInfo.InvariantCulture, $"LAN beacon on UDP {beacon.Port}, {beacon.Answered} answer(s)")
            : "no LAN beacon";
        MasterLine = master + "; " + lan;
    }

    /// <summary>Tells the players the host closed, takes it off the master and stops the thread.</summary>
    public void Stop()
    {
        if (!_thread.IsAlive)
        {
            return;
        }

        Command("close");
        if (!_thread.Join(TimeSpan.FromSeconds(3)))
        {
            _stop.Cancel();
            _thread.Join(TimeSpan.FromSeconds(2));
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Stop();
        _stop.Cancel();
        _upnp.Remove();
        lock (_exitGate)
        {
            if (!_exited)
            {
                _disposeOnExit = true;
                return;
            }
        }

        Server.Dispose();
        _stop.Dispose();
    }

    /// <summary>The host thread's clock (the Godot client's StopwatchClock).</summary>
    private sealed class HostClock : IClock
    {
        private readonly long _start = System.Diagnostics.Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)System.Diagnostics.Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
