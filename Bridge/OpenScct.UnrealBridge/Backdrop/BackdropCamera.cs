using System;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Movement;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Backdrop;

/// <summary>
/// The camera of the menu's backdrop match. The spectating player's own view follows the watched player from a fixed
/// compass direction (a spectator turns the view with the mouse, and nobody holds one here), which makes a poor
/// picture: this replaces it (<see cref="PlaySession.ViewOverride"/>) with a camera behind and a little above the
/// watched player that turns with the player's body, drifts slowly from side to side, comes closer where a wall is
/// in the way, and cuts to the next player every so often (the spectator's Fire).
/// <para>Whom to watch stays the simulation's choice (the spectator's view target), so the watched player is always
/// one the server keeps sending.</para>
/// </summary>
public sealed class BackdropCamera
{
    /// <summary>How far behind the watched player the camera sits, in unreal units.</summary>
    private const float Distance = 300f;

    /// <summary>The closest the camera comes when a wall is in the way.</summary>
    private const float NearestDistance = 70f;

    /// <summary>Degrees the camera looks down.</summary>
    private const float LookDown = 9f;

    /// <summary>Degrees the camera drifts to either side of the player's back, and the seconds one drift takes.</summary>
    private const float DriftDegrees = 24f;
    private const float DriftSeconds = 26f;

    /// <summary>How quickly the camera turns after the player's body (per second; higher is stiffer).</summary>
    private const float TurnRate = 2.2f;

    /// <summary>How quickly the camera backs away again after a wall pushed it in.</summary>
    private const float BackOffRate = 1.6f;

    /// <summary>Seconds on one player before the cut to the next, between these two.</summary>
    private const float ShortestStay = 18f;
    private const float LongestStay = 30f;

    /// <summary>
    /// Degrees the finished view is turned to the right and down from the line to the player: the player then stands
    /// left of the middle and in the upper half of the picture, between the menu's column of entries and its cards
    /// instead of behind a card.
    /// </summary>
    private const float FrameRight = 8f;
    private const float FrameDown = 5f;

    private const float FieldOfView = 78f;
    private const float UnitsPerDegree = 65536f / 360f;

    private readonly PlaySession _session;
    private readonly Random _random = new();
    private SBasePawn? _watched;
    private float _yaw;
    private float _distance = Distance;
    private float _seconds;
    private float _cutAt;
    private ViewSample _last;

    private BackdropCamera(PlaySession session)
    {
        _session = session;
        _cutAt = NextStay();
    }

    /// <summary>Gives <paramref name="session"/> the backdrop's camera. Called before the session's first tick.</summary>
    public static BackdropCamera Install(BridgeSession session)
    {
        var camera = new BackdropCamera(session.Session);
        session.Session.ViewOverride = camera.View;
        return camera;
    }

    private float NextStay() => ShortestStay + (float)_random.NextDouble() * (LongestStay - ShortestStay);

    /// <summary>The view to draw after a client tick; <paramref name="calc"/> is the spectator's own.</summary>
    private ViewSample View(ViewSample calc)
    {
        const float dt = Sim.DeltaTime;
        long tick = _session.Client.LocalTick;
        _seconds += dt;
        SPlayerController? pc = _session.LocalController;
        SBasePawn? pawn = (pc?.ViewTarget as SBasePawn ?? pc?.SpecTarget) is { bDeleteMe: false } target ? target : null;
        if (pawn is null)
        {
            // Between players (a death, the end of the round): the picture holds where it was.
            _watched = null;
            return _last.Valid ? _last with { Tick = tick } : calc;
        }

        if (!ReferenceEquals(pawn, _watched))
        {
            _watched = pawn;
            _yaw = pawn.Rotation.Yaw;
            _distance = Distance;
            _seconds = 0f;
            _cutAt = NextStay();
        }
        else if (_seconds >= _cutAt)
        {
            // The spectator's Fire: the next player. The view cuts when the simulation has changed the target.
            _cutAt = _seconds + NextStay();
            _session.Input.Commands.Add(new InputCommand("Fire", 0));
        }

        // The camera turns after the body, never faster than the eye follows.
        float turn = FRotator.NormalizeAxis(pawn.Rotation.Yaw - (int)_yaw);
        _yaw += turn * (1f - MathF.Exp(-TurnRate * dt));
        float drift = DriftDegrees * MathF.Sin(_seconds * MathF.Tau / DriftSeconds);
        var rotation = new FRotator((int)(-LookDown * UnitsPerDegree), FRotator.NormalizeAxis((int)(_yaw + drift * UnitsPerDegree)), 0);

        FVector focus = pawn.Location + pawn.NetworkCorrectionOffset + new FVector(0f, 0f, MathF.Max(pawn.EyeHeight, 20f) * 0.75f);
        FVector back = rotation.Vector() * -1f;
        float room = Distance;
        if (MovementNatives.Driver(pawn) is { } driver &&
            driver.Collision.LineCheck(out FCheckResult hit, pawn, focus + back * Distance, focus, new FVector(10f, 10f, 10f),
                ETraceFlags.TRACE_Level | ETraceFlags.TRACE_Movers | ETraceFlags.TRACE_LevelGeometry))
        {
            float reached = (hit.Location - focus).Size();
            room = Math.Clamp(reached - 6f, NearestDistance, Distance);
        }

        // In at once (the camera never sits inside a wall), out again slowly.
        _distance = room < _distance ? room : _distance + (room - _distance) * (1f - MathF.Exp(-BackOffRate * dt));
        var framed = new FRotator(rotation.Pitch - (int)(FrameDown * UnitsPerDegree), FRotator.NormalizeAxis(rotation.Yaw + (int)(FrameRight * UnitsPerDegree)), 0);
        _last = new ViewSample(tick, focus + back * _distance, framed, FieldOfView, true);
        return _last;
    }
}
