using System;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Game.Modes;
using OpenScct.Game.SBase;
using OpenScctGame.Play.Session;
using CK = OpenScct.Game.Combat.K;

namespace OpenScct.UnrealBridge.Test;

/// <summary>What <see cref="BridgeGrabArea.State"/> says Use would do now.</summary>
public static class GrabAreaState
{
    /// <summary>The local spy's grab test passes: Use grabs him now (green).</summary>
    public const int GrabNow = 0;

    /// <summary>He can be grabbed, but not from where the local player stands (yellow).</summary>
    public const int OutOfPosition = 1;

    /// <summary>He cannot be grabbed at all now: just got up, shocked, or in a state that refuses it (red).</summary>
    public const int Refused = 2;
}

/// <summary>
/// One living mercenary's grab window as the practice overlay draws it. Every field is 4 bytes wide, so the struct has
/// no padding. Source/OpenSCCT/Test/ScctGrabAreaApi.h mirrors it as FScctGrabArea.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeGrabArea
{
    public int ActorId;

    /// <summary><see cref="GrabAreaState"/>.</summary>
    public int State;

    /// <summary>The floor under him (his location less his collision height), lifted 2 cm so it draws on top of it. Unreal cm.</summary>
    public float X, Y, Z;

    /// <summary>Degrees: where his body faces.</summary>
    public float FacingYaw;

    /// <summary>Degrees: the middle of the window, the side a spy stands on (his facing shifted by iGrabAreaShift, turned round).</summary>
    public float SectorYaw;

    /// <summary>cm: how far from him a spy can grab (the local controller's fGrabDistance).</summary>
    public float Radius;

    /// <summary>Degrees either side of <see cref="SectorYaw"/> a spy facing straight at him can grab from (acos fGrabHalfAngle).</summary>
    public float HalfAngle;

    /// <summary>Degrees either side a spy can still grab from with his body turned by up to the front cone; at most 180.</summary>
    public float TurnedHalfAngle;
}

/// <summary>
/// The practice console command <c>grabarea</c> of the Godot client (game/Scripts/Play/GrabAreaOverlay.cs), its
/// reading half: for every living mercenary on this machine's client world, where a spy can stand to grab him
/// (spec/combat/spy-grab.md rule 4) and what Use would do now, exactly as that overlay works it out. It answers
/// only in a practice round (the level's <see cref="PracticeRules"/> on), as the Godot overlay draws only then:
/// outside practice it would show mercenaries through walls.
/// </summary>
public static class GrabAreaReader
{
    private const float FloorLift = 2f;
    private const float UnitsToDegrees = 360f / 65536f;

    /// <summary>Whether the session's client world is a practice round.</summary>
    public static bool IsPracticeRound(PlaySession? session) =>
        session?.Client.World.Level.GetService<PracticeRules>()?.On == true;

    /// <summary>Writes the grab windows of this moment. Returns the count written; 0 outside a practice round.</summary>
    public static int Read(PlaySession? session, Span<BridgeGrabArea> areas)
    {
        if (session is null || !IsPracticeRound(session))
        {
            return 0;
        }

        SPlayerController? pc = session.LocalController;
        SBasePawn? me = session.LocalPawn;
        float reach = pc?.fGrabDistance ?? 160f;
        int shift = pc?.iGrabAreaShift ?? 7200;
        float half = MathF.Acos(Math.Clamp(pc?.fGrabHalfAngle ?? 0.5f, -1f, 1f));
        float turned = MathF.Min(half + MathF.Acos(CK.GrabFrontCone), MathF.PI);
        float now = session.Client.World.Level.TimeSeconds;
        bool spyAlive = me is SPawnAttaque { Health: > 0 };
        int count = 0;
        foreach (SPAWNDEFENSE merc in session.Client.World.Level.AllActors<SPAWNDEFENSE>())
        {
            if (count >= areas.Length)
            {
                break;
            }

            if (merc.Health <= 0 || merc.bDeleteMe || merc.bHidden || ReferenceEquals(merc, me))
            {
                continue;
            }

            bool refused = !merc.bCanBeGrabbed || merc.bStickyShocking || !(now - merc.iLastWakeTime > CK.GrabWakeProtection);
            // The same stance, reach, ground, headroom and path probes the local player's grab runs.
            bool ready = !refused && pc is not null && spyAlive && pc.DescribeGrab(merc) == "Grab available.";
            FVector feet = merc.Location;
            feet.Z -= merc.CollisionHeight - FloorLift;
            // The spy stands on the side opposite the shifted facing.
            int back = merc.Rotation.Yaw + shift + 32768;
            areas[count++] = new BridgeGrabArea
            {
                ActorId = merc.ActorId,
                State = refused ? GrabAreaState.Refused : ready ? GrabAreaState.GrabNow : GrabAreaState.OutOfPosition,
                X = feet.X,
                Y = feet.Y,
                Z = feet.Z,
                FacingYaw = Wrap180(merc.Rotation.Yaw * UnitsToDegrees),
                SectorYaw = Wrap180(back * UnitsToDegrees),
                Radius = reach,
                HalfAngle = half * (180f / MathF.PI),
                TurnedHalfAngle = turned * (180f / MathF.PI),
            };
        }

        return count;
    }

    private static float Wrap180(float degrees)
    {
        degrees %= 360f;
        return degrees > 180f ? degrees - 360f : degrees < -180f ? degrees + 360f : degrees;
    }
}
