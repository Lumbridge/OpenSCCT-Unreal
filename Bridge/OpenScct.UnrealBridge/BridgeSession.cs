using System;
using System.Collections.Generic;
using System.Diagnostics;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The one play session Unreal drives: a local match (the dedicated server's code on its own thread, with bots) that
/// the local client joins. Unreal calls <see cref="Update"/> once a frame on its game thread and then reads the
/// drawn state; nothing here is touched from another thread.
/// </summary>
public sealed class BridgeSession : IDisposable
{
    public const int ApiVersion = 1;

    /// <summary>A view jump longer than this is drawn as a cut, not blended (as the Godot client).</summary>
    private const float ViewCutDistance = 128f;

    /// <summary>A pawn that moved farther than this in one tick was placed, not walked: no blend.</summary>
    private const float PawnCutDistanceSquared = 40000f;

    private readonly PlaySession _session;
    private readonly Dictionary<int, PawnSample> _previous = new();
    private readonly Dictionary<int, PawnSample> _current = new();
    private readonly List<int> _order = new();

    private BridgeSession(PlaySession session)
    {
        _session = session;
        _session.AfterClientTick = (_, _) => Snapshot();
    }

    /// <summary>Sets the converted content folder (the one holding <c>svmap</c> and <c>shared</c>).</summary>
    public static void Configure(string contentRoot)
    {
        ContentPaths.Configure(contentRoot, exeDir: null);
    }

    /// <summary>Hosts <paramref name="map"/> locally and joins it. Blocks while the map loads.</summary>
    public static BridgeSession StartLocal(string map, string mode, bool merc, int bots)
    {
        var settings = new PlaySettings
        {
            Map = map,
            Mode = mode,
            Side = merc ? PlaySide.Merc : PlaySide.Spy,
            Bots = bots,
            PlayerName = "Player",
        }.ForLocalLaunch();
        return new BridgeSession(new PlaySession(settings, new StopwatchClock()));
    }

    public PlaySession Session => _session;

    /// <summary>Runs the simulation ticks that are due. Returns how many ran.</summary>
    public int Update() => _session.Update();

    public float Alpha => _session.Alpha;

    public string? Failure => _session.ServerFailure?.ToString() ?? _session.LoginError;

    public unsafe void SetInput(in BridgeInput input)
    {
        LocalInput target = _session.Input;
        for (int i = 0; i < BridgeInput.ButtonCount && i < LocalInput.ButtonNames.Count; i++)
        {
            target.Set(LocalInput.ButtonNames[i], input.Buttons[i] != 0);
        }

        // Mouse counts add up until the next tick takes them; a frame without a tick must not lose them.
        target.MouseX += input.MouseX;
        target.MouseY += input.MouseY;
        target.Up = input.Up;
    }

    /// <summary>Queues an exec command (Jump, Fire, SwitchHeadset...) for the next tick.</summary>
    public void Command(string name, int arg) => _session.Input.Commands.Add(new InputCommand(name, arg));

    /// <summary>Writes the pawns as drawn this frame. Returns the count written.</summary>
    public int GetPawns(Span<BridgePawn> pawns)
    {
        float alpha = Alpha;
        int count = 0;
        foreach (int id in _order)
        {
            if (count >= pawns.Length)
            {
                break;
            }

            PawnSample now = _current[id];
            PawnSample from = _previous.TryGetValue(id, out PawnSample before) && before.ResetId == now.ResetId &&
                DistanceSquared(before.Location, now.Location) <= PawnCutDistanceSquared
                ? before
                : now;

            FVector location = Lerp(from.Location, now.Location, alpha);
            FVector prePivot = Lerp(from.PrePivot, now.PrePivot, alpha);
            pawns[count++] = new BridgePawn
            {
                ActorId = id,
                Kind = now.Merc ? 1 : 0,
                Team = now.Team,
                Health = now.Health,
                Flags = (now.Visible ? BridgePawnFlags.Visible : 0) | (now.Local ? BridgePawnFlags.Local : 0),
                X = location.X,
                Y = location.Y,
                Z = location.Z,
                PrePivotX = prePivot.X,
                PrePivotY = prePivot.Y,
                PrePivotZ = prePivot.Z,
                Yaw = LerpAngle(from.Yaw, now.Yaw, alpha),
                CollisionRadius = now.Radius,
                CollisionHeight = now.Height,
            };
        }

        return count;
    }

    public BridgeView GetView()
    {
        ViewSample now = _session.View;
        if (!now.Valid)
        {
            return default;
        }

        ViewSample from = _session.PreviousView;
        bool cut = !from.Valid || from.SpyFirstPerson != now.SpyFirstPerson ||
            DistanceSquared(from.Location, now.Location) > ViewCutDistance * ViewCutDistance;
        if (cut)
        {
            from = now;
        }

        float alpha = Alpha;
        FVector location = Lerp(from.Location, now.Location, alpha);
        return new BridgeView
        {
            X = location.X,
            Y = location.Y,
            Z = location.Z,
            Pitch = LerpAngle(Degrees(from.Rotation.Pitch), Degrees(now.Rotation.Pitch), alpha),
            Yaw = LerpAngle(Degrees(from.Rotation.Yaw), Degrees(now.Rotation.Yaw), alpha),
            Roll = LerpAngle(Degrees(from.Rotation.Roll), Degrees(now.Rotation.Roll), alpha),
            Fov = from.Fov + (now.Fov - from.Fov) * alpha,
            Valid = 1,
            SpyFirstPerson = now.SpyFirstPerson ? 1 : 0,
        };
    }

    public void Dispose() => _session.Dispose();

    private void Snapshot()
    {
        _previous.Clear();
        foreach (KeyValuePair<int, PawnSample> pair in _current)
        {
            _previous[pair.Key] = pair.Value;
        }

        _current.Clear();
        _order.Clear();
        SBasePawn? local = _session.LocalPawn;
        foreach (SBasePawn pawn in _session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            _current[pawn.ActorId] = new PawnSample
            {
                Location = pawn.Location + pawn.NetworkCorrectionOffset,
                PrePivot = pawn.PrePivot,
                Yaw = Degrees(pawn.Rotation.Yaw),
                ResetId = pawn.NetworkResetId,
                Merc = pawn is SPAWNDEFENSE,
                Team = SGameInfo.TeamIndexOf(pawn.Controller),
                Health = pawn.Health,
                Visible = !pawn.bHidden && (pawn.Role != ENetRole.ROLE_SimulatedProxy || pawn.NetworkPoseReady),
                Local = ReferenceEquals(pawn, local),
                Radius = pawn.CollisionRadius,
                Height = pawn.CollisionHeight,
            };
            _order.Add(pawn.ActorId);
        }
    }

    private static float Degrees(int rotatorUnits) => rotatorUnits * (360f / 65536f);

    private static FVector Lerp(FVector a, FVector b, float t) =>
        new(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t);

    private static float LerpAngle(float a, float b, float t)
    {
        float delta = (b - a) % 360f;
        if (delta > 180f)
        {
            delta -= 360f;
        }
        else if (delta < -180f)
        {
            delta += 360f;
        }

        return a + delta * t;
    }

    private static float DistanceSquared(FVector a, FVector b)
    {
        float x = a.X - b.X, y = a.Y - b.Y, z = a.Z - b.Z;
        return x * x + y * y + z * z;
    }

    private struct PawnSample
    {
        public FVector Location, PrePivot;
        public float Yaw, Radius, Height;
        public uint ResetId;
        public bool Merc, Visible, Local;
        public int Team, Health;
    }

    private sealed class StopwatchClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
