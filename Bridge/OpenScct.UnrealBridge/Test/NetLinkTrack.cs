using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Test;

/// <summary>
/// What a netcode check (Test/NetLinkLaunch.cs) reads of its match after every client tick: the round trip the client
/// measures, the resets of each pawn the client has seen (a reinitialisation or a placement: the one case a drawn body
/// may jump), and where the server had each pawn over the last <see cref="HistoryTicks"/> ticks, so the client's copy
/// can be compared with the nearest of them (a remote pawn is drawn some 100 ms or more behind the server).
/// </summary>
public sealed class NetLinkTrack : IDisposable
{
    /// <summary>Server ticks of each pawn's history: 1.5 s, more than the 300 ms link plus the interpolation delay.</summary>
    public const int HistoryTicks = 90;

    private readonly BridgeSession _session;
    private readonly Dictionary<int, Queue<FVector>> _serverHistory = new();
    private readonly Dictionary<int, (uint ResetId, int Resets)> _clientResets = new();
    private readonly List<int> _gone = new();
    private readonly HashSet<int> _seen = new();
    private readonly HashSet<NetConnection> _serverConnections = new(ReferenceEqualityComparer.Instance);

    /// <summary>Why the server closed a player's connection (the first one it closed), or null.</summary>
    public string? ServerCloseReason { get; private set; }

    public NetLinkTrack(BridgeSession session)
    {
        _session = session;
        _session.Ticked += OnTicked;
    }

    public static NetLinkTrack Of(BridgeSession session) => session.Feature(s => new NetLinkTrack(s));

    public void Dispose() => _session.Ticked -= OnTicked;

    /// <summary>The client tick the local player first had a pawn on, or -1.</summary>
    public long JoinedTick { get; private set; } = -1;

    /// <summary>Resets of any pawn the client has seen since the join.</summary>
    public long ClientResets { get; private set; }

    public float RttMs { get; private set; }

    public float MinRttMs { get; private set; }

    private void OnTicked()
    {
        PlaySession play = _session.Session;
        ClientSession client = play.Client;
        if (JoinedTick < 0 && play.LocalPawn is not null)
        {
            JoinedTick = _session.Ticks;
        }

        if (client.Connection?.Stats is { HasRtt: true } stats)
        {
            RttMs = stats.RttMicros / 1000f;
            MinRttMs = stats.MinimumRttMicros / 1000f;
        }

        _seen.Clear();
        foreach (SBasePawn pawn in client.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            _seen.Add(pawn.ActorId);
            if (_clientResets.TryGetValue(pawn.ActorId, out var known))
            {
                if (known.ResetId != pawn.NetworkResetId)
                {
                    _clientResets[pawn.ActorId] = (pawn.NetworkResetId, known.Resets + 1);
                    ClientResets += JoinedTick >= 0 ? 1 : 0;
                }
            }
            else
            {
                _clientResets[pawn.ActorId] = (pawn.NetworkResetId, 0);
            }
        }

        _gone.Clear();
        foreach (int id in _clientResets.Keys)
        {
            if (!_seen.Contains(id))
            {
                _gone.Add(id);
            }
        }

        foreach (int id in _gone)
        {
            _clientResets.Remove(id);
        }

        if (NetLinkLaunch.Current is not { } run || !ReferenceEquals(run.Session, _session) || run.Server is not { } server)
        {
            return;
        }

        // The server's side of each player's connection, kept so the reason the server closed one can be read after
        // the server let go of the player.
        foreach (PlayerSlot slot in server.Players)
        {
            _serverConnections.Add(slot.Connection);
        }

        foreach (NetConnection connection in _serverConnections)
        {
            if (connection.State == EConnectionState.USOCK_Closed && ServerCloseReason is null)
            {
                ServerCloseReason = connection.CloseReason ?? "closed";
            }
        }

        _seen.Clear();
        foreach (SBasePawn pawn in server.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            _seen.Add(pawn.ActorId);
            if (!_serverHistory.TryGetValue(pawn.ActorId, out Queue<FVector>? history))
            {
                history = new Queue<FVector>(HistoryTicks + 1);
                _serverHistory[pawn.ActorId] = history;
            }

            history.Enqueue(pawn.Location);
            while (history.Count > HistoryTicks)
            {
                history.Dequeue();
            }
        }

        _gone.Clear();
        foreach (int id in _serverHistory.Keys)
        {
            if (!_seen.Contains(id))
            {
                _gone.Add(id);
            }
        }

        foreach (int id in _gone)
        {
            _serverHistory.Remove(id);
        }
    }

