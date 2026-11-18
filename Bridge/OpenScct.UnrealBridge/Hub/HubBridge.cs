using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using OpenScct.Core;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.Server.Hub;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// The Unreal client's hub visit (docs/HUB.md in OpenSCCT). Ported from the Godot client's
/// game/Scripts/Hub/HubService.cs with the scenes and the HUD taken out: it owns the player's <see cref="HubClient"/>,
/// ticks it at 60 Hz from <see cref="Update"/> on the caller's thread until the hub map and the player's controller
/// are in, then wraps the visit's connection in a remote <see cref="PlaySession"/> and puts it in the bridge's main
/// session slot, so pawns, poses, the view and input read the same as in a match. A refusal or a lost connection
/// ends the visit as Failed with the reason.
/// <para>It can also host the hub it joins (<see cref="HostAndJoin"/>): an in-process <see cref="HubServer"/> on its
/// own thread, as "Host a hub on this PC" does in the Godot client, listed in the master's hub list when asked. The
/// master's hub list, its pings and joins of a listed hub are in HubBridge.Master.cs.</para>
/// <para>The pads' matches (the hub's offer, the online visit to the match and the way back) and the pad laptop are in
/// HubBridge.Pads.cs. Not ported yet: voice chat, the activities' props and sounds.</para>
/// <para>Single-threaded: everything is called from Unreal's game thread; only the hosted hub's log lines arrive on
/// its own thread, and they are queued.</para>
/// </summary>
public sealed partial class HubBridge : IDisposable
{
    public const int ApiVersion = 2;

    /// <summary>When the caller falls further behind than this (a stall, a level load), the backlog is dropped.</summary>
    private const int MaxCatchUpTicks = 5;

    /// <summary>Log lines kept for <see cref="TakeLog"/> when nobody takes them (the oldest go first).</summary>
    private const int LogCapacity = 256;

    private readonly StopwatchClock _clock = new();
    private readonly ConcurrentQueue<string> _log = new();
    private readonly List<string> _mapRoots = [];
    private HubPlayCatalog? _catalog;
    private bool _catalogStale = true;
    private NetClassRegistry? _classes;
    private long _start;
    private long _ticks;
    private BridgeSession? _adopted;
    private bool _loadReported;
    private string _target = string.Empty;
    private string _name = "Player";
    private int _team;
    private string? _error;
    private string _status = string.Empty;
    private int _revision;
    private HubState _lastState;

    /// <summary>The one hub bridge of the process, made on first use (the entry points and the probe use it).</summary>
    public static HubBridge Instance => s_instance ??= new HubBridge();

    private static HubBridge? s_instance;

    /// <summary>The hub bridge if one was made, without making one (the module's shutdown asks).</summary>
    internal static HubBridge? Existing => s_instance;

    /// <summary>The visit, or null when not in a hub.</summary>
    public HubClient? Client { get; private set; }

    /// <summary>The visit's play session once the hub map and the controller are in, or null.</summary>
    public PlaySession? Round { get; private set; }

    /// <summary>The hub this PC hosts, or null.</summary>
    public HubHost? Host { get; private set; }

    /// <summary>
    /// The maps the visit plays: the folders given with <see cref="AddMapRoot"/> first, then the default roots. Made
    /// again when a root was added, but never during a visit.
    /// </summary>
    public HubPlayCatalog Catalog
    {
        get
        {
            if (_catalog is null || (_catalogStale && Client is null))
            {
                _catalog = new HubPlayCatalog([.. _mapRoots]);
                _catalogStale = false;
            }

            return _catalog;
        }
    }

    /// <summary>The replicated classes: the online bridge's when it made them, so the process has one protocol.</summary>
    public NetClassRegistry Classes => OnlineBridge.Existing?.Classes ?? (_classes ??= NetClasses.Create());

    public IClock Clock => _clock;

    /// <summary>Goes up when anything the hub page shows changed.</summary>
    public int Revision => _revision;

