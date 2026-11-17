using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.UnrealBridge.Hud;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The camera Unreal draws this frame, which a ping is aimed through (<see cref="TeamPingApi.Send"/>).
/// Source/OpenSCCT/Runtime/ScctTeamPingApi.h mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct TeamPingCamera
{
    /// <summary>The camera's location in the world.</summary>
    public float X, Y, Z;

    /// <summary>The camera's rotation in degrees, as Unreal's FRotator has it.</summary>
    public float Pitch, Yaw, Roll;
}

/// <summary>Whether the local player can ping now, and what was sent and received (<see cref="TeamPingApi.GetState"/>). The header mirrors it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct TeamPingState
{
    /// <summary>
    /// A number for the player's pawn, the actor that gives his view (the pawn, his sticky cam, the camera network, the
    /// projector) and his team, the same while none of them changes, never 0; 0 while a ping cannot be placed (no
    /// match begun, dead, spectating, a deathmatch, a cinematic, the server does not take pings). A gesture begun under
    /// one number is dropped when it changes, as Godot's TeamPingInput drops it on a new context.
    /// </summary>
    public int Context;

    /// <summary>The local player's team (0 spies, 1 mercs) while he has one in a ping mode, else -1.</summary>
    public int Team;

    /// <summary>1 when the session's server offered team pings and the client took them (ClientSession.SupportsTeamPings).</summary>
    public int Supported;

    /// <summary>Pings handed to the connection, pings of the team received, and sends refused (no target, kind not allowed).</summary>
    public int Sent;

    public int Received;

    public int Refused;

    /// <summary>Where the last ping sent was aimed, and its kind.</summary>
    public float TargetX, TargetY, TargetZ;

    public int LastKind;
}

/// <summary>One teammate's ping as the HUD draws it (<see cref="TeamPingApi.GetPings"/>). The header mirrors it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct TeamPingRow
{
    /// <summary>TeamPingKind: 0 generic, 1 watch here, 2 check here, 3 co-op boost, 4 mine.</summary>
    public int Kind;

    /// <summary>The team it was placed for (0 spies, 1 mercs).</summary>
    public int Team;

    /// <summary>
    /// The sender's player slot on the server (ClientSession.PlayerId on his machine), and his name as a HUD text id
    /// (FScctHudApi::GetText). The client knows no slot's name but its own, so a teammate's ping has name 0.
    /// </summary>
    public int SenderId;

    public int SenderNameId;

    /// <summary>The marked spot in the world.</summary>
    public float X, Y, Z;

    /// <summary>Seconds since the server accepted it (it lives 4 s), and its opacity (1, fading out over the last 0.75 s).</summary>
    public float Age;

    public float Opacity;

    /// <summary>1 for the local player's own ping (the server echoes it to the whole team, the sender included).</summary>
    public int Own;
}

/// <summary>
/// The team ping's table of entry points: Godot's TeamPingHud without its drawing. Unreal asks for
/// <see cref="TeamPingExports.GetApi"/> once (<c>Host.GetFunction("OpenScct.UnrealBridge.TeamPingExports", "GetApi")</c>);
/// Source/OpenSCCT/Runtime/ScctTeamPingApi.h mirrors it field for field.
/// <para>A ping goes out on the running session's own connection (ClientSession.SendTeamPing), which in a local match is
/// the in-process server and in an online round the visit's server. The server checks it against the sender's view and
/// the world (TeamPingPlacement.TryValidateTarget), at most one a second, and sends it to every teammate whose client
/// took pings, the sender included; what comes back is kept here (one ping per sender, the newest) and handed to Unreal
/// as rows, aged by the server tick as Godot's TeamPingHud ages them.</para>
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct TeamPingApi
{
    public int Version;

    /// <summary>Fills the state; returns its Context (0 when a ping cannot be placed now).</summary>
    public delegate* unmanaged[Cdecl]<TeamPingState*, int> GetState;

    /// <summary>
    /// (kind, camera, target): aims a ping of that kind through the camera (the trace from its centre, 40 m, else 12 m
    /// ahead) and sends it; the spot goes to target (3 floats) when given. 1 sent, 0 no ping can be placed now, -1 the
    /// kind is not one of the player's team, -2 no spot was found.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, TeamPingCamera*, float*, int> Send;

    /// <summary>Writes the team's live pings, oldest first; returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<TeamPingRow*, int, int> GetPings;
}

