using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Security.Cryptography;
using OpenScct.Core;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.Server.Online;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// The Unreal client's online play: one visit to an existing OpenSCCT server (docs/ONLINE.md in OpenSCCT). Ported
/// from the Godot client's game/Scripts/Online/OnlineService.cs with the scene handling taken out: it owns the
/// <see cref="OnlineClient"/>, ticks it at 60 Hz from <see cref="Update"/> on the caller's thread, holds the lobby as
/// the server describes it, and when the visit reaches a round's map it wraps the visit's connection in a remote
/// <see cref="PlaySession"/>, the same object a local match gives the bridge, so pawns, poses, the view and input
/// read the same either way.
/// <para>It is not a per-session feature (<c>BridgeSession.Feature</c>): the visit lives across maps and makes the
/// play sessions itself, one per round.</para>
/// <para>Single-threaded: everything is called from Unreal's game thread. The master's HTTP answers arrive on
/// worker threads and are only picked up inside <see cref="Update"/>.</para>
/// </summary>
public sealed class OnlineBridge : IDisposable
{
    public const int ApiVersion = 1;

    /// <summary>When the caller falls further behind than this (a stall, a map load), the backlog is dropped.</summary>
    private const int MaxCatchUpTicks = 5;

    /// <summary>Chat lines kept for <see cref="TakeChat"/> when nobody takes them (the oldest go first).</summary>
    private const int ChatCapacity = 64;

    private readonly StopwatchClock _clock = new();
    private readonly Queue<SessionMessage> _chat = new();
    private PlayCatalog? _catalog;
    private NetClassRegistry? _classes;
    private long _start;
    private long _ticks;
    private bool _loadReported;
    private ClientSession? _roundRefused;
    private ClientSession? _roundEnded;
    private BridgeSession? _adopted;
    private string? _error;
    private OnlineState _stateOverride = OnlineState.Idle;
    private int _revision;

    private string _playerName = "Player";
    private string _installId = NewInstallId();
    private PlayerKey _key = PlayerKey.Create();

    public OnlineBridge()
    {
        Master = new OnlineMaster(this);
    }

    /// <summary>The one online bridge of the process, made on first use (the entry points and the probe use it).</summary>
    public static OnlineBridge Instance => s_instance ??= new OnlineBridge();

    private static OnlineBridge? s_instance;

    /// <summary>
    /// Hands a round's play session to the bridge's main session slot, so the per-frame entries of the main table
    /// (pawns, poses, view, input) read the online round, and that table's Update ticks it. Exports sets it to wrap
    /// the play session in a BridgeSession and make it the current one; while it is null this class ticks and
    /// disposes the round itself.
    /// </summary>
    internal static Func<PlaySession, BridgeSession>? AdoptRound { get; set; }

    /// <summary>Takes the session <see cref="AdoptRound"/> returned out of the main slot and disposes it.</summary>
    internal static Action<BridgeSession>? ReleaseRound { get; set; }

    /// <summary>The player's visit, or null when not online. It stays after a failure so its error can be read.</summary>
    public OnlineClient? Client { get; private set; }

    /// <summary>The round being played on the visit's connection, or null (lobby, travel, results, not online).</summary>
    public PlaySession? Round { get; private set; }

    /// <summary>The master's list, listed joins and the account.</summary>
    public OnlineMaster Master { get; }

    /// <summary>The maps this client plays (every installed map with its play loader), read on first use.</summary>
    public PlayCatalog Catalog => _catalog ??= new PlayCatalog();

    /// <summary>The replicated classes; their hash is this build's network protocol.</summary>
    public NetClassRegistry Classes => _classes ??= NetClasses.Create();

    public IClock Clock => _clock;

    /// <summary>Goes up when anything a menu shows changed (the lobby, the phase, results, errors, master answers).</summary>
    public int Revision => _revision;

    public string PlayerName => Master.AccountName ?? _playerName;

    public string InstallId => _installId;