    public HubState State
    {
        get
        {
            if (Client is null)
            {
                return _trip is not null ? HubState.InMatch
                    : _pendingJoin is not null ? HubState.Connecting : _error is null ? HubState.Idle : HubState.Failed;
            }

            return Round is not null ? HubState.InHub : Client.Welcomed ? HubState.Loading : HubState.Connecting;
        }
    }

    /// <summary>Why the visit failed, as a player reads it, or null.</summary>
    public string? Error => _error;

    /// <summary>What is happening, for the hub page.</summary>
    public string Status => _status;

    /// <summary>The "host:port" the visit connects to (empty when not in a hub).</summary>
    public string Address => Client is null ? string.Empty : _target;

    public string PlayerName => _name;

    /// <summary>True between entering the hub and <see cref="ReportLoaded"/>.</summary>
    public bool NeedsLoadReport => Round is not null && !_loadReported;

    /// <summary>The map Unreal must have open: the hub's map once the hub welcomed the player, else null.</summary>
    public string? MapToLoad => Client is { } client && (client.Welcomed || client.Session.Map is not null)
        ? client.Session.Map?.Name ?? HubK.MapName
        : null;

    /// <summary>Seconds since the visit started connecting.</summary>
    public float ConnectSeconds => Client is null ? 0f : (float)((_clock.NowMicroseconds - _start) / 1_000_000.0);

    /// <summary>
    /// Adds a folder that holds SVMap folders (the Unreal project's Saved/ScctImport/HubContent/svmap, which
    /// Tools/Import/Import-HubMap.ps1 lays out with the hub map in it). Seen by the next visit.
    /// </summary>
    public void AddMapRoot(string folder)
    {
        if (string.IsNullOrWhiteSpace(folder))
        {
            return;
        }

        string full = System.IO.Path.GetFullPath(folder);
        if (!_mapRoots.Exists(r => string.Equals(r, full, StringComparison.OrdinalIgnoreCase)))
        {
            _mapRoots.Add(full);
            _catalogStale = true;
        }
    }

    /// <summary>"host" or "host:port" with the hub's default port added ("[v6]" and "[v6]:port" too).</summary>
    public static string Normalise(string address)
    {
        string a = (address ?? string.Empty).Trim();
        if (a.Length == 0)
        {
            throw new ArgumentException("Enter the hub's address.");
        }

        string port = HubK.DefaultPort.ToString(CultureInfo.InvariantCulture);
        if (a.StartsWith('['))
        {
            return a.Contains("]:", StringComparison.Ordinal) ? a : a + ":" + port;
        }

        int colon = a.LastIndexOf(':');
        bool hasPort = colon > 0 && a.IndexOf(':') == colon && int.TryParse(a[(colon + 1)..], out _);
        return hasPort ? a : a + ":" + port;
    }

    // ---- joining, hosting and leaving ---------------------------------------------------------------------------

    /// <summary>Joins the hub at <paramref name="address"/> as <paramref name="name"/> on <paramref name="team"/> (0 spies, 1 mercs).</summary>
    public void Join(string address, string? name, int team)
    {
        string target = Normalise(address);
        Leave();
        ForgetListed();
        Connect(target, name, team);
    }

    /// <summary>
    /// Opens a hub on this PC (UDP <paramref name="port"/>; every interface so LAN and direct-address players can
    /// join, or the loopback addresses only) and joins it. False with <see cref="Error"/> set when it could not.
    /// </summary>
    public bool HostAndJoin(int port, string? name, int team, bool loopbackOnly)
    {
        Leave();
        ForgetListed();
        string player = string.IsNullOrWhiteSpace(name) ? "Player" : name.Trim();
        try
        {
            Host = HubHost.Start(port <= 0 ? HubK.DefaultPort : port, player + "'s hub", new MapCatalog([.. Catalog.Maps.Roots]), Classes,
                loopbackOnly, line => Log("server: " + line), HostListing(loopbackOnly));
        }
        catch (InvalidOperationException e)
        {
            Fail("Could not host a hub: " + e.Message);
            return false;
        }

        Connect(Host.LocalAddress, player, team);
        return true;
    }