/// <summary>
/// The team pings of one session: the marks received for the local player's team, kept until they are 4 s old, cleared
/// when his pawn or team changes (Godot's TeamPingHud.RefreshRecipient), and the context number of the gesture.
/// </summary>
public sealed class TeamPingFeature : IDisposable
{
    public const int LifetimeTicks = 4 * Sim.Hz;
    public const float FadeTicks = 0.75f * Sim.Hz;

    private readonly BridgeSession _bridge;
    private readonly PlaySession _session;
    private readonly ClientSession _client;
    private readonly Dictionary<int, (TeamPingMark Mark, long ReceivedTick)> _marks = new();
    private (SBasePawn Pawn, byte Team)? _recipient;
    private (SBasePawn Pawn, Actor Source, byte Team)? _context;
    private int _contextId;

    private TeamPingFeature(BridgeSession bridge)
    {
        _bridge = bridge;
        _session = bridge.Session;
        _client = _session.Client;
        _client.TeamPingReceived += Receive;
    }

    public static TeamPingFeature Of(BridgeSession session) => session.Feature(static s => new TeamPingFeature(s));

    public int Sent { get; private set; }

    public int Received { get; private set; }

    public int Refused { get; private set; }

    public FVector LastTarget { get; private set; }

    public TeamPingKind LastKind { get; private set; }

    public void Dispose() => _client.TeamPingReceived -= Receive;

    /// <summary>The player and the actor giving his view while a ping can be placed (Godot's TeamPingHud.PlacementSource, less the HUD's own blocks, which Unreal checks).</summary>
    private (SPlayerController Player, Actor Source, byte Team)? PlacementSource()
    {
        if (_session.IsStopped || _session.ViewOverride is not null || !_session.View.Valid ||
            !_client.SupportsTeamPings || _client.State != EClientState.Joined ||
            _session.LocalController is not { GameReplicationInfo.bMatchHasBegun: true } player ||
            !TeamPingPlacement.TryGetTeam(player, out byte team) ||
            !TeamPingPlacement.TryGetSource(player, out Actor source, out _))
        {
            return null;
        }

        return (player, source, team);
    }

    /// <summary>The context number (see <see cref="TeamPingState.Context"/>): a new number each time the pawn, the view's source or the team changes.</summary>
    public int Context()
    {
        if (PlacementSource() is not { } source || source.Player.SBPawn is not { } pawn)
        {
            _context = null;
            return 0;
        }

        var context = (pawn, source.Source, source.Team);
        if (_context is not { } last || !ReferenceEquals(last.Pawn, pawn) || !ReferenceEquals(last.Source, source.Source) || last.Team != source.Team)
        {
            _context = context;
            _contextId = _contextId == int.MaxValue ? 1 : _contextId + 1;
        }

        return _contextId;
    }

    public TeamPingState State()
    {
        int context = Context();
        return new TeamPingState
        {
            Context = context,
            Team = context != 0 && _context is { } c ? c.Team : -1,
            Supported = _client.SupportsTeamPings ? 1 : 0,
            Sent = Sent,
            Received = Received,
            Refused = Refused,
            TargetX = LastTarget.X,
            TargetY = LastTarget.Y,
            TargetZ = LastTarget.Z,
            LastKind = (int)LastKind,
        };
    }

    /// <summary>
    /// Aims and sends a ping: the spot is where the camera's centre meets the world when it is sent (Godot's
    /// TeamPingHud.Submit: a wheel held open while moving marks where the player aims at the release).
    /// </summary>
    public int Send(TeamPingKind kind, in TeamPingCamera camera, out FVector target)
    {
        target = default;
        if (PlacementSource() is not { } source)
        {
            return 0;
        }

        if (!TeamPingPlacement.IsAllowed(kind, source.Team))
        {
            Refused++;
            return -1;
        }

        // Unreal's degrees to the simulation's 65536 units a turn; both share the world's axes.
        const float UnitsPerDegree = 65536f / 360f;
        var rotation = new FRotator((int)MathF.Round(camera.Pitch * UnitsPerDegree), (int)MathF.Round(camera.Yaw * UnitsPerDegree),
            (int)MathF.Round(camera.Roll * UnitsPerDegree));
        if (!TeamPingPlacement.TryProjectTarget(source.Player, source.Source, new FVector(camera.X, camera.Y, camera.Z), rotation, out target))
        {
            Refused++;
            return -2;
        }

        _client.SendTeamPing(kind, source.Source, target);
        Sent++;
        LastTarget = target;
        LastKind = kind;
        return 1;
    }

