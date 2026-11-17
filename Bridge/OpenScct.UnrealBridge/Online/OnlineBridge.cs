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
public sealed partial class OnlineBridge : IDisposable
{
    public const int ApiVersion = 2;

    /// <summary>When the caller falls further behind than this (a stall, a map load), the backlog is dropped.</summary>
    private const int MaxCatchUpTicks = 5;

    /// <summary>Chat lines kept for <see cref="TakeChat"/> when nobody takes them (the oldest go first).</summary>
    private const int ChatCapacity = 64;

    private readonly StopwatchClock _clock = new();
    private readonly Queue<SessionMessage> _chat = new();
    private PlayCatalog? _catalog;
    private string? _catalogRoot;
    private NetClassRegistry? _classes;
    private long _start;
    private long _ticks;
    private bool _loadReported;
    private ClientSession? _roundRefused;
    private BridgeSession? _adopted;
    private string? _error;
    private OnlineState _stateOverride = OnlineState.Idle;
    private int _revision;
    private int _factsRevision = -1;
    private LobbyFacts _facts;

    /// <summary>How the visit's server was reached, for the rejoin record: its master id (a listed join) and password.</summary>
    private string? _visitMasterId;
    private string? _visitPassword;

    private string _playerName = "Player";
    private string _installId = NewInstallId();
    private PlayerKey _key = PlayerKey.Create();

    public OnlineBridge()
    {
        Master = new OnlineMaster(this);
        Browser = new ServerBrowser(this);
        Rejoin = new RejoinService(this);
    }

    /// <summary>The one online bridge of the process, made on first use (the entry points and the probe use it).</summary>
    public static OnlineBridge Instance => s_instance ??= new OnlineBridge();

    private static OnlineBridge? s_instance;

    /// <summary>The online bridge if one was made, without making one (the module's shutdown asks).</summary>
    internal static OnlineBridge? Existing => s_instance;

    /// <summary>The player's visit, or null when not online. It stays after a failure so its error can be read.</summary>
    public OnlineClient? Client { get; private set; }

    /// <summary>The round being played on the visit's connection, or null (lobby, travel, results, not online).</summary>
    public PlaySession? Round { get; private set; }

    /// <summary>The master's list, listed joins and the account.</summary>
    public OnlineMaster Master { get; }

    /// <summary>The server browser: the master's list, the LAN and the saved addresses, with pings (Online/ServerBrowser.cs).</summary>
    public ServerBrowser Browser { get; }

    /// <summary>The round to rejoin after a crash, a lost connection or a quit (Online/RejoinService.cs).</summary>
    public RejoinService Rejoin { get; }

    /// <summary>
    /// The maps this client plays (every installed map with its play loader), read on first use. It is read again
    /// when the content folder was configured or changed since (Unreal may ask for the server list before it sets
    /// the content), but never during a visit: the visit's sessions load through the catalog it started with.
    /// </summary>
    public PlayCatalog Catalog
    {
        get
        {
            if (_catalog is null || (Client is null && (!string.Equals(_catalogRoot, ContentPaths.Root, StringComparison.OrdinalIgnoreCase)
                || _catalog.Revision != MapFolders.Revision)))
            {
                _catalogRoot = ContentPaths.Root;
                _catalog = new PlayCatalog();
            }

            return _catalog;
        }
    }

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
    public void Join(string address, string? password = null, int preferredTeam = -1, bool loopbackOnly = false)
    {
        // A listed join still waiting for the master would otherwise replace this visit when its answer arrives.
        Master.CancelJoin();
        Join(JoinTarget.FromAddress(address), password, ticket: null, preferredTeam, loopbackOnly);
    }