    private void Connect(string target, string? name, int team)
    {
        _error = null;
        _name = string.IsNullOrWhiteSpace(name) ? "Player" : name.Trim();
        _team = team == 1 ? 1 : 0;
        _target = target;
        _offerTaken = null;
        // A hub on this machine is reached through a socket on the loopback addresses only: a socket on every
        // interface makes Windows ask the player about the firewall, for nothing.
        var transport = LiteNetLibTransport.CreateClient(loopbackOnly: IsThisMachine(target));
        _start = _clock.NowMicroseconds;
        _ticks = 0;
        HubPlayCatalog catalog = Catalog;
        var client = new HubClient(transport, target, _start, new HubClientSettings
        {
            PlayerName = _name,
            Team = _team,
            Maps = catalog.Maps,
            Classes = Classes,
            // The play loaders install the gameplay services with the map, as for a match.
            InstallGameplayServices = false,
            // The hub places the player once Unreal says its level is up (ReportLoaded).
            AutoReportReady = false,
            // Proximity voice has no microphone or speaker here yet: the hub sends this visit no voice.
            Voice = false,
            InstallId = OnlineBridge.Existing?.InstallId ?? string.Empty,
            HostKey = Host?.HostKey,
            // The master's ticket is for the listed hub it was asked about only (a hosted or typed-in hub gets none).
            Ticket = _masterId is not null ? _ticket : null,
            // Back from a pad's match: the hub puts the player beside that pad.
            ReturnPad = _returnPad,
            // The Armoury's look; a change while in the hub is sent by the account service.
            SpyLook = Social.ArmouryLook.Shared(OpenScct.Net.Cosmetics.CosmeticSide.Spy),
            MercLook = Social.ArmouryLook.Shared(OpenScct.Net.Cosmetics.CosmeticSide.Merc),
        }, ownsTransport: true);
        client.Play.Clock = () => _clock.NowMicroseconds / 1_000_000.0;
        client.LaptopOpened += OnLaptopOpened;
        Client = client;
        _status = $"Connecting to {target}...";
        Log($"connecting to {target} as '{_name}' ({(_team == 1 ? "merc" : "spy")})" + (_returnPad is null ? string.Empty : $", back from the match of {_returnPad}"));
        Touch();
    }

    /// <summary>True for an address that names this machine by a loopback address or as localhost.</summary>
    private static bool IsThisMachine(string target)
    {
        string host = HostOf(target);
        return host.Equals("localhost", StringComparison.OrdinalIgnoreCase)
            || (System.Net.IPAddress.TryParse(host, out System.Net.IPAddress? ip) && System.Net.IPAddress.IsLoopback(ip));
    }

    /// <summary>The host part of "host:port" or "[v6]:port".</summary>
    private static string HostOf(string target)
    {
        if (target.StartsWith('[') && target.IndexOf(']', StringComparison.Ordinal) is > 0 and int end)
        {
            return target[1..end];
        }

        int colon = target.LastIndexOf(':');
        return colon > 0 ? target[..colon] : target;
    }

    /// <summary>Leaves the hub (it hears so), closes the hub this PC hosts and clears a failure: the state is Idle afterwards.</summary>
    public void Leave()
    {
        bool had = Client is not null || Host is not null || _error is not null;
        EndVisit();
        _error = null;
        _status = string.Empty;
        _lastState = HubState.Idle;
        if (had)
        {
            Touch();
        }
    }

    /// <summary>Ends the visit with <paramref name="error"/>: the state is Failed until Leave or the next join.</summary>
    internal void Fail(string error)
    {
        EndVisit();
        _error = error;
        _status = error;
        _lastState = HubState.Failed;
        Log("failed: " + error);
        Touch();
    }