    private void RefreshRecipient()
    {
        SPlayerController? player = _session.LocalController;
        (SBasePawn Pawn, byte Team)? recipient = !_session.IsStopped && _client.State == EClientState.Joined &&
            player is { GameReplicationInfo.bMatchHasBegun: true, SBPawn: { } pawn } &&
            TeamPingPlacement.TryGetTeam(player, out byte team) ? (pawn, team) : null;
        if (_recipient?.Pawn != recipient?.Pawn || _recipient?.Team != recipient?.Team)
        {
            _recipient = recipient;
            _marks.Clear();
        }
    }

    private void Receive(TeamPingMark mark)
    {
        RefreshRecipient();
        if (_recipient?.Team != mark.Team ||
            (_marks.TryGetValue(mark.SenderId, out var previous) && previous.Mark.ServerTick >= mark.ServerTick))
        {
            return;
        }

        _marks[mark.SenderId] = (mark, _client.LocalTick);
        Received++;
    }

    /// <summary>The live marks as rows, oldest first; the expired ones are dropped.</summary>
    public int Rows(Span<TeamPingRow> rows)
    {
        if (_marks.Count == 0)
        {
            return 0;
        }

        RefreshRecipient();
        long localTick = _client.LocalTick;
        NetDriver driver = _client.Driver;
        long serverTick = driver.Timeline.IsValid
            ? Math.Max(driver.NewestServerTick, driver.Timeline.RenderTimeFixed(localTick, delayTicks: 0) / NetTimeline.Fraction)
            : driver.NewestServerTick;
        // The sender is the server's player slot (the id its Welcome gave each connection), not a replicated player id.
        int localId = _client.PlayerId;
        string? localName = _session.LocalController?.PlayerReplicationInfo?.PlayerName;

        var expired = new List<int>();
        var live = new List<(TeamPingMark Mark, long Age)>();
        foreach ((int sender, var active) in _marks)
        {
            long age = Math.Max(localTick - active.ReceivedTick, serverTick - active.Mark.ServerTick);
            if (age >= LifetimeTicks)
            {
                expired.Add(sender);
            }
            else
            {
                live.Add((active.Mark, Math.Max(age, 0)));
            }
        }

        foreach (int sender in expired)
        {
            _marks.Remove(sender);
        }

        live.Sort((a, b) => b.Age.CompareTo(a.Age));
        int written = 0;
        HudTextTable texts = HudFeature.Of(_bridge).Texts;
        foreach ((TeamPingMark mark, long age) in live)
        {
            if (written >= rows.Length)
            {
                break;
            }

            rows[written++] = new TeamPingRow
            {
                Kind = (int)mark.Kind,
                Team = mark.Team,
                SenderId = mark.SenderId,
                SenderNameId = mark.SenderId == localId ? texts.Id(localName) : 0,
                X = mark.Location.X,
                Y = mark.Location.Y,
                Z = mark.Location.Z,
                Age = age / (float)Sim.Hz,
                Opacity = Math.Min(1f, (LifetimeTicks - age) / FadeTicks),
                Own = mark.SenderId == localId ? 1 : 0,
            };
        }

        return written;
    }
}

public static unsafe class TeamPingExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the team ping API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(TeamPingApi* api)
    {
        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->Send = &Send;
        api->GetPings = &GetPings;
        return ApiVersion;
    }

    private static TeamPingFeature? Feature => Exports.Current is { IsDisposed: false } session ? TeamPingFeature.Of(session) : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(TeamPingState* state)
    {
        try
        {
            TeamPingState read = Feature?.State() ?? new TeamPingState { Team = -1 };
            if (state != null)
            {
                *state = read;
            }

            return read.Context;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Send(int kind, TeamPingCamera* camera, float* target)
    {
        try
        {
            if (camera == null || kind < 0 || kind > (int)TeamPingKind.Mine || Feature is not { } feature)
            {
                return 0;
            }

            int result = feature.Send((TeamPingKind)kind, *camera, out FVector spot);
            if (target != null)
            {
                target[0] = spot.X;
                target[1] = spot.Y;
                target[2] = spot.Z;
            }

            return result;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPings(TeamPingRow* rows, int capacity)
    {
        try
        {
            return rows == null || capacity <= 0 || Feature is not { } feature ? 0 : feature.Rows(new Span<TeamPingRow>(rows, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