    public PlayerKey Key => _key;

    public OnlineState State => Client is { } client ? (OnlineState)((int)client.State + 1) : _stateOverride;

    /// <summary>Why the visit failed, as a player reads it, or null.</summary>
    public string? Error => Client?.Error ?? _error;

    /// <summary>True between entering a round and <see cref="ReportLoaded"/>.</summary>
    public bool NeedsLoadReport => Round is not null && !_loadReported;

    /// <summary>The round's map has no converted content here, so the visit goes on without a play session.</summary>
    public bool RoundRefused => Client?.Session is { } session && ReferenceEquals(session, _roundRefused);

    /// <summary>
    /// The map Unreal must have loaded: the travel's target while travelling, the round's map while loading, playing
    /// and in the results; null in the lobby and when not online.
    /// </summary>
    public string? MapToLoad
    {
        get
        {
            if (Client is not { } client)
            {
                return null;
            }

            string? map = client.State switch
            {
                OnlineClientState.Travelling => client.TravellingTo,
                OnlineClientState.Loading or OnlineClientState.Match or OnlineClientState.Results => client.CurrentMap,
                _ => null,
            };
            return map is null || MapCatalog.IsMenuMap(map) ? null : map;
        }
    }

    // ---- identity -----------------------------------------------------------------------------------------------

    /// <summary>
    /// Who joins: the name a guest goes by, and the install's id and private key as Unreal saved them from an earlier
    /// run (bans and returning players follow them). An id or key that is empty or not valid is replaced by a new
    /// one; read both back and save them.
    /// </summary>
    public void SetIdentity(string? playerName, string? installId, string? privateKey)
    {
        if (!string.IsNullOrWhiteSpace(playerName))
        {
            _playerName = playerName.Trim();
        }

        // 16 hex digits, as the Godot client's OnlineSettings makes it.
        if (installId is { Length: 16 })
        {
            _installId = installId;
        }

        if (PlayerKey.FromPrivate(privateKey) is { } key)
        {
            _key = key;
        }

        Touch();
    }

    private static string NewInstallId() => Convert.ToHexString(RandomNumberGenerator.GetBytes(8)).ToLowerInvariant();

    // ---- joining and leaving ------------------------------------------------------------------------------------

    /// <summary>Joins the server at a typed "host", "host:port" or "[v6]:port" address (port 28888 when none is given).</summary>
    public void Join(string address, string? password = null, int preferredTeam = -1, bool loopbackOnly = false) =>
        Join(JoinTarget.FromAddress(address), password, ticket: null, preferredTeam, loopbackOnly);

    /// <summary>
    /// Starts a visit (OnlineService.JoinPrepared and StartVisit). <paramref name="ticket"/> is the master's ticket
    /// for that server when the player is signed in; <paramref name="preferredTeam"/> 0 spies, 1 mercs, 255 watch,
    /// -1 the smaller side.
    /// </summary>
    public void Join(JoinTarget target, string? password, string? ticket, int preferredTeam = -1, bool loopbackOnly = false)
    {
        ArgumentNullException.ThrowIfNull(target);
        EndVisit();
        _error = null;
        _stateOverride = OnlineState.Idle;
        var settings = new OnlineClientSettings
        {
            PlayerName = PlayerName,
            InstallId = _installId,
            Key = _key,
            Password = string.IsNullOrEmpty(password) ? null : password,
            Ticket = ticket,
            PreferredTeam = preferredTeam,
            Maps = Catalog.Maps,
            Classes = Classes,
            LoopbackOnly = loopbackOnly,
            // The play loaders install the gameplay services with each map, as for a local match.
            InstallGameplayServices = false,
            // The server's start gate waits for ReportLoaded: Unreal says when its level is up.
            AutoReportReady = false,
        };
        _start = _clock.NowMicroseconds;
        _ticks = 0;
        var client = new OnlineClient(target, settings, _start);
        Client = client;
        client.Changed += Touch;
        client.SessionEnding += OnSessionEnding;
        client.MessageReceived += OnMessage;
        Touch();
    }