    private void EndVisit()
    {
        EndTrip();
        _pendingJoin = null;
        _attempts.Clear();
        StopRound();
        if (Client is { } client)
        {
            Client = null;
            try
            {
                client.Session.Disconnect();
            }
            finally
            {
                client.Dispose();
            }
        }

        if (Bots is { } bots)
        {
            Bots = null;
            bots.Dispose();
            Log("the hub bots left");
        }

        if (Host is { } host)
        {
            Host = null;
            host.Dispose();
            Log("the hub this PC hosted is closed");
        }
    }

    /// <summary>The bot visitors of the hub this PC hosts, or null.</summary>
    public HubBots? Bots { get; private set; }

    /// <summary>
    /// Sends <paramref name="count"/> bot visitors into the hub this PC hosts (none before, they replace any already
    /// there). False with the reason logged when this PC hosts no hub: bots are never sent into someone else's.
    /// </summary>
    public bool StartBots(int count)
    {
        if (Host is not { } host)
        {
            Log("hub bots are only sent into a hub this PC hosts");
            return false;
        }

        Bots?.Dispose();
        Bots = count > 0 ? HubBots.Start(count, host.LocalAddress, new MapCatalog([.. Catalog.Maps.Roots]), Classes, line => Log("bots: " + line)) : null;
        Touch();
        return Bots is not null;
    }

    // ---- the frame ----------------------------------------------------------------------------------------------

    /// <summary>
    /// Unreal's hub level is up and presents the visit's session (PlayRoot's MarkReadyToPlay in the Godot client):
    /// the next network tick tells the hub this player is in, and the hub spawns his pawn.
    /// </summary>
    public void ReportLoaded()
    {
        if (Round is { } round && !_loadReported)
        {
            _loadReported = true;
            round.Client.MarkReadyToPlay();
            Log("the hub level is up: told the hub this player is in");
            Touch();
        }
    }

    /// <summary>
    /// Runs the visit's 60 Hz ticks that are due (HubService._Process), enters the hub once its map and the
    /// player's controller are in, and ends the visit on a refusal or a lost connection. Call once a frame, before
    /// the main table's Update. Returns the visit ticks run. The tick that receives the hub map loads its simulation
    /// and blocks for as long.
    /// </summary>
    public int Update()
    {
        PumpMaster();
        PumpMatch();
        if (Client is not { } client)
        {
            LogState();
            return 0;
        }

        if (Host is { Failure: { } failure })
        {
            Fail("The hub this PC hosts stopped: " + failure.Message);
            return 0;
        }

        if (client.Error is { } failed && !client.Welcomed && client.Session.State != EClientState.Rejected && TryNextAttempt(failed))
        {
            return 0;
        }

        if (client.Error is { } error)
        {
            Fail(client.Welcomed || client.Session.State == EClientState.Rejected
                ? error
                : $"No hub answered at {_masterName ?? _target}. Check that a hub is running there (svm-server --hub) and that its UDP port "
                  + $"(default {HubK.DefaultPort.ToString(CultureInfo.InvariantCulture)}) is open, or host a hub on this PC.");
            return 0;
        }

        if (client.Offer is { } offer && !ReferenceEquals(offer, _offerTaken))
        {
            // The hub sent the player to a pad's match: the hub connection closes and the online visit plays it.
            GoToMatch(client, offer);
            LogState();
            return 0;
        }

        PumpPadRun(client);

        if (Round is { } drawn)
        {
            if (drawn.IsStopped)
            {
                // The main table stopped the session under the visit (its Stop, or a local match was started):
                // nothing would tick the connection any more. The player left.
                Log("the hub's session was stopped by the main table: leaving the hub");
                Leave();
                return 0;
            }

            drawn.Client.ReportRenderedFrame(drawn.Alpha);
        }

        int ran = 0;
        if (!client.SessionTickedElsewhere)
        {
            long now = _clock.NowMicroseconds;
            long due = (now - _start) * Sim.Hz / 1_000_000;
            if (due - _ticks > MaxCatchUpTicks)
            {
                _ticks = due - MaxCatchUpTicks;
            }

            while (_ticks < due && Client is not null && client.Error is null)
            {
                client.Tick(_start + (_ticks + 1) * 1_000_000 / Sim.Hz);
                _ticks++;
                ran++;
            }
        }

        if (Round is null && client.Error is null && client.Session.Map is { } map && client.LocalPlayer is not null)
        {
            Enter(client, map.Name);
        }
        else if (Round is null && client.Error is null)
        {
            long waited = (_clock.NowMicroseconds - _start) / 1_000_000;
            string status = client.Welcomed
                ? $"Loading {client.ServerName}..."
                : string.Create(CultureInfo.InvariantCulture, $"Connecting to {_masterName ?? _target}... {waited} s (gives up after {(int)OpenScct.Net.K.InitialConnectTimeout} s)");
            if (status != _status)
            {
                _status = status;
                Touch();
            }
        }

        // The session is ticked here, on the clock the visit ticked the connection on so far, also when the main
        // slot adopted it: the visit owns the connection, and a main table that stopped calling Update must not
        // starve it until the hub times the player out.
        if (Round is { IsStopped: false } round)
        {
            round.Update();
        }

        LogState();
        return ran;
    }

