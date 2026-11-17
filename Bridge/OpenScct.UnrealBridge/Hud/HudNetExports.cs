using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Net;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The running totals of the local player's link to the server (<see cref="HudNetApi.GetState"/>), Godot's NetReadout
/// as PlayRoot.ReadNet fills it. Totals, not rates: the HUD works the rates out over its own one-second window, as Godot's
/// HudNet does, and starts over when a total goes down (a new connection). Source/OpenSCCT/Runtime/ScctHudNetApi.h mirrors
/// it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudNetState
{
    /// <summary>1 while the session has a connection to its server.</summary>
    public int Connected;

    /// <summary>1 once the connection has measured a round trip (a loopback to a local host never does).</summary>
    public int HasRtt;

    /// <summary>1 on a listen host: the player's own connection is the local loopback.</summary>
    public int IsHost;

    /// <summary>The server's send rate in snapshots a second.</summary>
    public int ServerNetRate;

    public long RttMicros;
    public long JitterMicros;
    public long InBytes;
    public long OutBytes;
    public long InPackets;
    public long InPacketsLost;
    public long OutPackets;
    public long OutPacketsLost;

    /// <summary>Position corrections the client's prediction applied (ClientMoves.CorrectionsApplied).</summary>
    public long Corrections;

    /// <summary>On a host: the other players with a measured ping (PlayerReplicationInfo.Ping above 0), the best and worst.</summary>
    public int Clients;
    public int BestClientPingMs;
    public int WorstClientPingMs;

    /// <summary>On a host: the other players without a ping (bots: the host measures none for them).</summary>
    public int Unmeasured;

    /// <summary>The interpolation delay the client draws other actors behind the server, in ms.</summary>
    public float BufferMs;

    /// <summary>How far the last correction moved the player's pawn, in Unreal units.</summary>
    public float CorrectionDistance;

    /// <summary>How old the newest pose snapshot is at the drawn moment, in ms.</summary>
    public float SnapshotAgeMs;

    public int Reserved;

    /// <summary>The presentation clock ran out of snapshots to draw from.</summary>
    public long BufferUnderflows;
    public long MovesReplayed;

    /// <summary>Pose snapshots that named a body the client had no descriptor for.</summary>
    public long AnimationGaps;
    public long PoseResets;

    /// <summary>The owner's sequenced move history was reset.</summary>
    public long PredictionResets;
    public long StateGaps;
}

/// <summary>
/// The network readout's table of entry points (Godot's PlayRoot.ReadNet feeding HudNet). Unreal asks for
/// <see cref="HudNetExports.GetApi"/> once (<c>Host.GetFunction("OpenScct.UnrealBridge.HudNetExports", "GetApi")</c>) and
/// reads the totals once a frame; the frame-rate readout is Unreal's own (its frame times).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudNetApi
{
    public int Version;

    /// <summary>Fills the totals; returns 1 while a session with a connection runs, 0 otherwise (the state is then zeroed).</summary>
    public delegate* unmanaged[Cdecl]<HudNetState*, int> GetState;
}

public static unsafe class HudNetExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the network readout API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudNetApi* api)
    {
        api->Version = ApiVersion;
        api->GetState = &GetState;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(HudNetState* state)
    {
        try
        {
            HudNetState read = default;
            int result = Read(ref read);
            if (state != null)
            {
                *state = read;
            }

            return result;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            if (state != null)
            {
                *state = default;
            }

            return 0;
        }
    }

    /// <summary>PlayRoot.ReadNet, field for field (main thread, between client ticks).</summary>
    private static int Read(ref HudNetState s)
    {
        if (Exports.Current is not { IsDisposed: false } bridge || bridge.Session is not { IsStopped: false } session ||
            session.Client.Connection?.Stats is not { } stats)
        {
            return 0;
        }

        var client = session.Client;
        PlayerController? local = session.LocalController;
        var endpoint = local is not null ? client.Movement.PeekEndpoint(local) : null;
        s.Connected = 1;
        s.HasRtt = stats.HasRtt ? 1 : 0;
        s.RttMicros = stats.RttMicros;
        s.JitterMicros = stats.JitterMicros;
        s.InBytes = stats.InBytes;
        s.OutBytes = stats.OutBytes;
        s.InPackets = stats.InPackets;
        s.InPacketsLost = stats.InPacketsLost;
        s.OutPackets = stats.OutPackets;
        s.OutPacketsLost = stats.OutPacketsLost;
        s.Corrections = endpoint?.Client?.CorrectionsApplied ?? 0;
        s.ServerNetRate = client.ServerNetRate;

        // A listen host's own player is on loopback: its line is the other players' pings (their PRI.Ping, set by the
        // server from each connection's round trip; bots and the host itself have none).
        bool host = client.Connection.IsLocal;
        s.IsHost = host ? 1 : 0;
        if (host && (local?.GameReplicationInfo ?? client.World.Level.Info.GRI) is { } gri)
        {
            int best = int.MaxValue;
            PlayerReplicationInfo? own = local?.PlayerReplicationInfo;
            foreach (PlayerReplicationInfo? pri in gri.PRIArray)
            {
                if (pri is null || pri.bDeleteMe || ReferenceEquals(pri, own) || pri.bOnlySpectator)
                {
                    continue;
                }

                if (pri.Ping > 0)
                {
                    // PlayerReplicationInfo.Ping is in ms (Godot's HudK.PingUnit is 1).
                    s.Clients++;
                    best = Math.Min(best, pri.Ping);
                    s.WorstClientPingMs = Math.Max(s.WorstClientPingMs, pri.Ping);
                }
                else
                {
                    s.Unmeasured++;
                }
            }

            s.BestClientPingMs = s.Clients > 0 ? best : 0;
        }

        s.BufferMs = client.InterpolationDelayMs;
        s.BufferUnderflows = client.Presentation.Underflows;
        s.MovesReplayed = endpoint?.Client?.MovesReplayed ?? 0;
        s.CorrectionDistance = endpoint?.Client?.LastCorrectionDistance ?? 0f;
        s.AnimationGaps = client.PawnStates?.MissingDescriptors ?? 0;
        s.SnapshotAgeMs = client.PawnStates is { LatestSnapshotTick: >= 0 } poses
            ? Math.Max(0, client.Driver.Timeline.RenderTimeFixed(client.LocalTick, 0) - poses.LatestSnapshotTick * NetTimeline.Fraction) * (Sim.DeltaTime * 1000f / NetTimeline.Fraction)
            : 0f;
        s.PoseResets = client.PawnStates?.Resets ?? 0;
        s.PredictionResets = endpoint?.Sequenced?.HistoryResets ?? 0;
        s.StateGaps = client.PawnStates?.StateGaps ?? 0;
        return 1;
    }
}