    /// <summary>Shows a join that failed before a visit existed (the master refused it) as a failed visit.</summary>
    internal void FailJoin(string error)
    {
        EndVisit();
        _error = error;
        _stateOverride = OnlineState.Failed;
        Touch();
    }

    /// <summary>Shows "connecting" while the master is asked how to reach a listed server.</summary>
    internal void BeginListedJoin()
    {
        EndVisit();
        _error = null;
        _stateOverride = OnlineState.Connecting;
        Touch();
    }

    /// <summary>Leaves the server (it tells the others) and clears the visit: the state is Idle afterwards.</summary>
    public void Leave()
    {
        Master.CancelJoin();
        EndVisit();
        _error = null;
        _stateOverride = OnlineState.Idle;
        Touch();
    }

    private void EndVisit()
    {
        if (Client is not { } client)
        {
            return;
        }

        StopRound();
        client.Changed -= Touch;
        client.SessionEnding -= OnSessionEnding;
        client.MessageReceived -= OnMessage;
        client.Leave();
        client.Dispose();
        Client = null;
        _roundRefused = null;
        _roundEnded = null;
        _chat.Clear();
    }

    // ---- the lobby ----------------------------------------------------------------------------------------------

    /// <summary>Picks a side in the lobby: 0 spies, 1 mercs, 255 spectators.</summary>
    public void SetTeam(int team) => Client?.SetTeam(team);

    public void SetReady(bool ready) => Client?.SetReady(ready);

    /// <summary>Asks for the launch from the lobby (or its cancel while the countdown runs), or the next round from the results.</summary>
    public void Launch() => Client?.Launch();

    /// <summary>Host or admin: everyone back to the lobby.</summary>
    public void BackToLobby() => Client?.BackToLobby();

    /// <summary>Logs in as an admin of a dedicated server.</summary>
    public void Admin(string password) => Client?.Admin(password);

    /// <summary>An admin console command (status, map, set...); the answers arrive as system chat lines.</summary>
    public void Command(string command) => Client?.Command(command);

    /// <summary>A chat line to everyone, or to the player's side. Works in the lobby and in a round.</summary>
    public void Chat(string text, bool teamOnly)
    {
        if (!string.IsNullOrWhiteSpace(text))
        {
            Client?.Chat(text.Length > ChatK.MaxChars ? text[..ChatK.MaxChars] : text, teamOnly);
        }
    }

    /// <summary>The next waiting chat or system line, oldest first; false when there is none.</summary>
    public bool TakeChat(out SessionMessage message) => _chat.TryDequeue(out message!);

    public int ChatWaiting => _chat.Count;

    private void OnMessage(SessionMessage message)
    {
        if (_chat.Count == ChatCapacity)
        {
            _chat.Dequeue();
        }

        _chat.Enqueue(message);
    }

    // ---- the round ----------------------------------------------------------------------------------------------

    /// <summary>
    /// Unreal's level for the round's map is up (PlayRoot's MarkReadyToPlay in the Godot client): the next network
    /// tick tells the server this player is in and waiting, and the server's start gate counts him.
    /// </summary>
    public void ReportLoaded()
    {
        if (Round is { } round && !_loadReported)
        {
            _loadReported = true;
            round.Client.MarkReadyToPlay();
            Touch();
        }
    }

