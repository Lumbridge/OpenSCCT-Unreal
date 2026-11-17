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
/// The test checks' view of a session's pawns (Test/TestExports.cs). It keeps each client pawn's replicated location
/// of the last tick and of the tick before, by the same rule the drawn snapshot follows (BridgeSession.GetPawns: the
/// two only differ when the pawn was walked, not placed or reset), so a check can tell whether a body Unreal drew lies
/// where the simulation had it. It is made as each session starts, so it has both ticks from the second tick on.
/// </summary>
public sealed class TestPawnTrack : IDisposable
{
    /// <summary>A pawn that moved farther than this in one tick was placed, not walked (BridgeSession's rule).</summary>
    private const float CutDistanceSquared = 40000f;

    private readonly BridgeSession _session;
    private readonly Dictionary<int, Sample> _current = new();
    private readonly Dictionary<int, Sample> _previous = new();

    public TestPawnTrack(BridgeSession session)
    {
        _session = session;
        _session.Ticked += OnTicked;
    }

    public static TestPawnTrack Of(BridgeSession session) => session.Feature(s => new TestPawnTrack(s));

    public void Dispose() => _session.Ticked -= OnTicked;

    private void OnTicked()
    {
        _previous.Clear();
        foreach (KeyValuePair<int, Sample> pair in _current)
        {
            _previous[pair.Key] = pair.Value;
        }

        _current.Clear();
        foreach (SBasePawn pawn in _session.Session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe)
            {
                _current[pawn.ActorId] = new Sample(pawn.Location + pawn.NetworkCorrectionOffset, pawn.NetworkResetId);
            }
        }
    }

    /// <summary>
    /// Writes every pawn the client has (with the server's state when it can be read: a stopped or inline server) and
    /// every server pawn the client does not have. Returns the count written.
    /// </summary>
    public int Read(Span<BridgeTestPawn> pawns)
    {
        PlaySession play = _session.Session;
        ClientSession client = play.Client;
        SBasePawn? local = play.LocalPawn;
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

            FVector now = _current.TryGetValue(pawn.ActorId, out Sample current) ? current.Location : pawn.Location + pawn.NetworkCorrectionOffset;
            FVector before = _previous.TryGetValue(pawn.ActorId, out Sample previous) && previous.ResetId == current.ResetId &&
                (previous.Location - now).SizeSquared() <= CutDistanceSquared
                ? previous.Location
                : now;
            pawns[count++] = new BridgeTestPawn
            {
                ServerActorId = (int)channel.NetGuid,
                ClientActorId = pawn.ActorId,
                Flags = BridgeTestPawnFlags.OnClient | (ReferenceEquals(pawn, local) ? BridgeTestPawnFlags.Local : 0),
                MovesApplied = -1,
                ClientX = now.X,
                ClientY = now.Y,
                ClientZ = now.Z,
                PreviousX = before.X,
                PreviousY = before.Y,
                PreviousZ = before.Z,
            };
        }

        if (!TestExports.ServerReadable(play))
        {
            return count;
        }

        int clientCount = count;
        foreach (ServerPawn pawn in play.ServerSnapshot(host => ServerPawns(host, play.ServerController)))
        {
            int index = -1;
            for (int i = 0; i < clientCount; i++)
            {
                if (pawns[i].ServerActorId == pawn.ActorId)
                {
                    index = i;
                    break;
                }
            }

            if (index < 0)
            {
                if (count >= pawns.Length)
                {
                    continue;
                }

                index = count++;
                pawns[index] = new BridgeTestPawn { ServerActorId = pawn.ActorId };
            }

            ref BridgeTestPawn entry = ref pawns[index];
            entry.Flags |= BridgeTestPawnFlags.OnServer | (pawn.Bot ? BridgeTestPawnFlags.Bot : 0);
            entry.MovesApplied = pawn.MovesApplied;
            entry.ServerX = pawn.Location.X;
            entry.ServerY = pawn.Location.Y;
            entry.ServerZ = pawn.Location.Z;
        }

        return count;
    }

    /// <summary>The server's living pawns; called on the server's state through PlaySession.ServerSnapshot.</summary>
    private static List<ServerPawn> ServerPawns(ServerHost host, Actor? localPlayer)
    {
        var list = new List<ServerPawn>();
        foreach (SBasePawn pawn in host.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            int moves = -1;
            if (pawn.Controller is PlayerController controller && host.Movement.EndpointOf(controller)?.Server is { } server)
            {
                moves = (int)Math.Min(server.MovesApplied, int.MaxValue);
            }

            // In a local match every player but the one at this machine is a server bot.
            list.Add(new ServerPawn(pawn.ActorId, pawn.Location, localPlayer is null || !ReferenceEquals(pawn.Controller, localPlayer), moves));
        }

        return list;
    }

    private readonly record struct ServerPawn(int ActorId, FVector Location, bool Bot, int MovesApplied);

    /// <summary>Pawns the client has a snapshot of.</summary>
    public int ClientPawns => _current.Count;

    private readonly record struct Sample(FVector Location, uint ResetId);
}
