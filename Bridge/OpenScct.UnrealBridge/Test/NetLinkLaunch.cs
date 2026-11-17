using System;
using System.Collections.Generic;
using OpenScct.Bots;
using OpenScct.Content;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Test;

/// <summary>
/// How the netcode checks' matches are started (the Godot client's PlayNetcodeSmoke, PlayGrabNetcodeSmoke and
/// PlayCoopNetcodeSmoke): over a simulated link with latency, jitter, loss, duplication and reordering
/// (<see cref="SimLinkProfile"/>) instead of loopback, with OpenSCCT's tuned net profile or the original's.
/// <para>A listen match is the usual local host ticked inline, its server and the local client on the two ends of the
/// link. A dedicated match is an isolated server with three bot clients of its own (each a full client over the same
/// link) that the local client joins as one more player, as PlayNetcodeSmoke builds it; the session Unreal draws is
/// then a round on that server, not its host.</para>
/// <para>Unreal sets the link (<c>TestExports.SetNextMatchLink</c>) just before the level that starts the match opens;
/// the next local start takes it (<see cref="Apply"/>) and uses it up.</para>
/// </summary>
public static class NetLinkLaunch
{
    private static NetLinkRun? s_pending;
    private static NetLinkRun? s_waiting;

    /// <summary>The run of the match started last with a link, until that match ends.</summary>
    public static NetLinkRun? Current { get; private set; }

    /// <summary>Sets the link of the next local start. False with the reason for an unknown profile or scenario.</summary>
    public static bool Set(string profile, bool stock, bool dedicated, string scenario, out string error)
    {
        error = "";
        SimLinkProfile link;
        try
        {
            link = SimLinkProfile.Get(profile);
        }
        catch (ArgumentOutOfRangeException)
        {
            error = $"unknown link profile '{profile}' (A, B, C, D, 100, 150, 200 or 300)";
            return false;
        }

        scenario = scenario.Length == 0 ? NetLinkRun.Walk : scenario;
        if (scenario is not (NetLinkRun.Walk or NetLinkRun.Grab or NetLinkRun.Coop))
        {
            error = $"unknown scenario '{scenario}' (walk, grab or coop)";
            return false;
        }

        if (scenario != NetLinkRun.Walk && !dedicated)
        {
            error = $"the {scenario} scenario needs bot clients of their own: a dedicated server";
            return false;
        }

        s_pending = new NetLinkRun(link, stock ? NetProfile.Stock : NetProfile.Enhanced, dedicated, scenario);
        return true;
    }

    /// <summary>
    /// The match over the armed link, when one is armed; the match unchanged otherwise. Either way the armed link is
    /// used up.
    /// </summary>
    public static LocalMatch Apply(LocalMatch match)
    {
        ArgumentNullException.ThrowIfNull(match);
        NetLinkRun? run = s_pending;
        s_pending = null;
        s_waiting = run;
        if (run is null)
        {
            return match;
        }

        match = match with { SimLink = run.Link, NetworkProfile = run.Profile, InlineHost = true };
        if (!run.Dedicated)
        {
            return match;
        }

        return match with
        {
            Merc = run.Scenario == NetLinkRun.Coop,
            Bots = 0,
            CreateSession = run.CreateDedicated,
        };
    }

    /// <summary>Called for every session the bridge starts: the one a run was armed for gets its track.</summary>
    internal static void OnSessionStarted(BridgeSession session)
    {
        NetLinkRun? run = s_waiting;
        s_waiting = null;
        if (run is null)
        {
            return;
        }

        run.Attach(session);
        Current = run;
    }

    /// <summary>Called for every session that ends: the run's own server and clients end with it.</summary>
    internal static void OnSessionEnded(BridgeSession session)
    {
        if (Current is { } run && ReferenceEquals(run.Session, session))
        {
            Current = null;
            run.Dispose();
        }
    }
}

/// <summary>One netcode check's match: its link, its dedicated server and bots when it has them, its scenario.</summary>
public sealed class NetLinkRun : IDisposable
{
    public const string Walk = "walk", Grab = "grab", Coop = "coop";

    /// <summary>The map PlayNetcodeSmoke's dedicated server plays (its bot scripts are written for it).</summary>
    public const string DedicatedMap = "SteeE";

    private const string ServerAddress = "server";
    private const string GameOptions = "?GameMode=5?Editeur=true";

    private BotMatch? _match;
    private ClientSession? _client;
    private SimLinkNetwork? _network;
    private NetLinkScenario? _scenario;

    public NetLinkRun(SimLinkProfile link, NetProfile profile, bool dedicated, string scenario)
    {
        Link = link;
        Profile = profile;
        Dedicated = dedicated;
        Scenario = scenario;
    }

    public SimLinkProfile Link { get; }

    public NetProfile Profile { get; }

    public bool Dedicated { get; }

    public string Scenario { get; }

    public BridgeSession? Session { get; private set; }

    /// <summary>The dedicated server's match (server and bots), or null for a listen match.</summary>
    public BotMatch? Match => _match;

    /// <summary>The scripted grab or co-op move, or null for the walk.</summary>
    public NetLinkScenario? Script => _scenario;