    /// <summary>
    /// Starts a visit (OnlineService.JoinPrepared and StartVisit). <paramref name="ticket"/> is the master's ticket
    /// for that server when the player is signed in; <paramref name="preferredTeam"/> 0 spies, 1 mercs, 255 watch,
    /// -1 the smaller side.
    /// </summary>
    public void Join(JoinTarget target, string? password, string? ticket, int preferredTeam = -1, bool loopbackOnly = false, string? hostKey = null, string? masterId = null)
    {
        ArgumentNullException.ThrowIfNull(target);
        EndVisit();
        _visitMasterId = string.IsNullOrEmpty(masterId) ? null : masterId;
        _visitPassword = string.IsNullOrEmpty(password) ? null : password;
        _error = null;
        _stateOverride = OnlineState.Idle;
        // Read after the old visit ended, so a content folder set since then is seen.
        PlayCatalog catalog = Catalog;
        var settings = new OnlineClientSettings
        {
            PlayerName = PlayerName,
            InstallId = _installId,
            Key = _key,
            Password = string.IsNullOrEmpty(password) ? null : password,
            Ticket = ticket,
            // The listen host's own player proves with it that it is the lobby's host.
            HostKey = hostKey,
            PreferredTeam = preferredTeam,
            Maps = catalog.Maps,
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

    /// <summary>Leaves the server (it tells the others) and clears the visit: the state is Idle afterwards. A host stops hosting.</summary>
    public void Leave()
    {
        if (Client is not null)
        {
            Rejoin.LeftByChoice();
        }

        Master.CancelJoin();
        EndVisit();
        StopHosting();
        _error = null;
        _stateOverride = OnlineState.Idle;
        Touch();
    }

    private void EndVisit()
    {
        // The player's own server goes with the visit to it (OnlineService.EndVisit): a join elsewhere, a failure
        // and a leave all close it. Host() joins its server before it keeps it.
        StopHosting();
        if (Client is not { } client)
        {
            return;
        }

        StopRound();
        Voice.VoiceBridge.Existing?.EndVisit();
        client.Changed -= Touch;
        client.SessionEnding -= OnSessionEnding;
        client.MessageReceived -= OnMessage;
        client.Leave();
        client.Dispose();
        Client = null;
        _roundRefused = null;
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

    /// <summary>
    /// Whoever may change the setup (the host, an admin, anyone on an open dedicated server) picks the next match:
    /// the map's own untouched settings for that map and mode, as the lobby page's map and mode lists do
    /// (OnlineLobby.DefaultsFor in the Godot client). A null or empty map keeps the map, a negative mode the mode.
    /// False when the map is not installed here or the visit is not in the lobby.
    /// </summary>
    public bool SetMatch(string? map, int mode)
    {
        if (Client is not { State: OnlineClientState.Lobby } client)
        {
            return false;
        }

        LobbySetup now = client.Lobby.Setup;
        string id = string.IsNullOrWhiteSpace(map) ? now.Map : map.Trim();
        if (client.Maps.FindMap(id) is not { } entry || MapCatalog.IsMenuMap(entry.Id))
        {
            return false;
        }

        EGameMode picked = mode < 0 ? now.Mode : (EGameMode)(byte)mode;
        client.SetSetup(LobbySetup.ForMap(entry.Id, picked, client.Maps.GetMapSettings(entry.Id), now.MaxPlayers) with
        {
            Revenge = now.Revenge,
            GamesInARow = now.GamesInARow,
        });
        return true;
    }

    /// <summary>
    /// Changes single values of the setup: <paramref name="options"/> is an options line as the SetupOptions text
    /// gives it ("?TimeLimit=5?SvmBots=2"); the values it leaves out keep the lobby's. The server clamps and checks.
    /// False when the visit is not in the lobby.
    /// </summary>
    public bool SetSetupOptions(string? options)
    {
        if (Client is not { State: OnlineClientState.Lobby } client || string.IsNullOrWhiteSpace(options))
        {
            return false;
        }

        client.SetSetup(LobbySetup.FromOptions(options, client.Lobby.Setup));
        return true;
    }

    /// <summary>
    /// What the status says about the local player and the launch. The lobby model's own properties search the
    /// player list with a new delegate on every read, so they are read once per change, not once per frame.
    /// </summary>
    internal LobbyFacts Facts
    {
        get
        {
            if (_factsRevision != _revision)
            {
                _factsRevision = _revision;
                LobbyState? lobby = Client?.Lobby;
                LobbyPlayer? local = lobby?.Local;
                _facts = new LobbyFacts(local?.Team ?? -1, local?.Ready == true, lobby?.CanControl == true, lobby?.CanLaunch == true, lobby?.CanStart == true);
            }

            return _facts;
        }
    }

    internal readonly record struct LobbyFacts(int LocalTeam, bool LocalReady, bool CanControl, bool CanLaunch, bool CanStart);

    /// <summary>Logs in as an admin of a dedicated server.</summary>
    public void Admin(string password) => Client?.Admin(password);

    /// <summary>Host or admin: removes a player from the server (the server refuses it for anyone else, a bot or oneself).</summary>
    public void Kick(int playerId) => Client?.Kick(playerId);

    /// <summary>Host or admin: removes a player and keeps him out (his install id and address go on the server's ban list).</summary>
    public void Ban(int playerId) => Client?.Ban(playerId);

    /// <summary>Host or admin: bans a player from voice chat, or lifts it; the player's VoiceBanned flag follows.</summary>
    public void SetVoiceBanned(int playerId, bool banned) => Client?.SetVoiceBanned(playerId, banned);

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
        PumpHost();
        Browser.Pump();
        UpdateMapFetch();
        Rejoin.Pump();
        if (Client is not { } client)
        {
            return 0;
        }

        if (Round is { } drawn)
        {
            if (drawn.IsStopped)
            {
                // The main table stopped the round under the visit (its Stop, or a local match was started): nothing
                // would tick the connection any more and the server would time the player out. The player left.
                Leave();
                return 0;
            }

            // The frame drawn since the last call, as PlayRoot reports it after drawing: the server traces this
            // player's shots at the time the other players were shown at (lag compensation).
            drawn.Client.ReportRenderedFrame(drawn.Alpha);
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
        Rejoin.Follow(client, _visitMasterId, _visitPassword);

        // The round is ticked here, on the same clock the visit ticked the connection on so far (a fresh clock would
        // start behind it and the driver would see time go backwards). Also when the main session slot adopted it:
        // the visit owns the connection, and a main table that stopped calling Update (it failed) must not starve
        // it until the server times the player out. The ticks run by the clock, so the main table's own Update
        // right after this finds none due: it brings the frame's blend factor up to date and reports the ticks run here.
        if (Round is { IsStopped: false } round)
        {
            round.Update();
        }

        // Voice chat follows the visit to its current map (Voice/VoiceBridge.cs).
        Voice.VoiceBridge.Instance.AfterVisitTick(client);
        return ran;
    }

    /// <summary>OnlineService.ShowState without the screens: a round is entered on its map and left at its end.</summary>
    private void FollowState(OnlineClient client)
    {
        switch (client.State)
        {
            case OnlineClientState.Loading or OnlineClientState.Match when Round is null && client.Session is { } session:
                EnterRound(client, session);
                break;

            // The server's "phase results" line comes before the rows and the "results" line, which can land on
            // later ticks: the round stays up until they are in (the end banner shows meanwhile).
            case OnlineClientState.Results when client.Results is not null:
            case OnlineClientState.Lobby or OnlineClientState.Travelling or OnlineClientState.Failed or OnlineClientState.Left:
                if (Round is not null)
                {
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
            Side = Facts.LocalTeam == 1 ? PlaySide.Merc : PlaySide.Spy,
            Mode = MapCatalog.ModeKey(client.Lobby.Setup.Mode),
            PlayerName = PlayerName,
            MapRoots = [.. Catalog.Maps.Roots],
        };
        var round = new PlaySession(settings, _clock, session, Catalog.Maps, Catalog.Maps.FindFolder(map)!, Catalog.Loader(map));
        client.SessionTickedElsewhere = true;
        Round = round;
        _loadReported = false;
        // The round goes into the bridge's main session slot, so the per-frame entries of the main table (pawns,
        // poses, view, input) and the features read it as they read a local match. This class keeps ticking it.
        _adopted = Exports.AdoptRemote(round);
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
                Exports.Release(adopted);
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
        StopHosting();
        Master.Dispose();
        Browser.Dispose();
        Rejoin.Dispose();
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