    private void LogState()
    {
        HubState state = State;
        if (state != _lastState)
        {
            Log($"state {_lastState} -> {state}");
            _lastState = state;
            Touch();
        }
    }

    /// <summary>The hub map and the controller are in: a remote play session takes over ticking the connection (HubService.Enter).</summary>
    private void Enter(HubClient client, string map)
    {
        if (!Catalog.CanPlay(map))
        {
            Fail($"{map} has no render content in this install (run Tools/Import/Import-HubMap.ps1).");
            return;
        }

        var settings = new PlaySettings
        {
            Map = map,
            Side = client.Team == 1 ? PlaySide.Merc : PlaySide.Spy,
            Mode = "Story",
            PlayerName = client.Settings.PlayerName,
            MapRoots = [.. Catalog.Maps.Roots],
        };
        ClientSession session = client.Session;
        var round = new PlaySession(settings, _clock, session, Catalog.Maps, Catalog.Maps.FindFolder(map)!, Catalog.Loader(map));
        client.SessionTickedElsewhere = true;
        Round = round;
        _loadReported = false;
        _adopted = Exports.AdoptRemote(round);
        _status = Host is { } host
            ? string.Create(CultureInfo.InvariantCulture, $"In {client.ServerName}, hosted on this PC (UDP port {host.Port})")
            : $"In {client.ServerName}";
        Log($"entered {client.ServerName} on map {map} as player {client.PlayerId}: the visit's session is in the main slot (session {_adopted.Id})");
        Touch();
    }

    private void StopRound()
    {
        if (Round is { } round)
        {
            Round = null;
            round.Stop();
            if (_adopted is { } adopted)
            {
                _adopted = null;
                Exports.Release(adopted);
            }

            // A remote play session leaves the connection to the visit.
            round.Dispose();
        }

        _loadReported = false;
        if (Client is { } client)
        {
            client.SessionTickedElsewhere = false;
        }
    }

    // ---- the log ------------------------------------------------------------------------------------------------

    /// <summary>A line for Unreal's log (the visit's steps and a hosted hub's own lines); any thread.</summary>
    internal void Log(string line)
    {
        while (_log.Count >= LogCapacity && _log.TryDequeue(out _))
        {
        }

        _log.Enqueue(line);
    }

    /// <summary>The next waiting log line, oldest first; false when there is none.</summary>
    public bool TakeLog(out string line) => _log.TryDequeue(out line!);

    public int LogWaiting => _log.Count;

    internal void Touch() => _revision++;

    public void Dispose()
    {
        EndVisit();
        StopPings();
        if (ReferenceEquals(s_instance, this))
        {
            s_instance = null;
        }
    }

    private sealed class StopwatchClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