    /// <summary>The simulated network the match runs on (the listen host's or the dedicated server's).</summary>
    public SimLinkNetwork? Network => _network ?? Session?.Session.SimLink;

    /// <summary>
    /// The server of the match: the dedicated one, or the listen host (ticked inline, so it can be read between
    /// ticks). Null before the session starts.
    /// </summary>
    public ServerHost? Server => _match?.Server ?? (Session is { IsRemote: false } s ? s.Session.Host : null);

    /// <summary>The server's controller of the local player.</summary>
    public PlayerController? ServerPlayer
    {
        get
        {
            if (_match is null)
            {
                return Session?.Session.ServerController;
            }

            foreach (PlayerSlot slot in _match.Server.Players)
            {
                if (slot.Name == LocalName)
                {
                    return slot.Controller;
                }
            }

            return null;
        }
    }

    private const string LocalName = "Player";

    internal void Attach(BridgeSession session)
    {
        Session = session;
        NetLinkTrack.Of(session);
    }

    /// <summary>
    /// The dedicated match, as PlayNetcodeSmoke builds it: an isolated server on its own catalog and navigation, its
    /// bot clients and the local client bound to one simulated network, and the session Unreal draws a round on that
    /// server whose ticks step the server and the bots first.
    /// </summary>
    internal PlaySession CreateDedicated(PlaySettings settings, IClock clock)
    {
        settings = settings with { Map = DedicatedMap, Mode = "Story", Rules = PlayRules.PlayLevel, Bots = 0, NetworkProfile = Profile };
        MapCatalog serverMaps = MapCatalog.CreateDefault();
        var nav = new NavCache();
        nav.Prepare(serverMaps, settings.Map, terminalsBlockPlayers: false);
        _network = new SimLinkNetwork(clock, 1, Link);
        _scenario = Scenario switch
        {
            Grab => new GrabScenario(),
            Coop => new CoopScenario(),
            _ => null,
        };
        _match = new BotMatch(new BotMatchSettings
        {
            Map = settings.Map,
            Seed = 1,
            GameOptions = GameOptions,
            Bots = _scenario?.Bots ?? WalkBots(),
            Maps = serverMaps,
            Nav = nav,
            Profile = Profile,
            TerminalsBlockPlayers = false,
        }, _network.Bind, ServerAddress, clock.NowMicroseconds);
        try
        {
            MapCatalog clientMaps = MapCatalog.CreateDefault();
            string folder = clientMaps.FindFolder(settings.Map) ?? throw new System.IO.FileNotFoundException(settings.Map);
            var loader = new PlayMapLoader(SvMap.Load(folder), GameOptions, PlayMapLoader.FindCharsDir(folder));
            loader.Register(clientMaps);
            _client = new ClientSession(_network.Bind("rendered-player"), ServerAddress, clock.NowMicroseconds,
                new ClientSettings
                {
                    PlayerName = LocalName,
                    LoginOptions = settings.Side == PlaySide.Merc ? "?Team=1" : "?Team=0",
                    Maps = clientMaps,
                    Gameplay = true,
                    InstallGameplayServices = false,
                }, ownsTransport: true);
            PlaySession play = null!;
            play = new PlaySession(settings, clock, _client, clientMaps, folder, loader, now =>
            {
                if (_match is not { } match)
                {
                    return;
                }

                match.Step(now);
                _scenario?.Step(match, play);
            });
            return play;
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    /// <summary>The walk's bots: three of the SteeE story's scripted players (one spy, two mercenaries) moving about.</summary>
    private static IReadOnlyList<BotSpec> WalkBots()
    {
        IReadOnlyList<BotSpec> scripts = StoryScripts.Bots("spies");
        return [scripts[1], scripts[2], scripts[3]];
    }

    /// <summary>Bots that resynchronised their moves or left; the checks want none.</summary>
    public int BotProblems(out int joined)
    {
        joined = 0;
        int problems = 0;
        if (_match is null)
        {
            return 0;
        }

        foreach (BotClient bot in _match.Bots)
        {
            bool ok = bot.Session.State == EClientState.Joined && bot.Controller is { } controller &&
                bot.Session.Movement.PeekEndpoint(controller)?.Sequenced is { ResynchronizationRequests: 0 };
            joined += bot.Session.State == EClientState.Joined ? 1 : 0;
            problems += ok ? 0 : 1;
        }

        return problems;
    }
    /// <summary>The local client's connection as text, for a check whose player never joined or lost the match.</summary>
    public string Describe()
    {
        if (Session is not { } session)
        {
            return "no session yet";
        }

        ClientSession client = session.Session.Client;
        NetConnection? connection = client.Connection;
        int players = Server?.Players.Count ?? -1;
        return $"client {client.State}, connection {connection?.State.ToString() ?? "none"}" +
            (connection?.CloseReason is { } reason ? $" closed: {reason}" : "") +
            (client.RejectReason is { } reject ? $", refused: {reject}" : "") +
            $", server players {players}" + (NetLinkTrack.Of(session).ServerCloseReason is { } closed ? $", the server closed a player: {closed}" : "") + (session.Failure is { } failure ? $", failure: {failure}" : "");
    }

    public void Dispose()
    {
        _match?.Dispose();
        _match = null;
        _client?.Dispose();
        _client = null;
    }
}