    /// <summary>
    /// Runs the visit's 60 Hz ticks that are due (OnlineService._Process), follows the visit into and out of rounds,
    /// and picks up the master's answers. Call once a frame, before the main table's Update. Returns the visit ticks
    /// run. A tick that connects to a round's map loads that map's simulation and blocks for as long (seconds):
    /// the state is Travelling for at least two seconds before, which is when to put a loading screen up.
    /// </summary>
    public int Update()
    {
        Master.Pump();
        if (Client is not { } client)
        {
            return 0;
        }

        long now = _clock.NowMicroseconds;
        long due = (now - _start) * Sim.Hz / 1_000_000;
        if (due - _ticks > MaxCatchUpTicks)
        {
            _ticks = due - MaxCatchUpTicks;
        }

        int ran = 0;
        while (_ticks < due && client.State is not (OnlineClientState.Failed or OnlineClientState.Left))
        {
            client.Tick(_start + (_ticks + 1) * 1_000_000 / Sim.Hz);
            _ticks++;
            ran++;
        }

        FollowState(client);

        // Not adopted by the main session slot: the round is ticked here, on the same clock the visit ticked the
        // connection on so far (a fresh clock would start behind it and the driver would see time go backwards).
        if (Round is { IsStopped: false } round && _adopted is null)
        {
            round.Update();
        }

        return ran;
    }

    /// <summary>OnlineService.ShowState without the screens: a round is entered on its map and left at its end.</summary>
    private void FollowState(OnlineClient client)
    {
        switch (client.State)
        {
            case OnlineClientState.Loading or OnlineClientState.Match
                when Round is null && client.Session is { } session && !ReferenceEquals(session, _roundEnded):
                EnterRound(client, session);
                break;

            // The server's "phase results" line comes before the rows and the "results" line, which can land on
            // later ticks: the round stays up until they are in (the end banner shows meanwhile).
            case OnlineClientState.Results when client.Results is not null:
            case OnlineClientState.Lobby or OnlineClientState.Travelling or OnlineClientState.Failed or OnlineClientState.Left:
                if (Round is not null)
                {
                    _roundEnded = client.Session;
                    StopRound();
                }

                break;
        }
    }

    /// <summary>
    /// The visit is on a round's map (after a travel, or joining a running round as a spectator): a remote play
    /// session takes over ticking the connection with the local input (OnlineService.EnterRound).
    /// </summary>
    private void EnterRound(OnlineClient client, ClientSession session)
    {
        if (ReferenceEquals(session, _roundRefused))
        {
            return;
        }

        if (client.CurrentMap is not { } map || MapCatalog.IsMenuMap(map) || !Catalog.CanPlay(map))
        {
            // Without the map's content the visit still goes on, headless: the menus say so.
            _roundRefused = session;
            Touch();
            return;
        }

        var settings = new PlaySettings
        {
            Map = map,
            Side = client.Lobby.Local?.Team == 1 ? PlaySide.Merc : PlaySide.Spy,
            Mode = MapCatalog.ModeKey(client.Lobby.Setup.Mode),
            PlayerName = PlayerName,
            MapRoots = [.. Catalog.Maps.Roots],
        };
        var round = new PlaySession(settings, _clock, session, Catalog.Maps, Catalog.Maps.FindFolder(map)!, Catalog.Loader(map));
        client.SessionTickedElsewhere = true;
        Round = round;
        _loadReported = false;
        _adopted = AdoptRound?.Invoke(round);
        Touch();
    }

    /// <summary>The visit is about to close a connection: a round on it stops first.</summary>
    private void OnSessionEnding(ClientSession session)
    {
        if (Round is { } round && ReferenceEquals(round.Client, session))
        {
            StopRound();
        }
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
                ReleaseRound?.Invoke(adopted);
            }

            // A remote play session leaves the connection to the visit; disposing twice is harmless.
            round.Dispose();
            Touch();
        }

        _loadReported = false;
        if (Client is { } client)
        {
            // The visit ticks the connection again (the results and the way back to the lobby).
            client.SessionTickedElsewhere = false;
        }
    }

    internal void Touch() => _revision++;

    public void Dispose()
    {
        EndVisit();
        Master.Dispose();
        if (ReferenceEquals(s_instance, this))
        {
            s_instance = null;
        }
    }

    /// <summary>The visit's clock: the round's play session keeps ticking the connection on it.</summary>
    private sealed class StopwatchClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