    /// <summary>
    /// Writes every pawn the client has and every server pawn it lacks: the server's location now, the client's
    /// (with its correction offset, as drawn), the resets the client saw and the drift. Returns the count written.
    /// </summary>
    public int Read(NetLinkRun run, Span<BridgeNetPawn> pawns)
    {
        PlaySession play = _session.Session;
        ClientSession client = play.Client;
        SBasePawn? local = play.LocalPawn;
        ServerHost? server = run.Server;
        PlayerController? serverPlayer = run.ServerPlayer;
        int count = 0;
        foreach (ActorChannel channel in client.ActorChannels)
        {
            if (count >= pawns.Length)
            {
                break;
            }

            if (channel.Actor is not SBasePawn { bDeleteMe: false } pawn)
            {
                continue;
            }

            FVector at = pawn.Location + pawn.NetworkCorrectionOffset;
            int serverId = (int)channel.NetGuid;
            ref BridgeNetPawn entry = ref pawns[count++];
            entry = new BridgeNetPawn
            {
                ServerActorId = serverId,
                ClientActorId = pawn.ActorId,
                Flags = BridgeNetPawnFlags.OnClient | (ReferenceEquals(pawn, local) ? BridgeNetPawnFlags.Local : 0),
                Resets = _clientResets.TryGetValue(pawn.ActorId, out var resets) ? resets.Resets : 0,
                ClientX = at.X,
                ClientY = at.Y,
                ClientZ = at.Z,
                Drift = -1f,
            };

            if (server?.World.Level.FindActor(serverId) is SBasePawn { bDeleteMe: false } source)
            {
                SetServer(ref entry, source, serverPlayer);
                if (_serverHistory.TryGetValue(serverId, out Queue<FVector>? history))
                {
                    float best = float.MaxValue;
                    foreach (FVector was in history)
                    {
                        best = MathF.Min(best, (was - at).SizeSquared());
                    }

                    entry.Drift = history.Count > 0 ? MathF.Sqrt(best) : -1f;
                }
            }
        }

        if (server is null)
        {
            return count;
        }

        int clientCount = count;
        foreach (SBasePawn source in server.World.Level.AllActors<SBasePawn>())
        {
            if (source.bDeleteMe || count >= pawns.Length)
            {
                continue;
            }

            bool known = false;
            for (int i = 0; i < clientCount && !known; i++)
            {
                known = pawns[i].ServerActorId == source.ActorId;
            }

            if (!known)
            {
                ref BridgeNetPawn entry = ref pawns[count++];
                entry = new BridgeNetPawn { ServerActorId = source.ActorId, Drift = -1f };
                SetServer(ref entry, source, serverPlayer);
            }
        }

        return count;
    }

    private static void SetServer(ref BridgeNetPawn entry, SBasePawn source, PlayerController? serverPlayer)
    {
        entry.Flags |= BridgeNetPawnFlags.OnServer |
            (serverPlayer is null || !ReferenceEquals(source.Controller, serverPlayer) ? BridgeNetPawnFlags.Bot : 0);
        entry.ServerX = source.Location.X;
        entry.ServerY = source.Location.Y;
        entry.ServerZ = source.Location.Z;
    }
}
