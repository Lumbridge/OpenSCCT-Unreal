using System;
using System.Numerics;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Camera;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Pose;

/// <summary>
/// <see cref="ICameraBones"/> from the posed pawns of a bridge session, as the Godot client's PoseCameraBones
/// (game/Scripts/Views/PawnViews.cs): where a pawn's bone was in the last pose drawn, in the simulation's space. With
/// it on the client's level the merc's first-person view rides 'B Head' while the camera is attached to it (on a
/// ladder, grabbed, charged, knocked out, standing up, the finishing move: SPlayerCam.FirstPersonView), and TurnHead's
/// arm aim steps from the arm as drawn (ArmAim). Without it those views stay on the pawn's cylinder. The centred
/// camera's start keeps the pawn's centre, as in the Godot client.
/// <para>Only the client's level gets it: the poses are the client's, and a local match's server level is ticked on
/// another thread.</para>
/// </summary>
public sealed class PoseCameraBones : ICameraBones
{
    private readonly BridgeSession _session;

    private PoseCameraBones(BridgeSession session)
    {
        _session = session;
    }

    /// <summary>Bone lookups answered since the process began (for the probe).</summary>
    public static long Answered { get; private set; }

    /// <summary>
    /// For the probe's comparison only: while true no lookup is answered, so the views stay on the pawn's cylinder as
    /// they did before this service.
    /// </summary>
    public static bool Off { get; set; }

    /// <summary>Puts the session's bones on <paramref name="level"/> when it has no <see cref="ICameraBones"/> yet.</summary>
    public static void Ensure(BridgeSession session, Level? level)
    {
        ArgumentNullException.ThrowIfNull(session);
        if (level is not null && level.GetService<ICameraBones>() is null)
        {
            level.SetService<ICameraBones>(new PoseCameraBones(session));
        }
    }

    public bool TryGetBone(Pawn pawn, Name bone, out FVector location, out FRotator rotation)
    {
        location = default;
        rotation = default;
        if (Off || pawn is not SBasePawn body || _session.IsDisposed || _session.PoserOf(body.ActorId) is not { } poser ||
            !ReferenceEquals(poser.Pawn, body) || !poser.TryGetBone(bone, out location, out rotation))
        {
            return false;
        }

        Answered++;
        return true;
    }

    public bool TryGetCentre(Pawn pawn, out FVector location)
    {
        location = default;
        return false;
    }

    /// <summary>
    /// The rotator of an orthonormal frame (UE2 FCoords::OrthoRotation): pitch and yaw from the X axis, roll from the
    /// Z axis against the unrolled Y axis. The Godot client's PoseCameraBones.OrthoRotation, rounding included.
    /// </summary>
    public static FRotator OrthoRotation(FVector x, FVector y, FVector z)
    {
        const double ToUnits = 32768.0 / Math.PI;
        int pitch = (int)Math.Round(Math.Atan2(x.Z, Math.Sqrt((x.X * x.X) + (x.Y * x.Y))) * ToUnits);
        int yaw = (int)Math.Round(Math.Atan2(x.Y, x.X) * ToUnits);
        new FRotator(pitch, yaw, 0).GetAxes(out _, out FVector sy, out _);
        int roll = (int)Math.Round(Math.Atan2((z.X * sy.X) + (z.Y * sy.Y) + (z.Z * sy.Z), (y.X * sy.X) + (y.Y * sy.Y) + (y.Z * sy.Z)) * ToUnits);
        return new FRotator(pitch, yaw, roll);
    }
}

// The half of the poser that says where a bone is in the world (the Godot client's SimPawnView.TryGetBone:
// Skeleton.GlobalTransform * SkeletalModel.CurrentBonePose).
public sealed partial class PawnPoser
{
    private FVector _prevOrigin;
    private FVector _origin;

    /// <summary>
    /// Keeps where the model stands at the last two ticks: the pawn's location plus its PrePivot (a crouch moves both
    /// in one tick, so the two are stepped together). Called by OnTick once it has decided whether the pawn was placed
    /// anew (a respawn, a jump): then there is no step to blend.
    /// </summary>
    private void TakeOrigin(FVector shown, FVector prePivot)
    {
        FVector origin = shown + prePivot;
        _prevOrigin = _placed ? _origin : origin;
        _origin = origin;
    }

    /// <summary>
    /// The world location (units) and rotation of one of the pawn's bones in the simulation's space, as last drawn:
    /// the model stood where the last pose's frame put it between the last two ticks, turned to the drawn yaw, with the
    /// bones as that pose left them. When nothing drew the pawn since its last tick (a run without a picture), the
    /// last tick's pose is built first. False before the first tick and for a bone the skeleton does not have.
    /// <para>The rotation is the bone's axes read as the Godot client reads them: the bone's X, Z and Y axes of the
    /// character file's space are the UE X, Y and Z axes of the frame. Unlike Godot the feet's lift on a slope is not
    /// in it, because Unreal does not lift the model either.</para>
    /// </summary>
    public bool TryGetBone(Name bone, out FVector location, out FRotator rotation)
    {
        location = default;
        rotation = default;
        if (!_placed)
        {
            return false;
        }

        int index = BoneIndex(bone);
        if (index < 0 || index >= _applier.BoneCount)
        {
            return false;
        }

        if (_posedTick != _tick)
        {
            Pose(1f, default);
        }

        ReadOnlySpan<Vector3> positions = _applier.Positions;
        ReadOnlySpan<Quaternion> rotations = _applier.Rotations;
        ReadOnlySpan<Vector3> scales = _applier.Scales;
        Xform pose = Xform.FromTrs(positions[index], rotations[index], scales[index]);
        int[] parents = _character.Parents;
        for (int parent = parents[index]; parent >= 0; parent = parents[parent])
        {
            pose = Xform.FromTrs(positions[parent], rotations[parent], scales[parent]) * pose;
        }

        Basis3 skeleton = SkeletonBasis();
        FVector origin = _prevOrigin + ((_origin - _prevOrigin) * _posedAlpha);
        location = origin + (RenderSpace.DirectionToUe(skeleton * pose.Origin) * RenderSpace.UnitsPerMetre);
        Basis3 axes = (skeleton * pose.Basis).Orthonormalized();
        rotation = PoseCameraBones.OrthoRotation(RenderSpace.DirectionToUe(axes.X), RenderSpace.DirectionToUe(axes.Z), RenderSpace.DirectionToUe(axes.Y));
        return true;
    }
}
