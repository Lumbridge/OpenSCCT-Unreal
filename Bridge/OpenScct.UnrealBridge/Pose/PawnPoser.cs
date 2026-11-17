using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Animation;
using OpenScct.Game.Movement;
using OpenScct.Game.SBase;
using OpenScct.Physics.Movement;

namespace OpenScct.UnrealBridge.Pose;

/// <summary>
/// Poses one simulated pawn's skeleton for a drawn frame: all 25 of the pawn's animation channels as the simulation
/// keeps them (the actions' clips, the walk blender's channels 1 to 15, the overlays on their start bones, the root
/// lock of a root-motion move), interpolated between the last two ticks, then the upper-body aim and the bone
/// direction controllers. It reads the simulation and never writes it.
/// <para>A port of the posing half of OpenSCCT's game/Scripts/Views/SimPawnView.cs (OnTick, SetAim, SetDirections,
/// Pose, RootLockOf, Interpolated). Not ported: the hub's emote overlay (IPawnPoseOverlay), the local player's
/// look-ahead aim (DrawnView, which needs the camera of the frame), map outfits (the stock mesh is always posed) and
/// replacement clips of an animation project.</para>
/// </summary>
public sealed class PawnPoser
{
    private readonly ChannelPose[] _channels = new ChannelPose[PawnAnimation.ChannelCount];
    private readonly TickChannel[] _prev = new TickChannel[PawnAnimation.ChannelCount];
    private readonly TickChannel[] _cur = new TickChannel[PawnAnimation.ChannelCount];
    private readonly Dictionary<Name, int> _boneOf = new();
    private readonly GlbCharacter _character;
    private readonly ChannelPoseApplier _applier;
    private readonly int _aimBone;
    private FVector _location;
    private uint _networkReset;
    private int _prevYaw;
    private int _yaw;
    private int _drawYaw;
    private Vector2 _prevAim, _aim;
    private Dictionary<Name, BoneDirection> _prevDirections = new();
    private Dictionary<Name, BoneDirection> _directions = new();
    private bool _placed;
    private long _tick;
    private long _posedTick = -1;
    private float _posedAlpha;

    public PawnPoser(SBasePawn pawn, GlbCharacter character)
    {
        Pawn = pawn ?? throw new ArgumentNullException(nameof(pawn));
        _character = character ?? throw new ArgumentNullException(nameof(character));
        // BoneMirrors counts the team's stock skeleton, which is the one posed here.
        _applier = new ChannelPoseApplier(character, pawn.BoneMirrors);
        _aimBone = _applier.FindBone(MeshAimIK.FullAimBoneName);
    }

    /// <summary>The pawn posed (on the client world).</summary>
    public SBasePawn Pawn { get; }

    public GlbCharacter Character => _character;

    /// <summary>False until the first tick has been taken: there is no pose to read yet.</summary>
    public bool Placed => _placed;

    /// <summary>The channels of the last pose as they were blended (for the probe).</summary>
    public ReadOnlySpan<ChannelPose> DrawnChannels => _channels;

    /// <summary>The upper-body aim (the AAFULL pair: yaw, pitch) in radians, <paramref name="alpha"/> of the way between the last two ticks.</summary>
    public Vector2 Aim(float alpha) => Vector2.Lerp(_prevAim, _aim, Math.Clamp(alpha, 0f, 1f));

    /// <summary>Takes the pawn's state after a simulation tick (yaw, aim, bone directions and channel frames for the interpolation).</summary>
    public void OnTick()
    {
        SBasePawn pawn = Pawn;
        FVector shown = pawn.Location + pawn.NetworkCorrectionOffset;
        if (_networkReset != pawn.NetworkResetId || (_placed && (shown - _location).SizeSquared() > 40000f))
        {
            _placed = false;
            _networkReset = pawn.NetworkResetId;
        }

        _location = shown;
        _prevYaw = _placed ? _yaw : pawn.Rotation.Yaw;
        _yaw = pawn.Rotation.Yaw;
        var aim = new Vector2(pawn.PawnAnim.Aim.FullYaw, pawn.PawnAnim.Aim.FullPitch);
        _prevAim = _placed ? _aim : aim;
        _aim = aim;
        (_prevDirections, _directions) = (_directions, _prevDirections);
        _directions.Clear();
        foreach (BoneDirection direction in pawn.PawnAnim.Directions.All)
        {
            _directions[direction.Bone] = direction;
        }

        if (!_placed)
        {
            _prevDirections.Clear();
            foreach (BoneDirection direction in _directions.Values)
            {
                _prevDirections[direction.Bone] = direction;
            }
        }

        PawnAnimation anim = pawn.PawnAnim;
        for (int c = 0; c < PawnAnimation.ChannelCount; c++)
        {
            _prev[c] = _placed ? _cur[c] : default;
            PawnAnimChannel ch = anim[c];
            _cur[c] = new TickChannel(ch.Sequence, ch.Frame, ch.ClipFrame, ch.BlendAlpha, ch.StartBone);
        }

        _placed = true;
        _tick++;
    }

    /// <summary>
    /// Writes the pose for a frame <paramref name="alpha"/> of the way from the previous tick to the last: each bone's
    /// transform relative to its parent, in the skin's joint order. Returns the bone count (0 before the first tick);
    /// bones past the end of <paramref name="bones"/> are not written.
    /// </summary>
    public int Pose(float alpha, Span<BridgeBone> bones)
    {
        if (!_placed)
        {
            return 0;
        }

        alpha = Math.Clamp(alpha, 0f, 1f);
        // The blend keeps state from draw to draw (a tween moves on from the pose of the draw before), so a second
        // read of the same frame must not blend again.
        if (_posedTick != _tick || _posedAlpha != alpha)
        {
            _posedTick = _tick;
            _posedAlpha = alpha;
            _drawYaw = _prevYaw + (int)(FRotator.NormalizeAxis(_yaw - _prevYaw) * alpha);
            SetAim(alpha);
            SetDirections(alpha);
            Blend(alpha);
        }

        ReadOnlySpan<Vector3> positions = _applier.Positions;
        ReadOnlySpan<Quaternion> rotations = _applier.Rotations;
        ReadOnlySpan<Vector3> scales = _applier.Scales;
        int count = Math.Min(bones.Length, _applier.BoneCount);
        for (int b = 0; b < count; b++)
        {
            bones[b] = new BridgeBone
            {
                Px = positions[b].X,
                Py = positions[b].Y,
                Pz = positions[b].Z,
                Qx = rotations[b].X,
                Qy = rotations[b].Y,
                Qz = rotations[b].Z,
                Qw = rotations[b].W,
                Sx = scales[b].X,
                Sy = scales[b].Y,
                Sz = scales[b].Z,
            };
        }

        return _applier.BoneCount;
    }

    /// <summary>The skeleton's axes in render space at the drawn yaw (the Godot client's Skeleton.GlobalTransform.Basis).</summary>
    private Basis3 SkeletonBasis() => RenderSpace.SkeletonBasis(_drawYaw, _character.RotOrigin, _character.MeshScale);

    /// <summary>
    /// The upper-body aim of this draw: the mesh instance's AAFULL pair (yaw, pitch) turns B Spine2 (bone 4) by the
    /// pitch about the horizontal axis across the pawn's facing (Z x facing), then by the yaw about the vertical
    /// (0x11102160: two axis-angle coords applied to the bone's transposed space base).
    /// </summary>
    private void SetAim(float alpha)
    {
        Vector2 drawn = Vector2.Lerp(_prevAim, _aim, alpha);
        if (drawn.X == 0f && drawn.Y == 0f)
        {
            _applier.AimBone = -1;
            return;
        }

        _applier.AimBone = _aimBone;
        Basis3 world = RenderSpace.AimBasis(drawn.X, drawn.Y, new FRotator(0, _drawYaw, 0).Vector());
        Basis3 skeleton = SkeletonBasis().Orthonormalized();
        _applier.AimRotation = (skeleton.Inverse() * world * skeleton).GetRotationQuaternion();
    }

    /// <summary>
    /// The bone direction controllers the simulation holds (TurnHead's arm aim, aim-ik.md rule 5): each world rotation
    /// (UE axes) taken into the skeleton's space for the applier, interpolated between ticks.
    /// </summary>
    private void SetDirections(float alpha)
    {
        _applier.Directions.Clear();
        if (_directions.Count == 0)
        {
            return;
        }

        Basis3 toSkeleton = SkeletonBasis().Orthonormalized().Inverse();
        foreach (BoneDirection d in _directions.Values)
        {
            int bone = BoneIndex(d.Bone);
            if (bone < 0)
            {
                continue;
            }

            FRotator drawn = d.Rotation;
            if (_prevDirections.TryGetValue(d.Bone, out BoneDirection previous))
            {
                FRotator before = previous.Rotation;
                drawn = new FRotator(before.Pitch + (int)(FRotator.NormalizeAxis(drawn.Pitch - before.Pitch) * alpha),
                    before.Yaw + (int)(FRotator.NormalizeAxis(drawn.Yaw - before.Yaw) * alpha),
                    before.Roll + (int)(FRotator.NormalizeAxis(drawn.Roll - before.Roll) * alpha));
            }

            Quaternion rotation = (toSkeleton * RenderSpace.Rotation(drawn)).GetRotationQuaternion();
            _applier.Directions[bone] = (rotation, d.Alpha);
        }
    }

    /// <summary>The skeleton index of a bone by its UE name (cached), 0 for no name, or -1.</summary>
    private int BoneIndex(Name bone)
    {
        if (bone.IsNone)
        {
            return 0;
        }

        if (!_boneOf.TryGetValue(bone, out int index))
        {
            index = _applier.FindBone(bone.ToString());
            _boneOf[bone] = index;
        }

        return index;
    }

    private void Blend(float alpha)
    {
        SBasePawn pawn = Pawn;
        PawnAnimation anim = pawn.PawnAnim;
        Array.Clear(_channels);
        for (int c = 0; c < PawnAnimation.ChannelCount; c++)
        {
            PawnAnimChannel ch = anim[c];
            if (ch.Sequence.IsNone || ch.Clip is null || _character.Find(ch.Sequence.ToString()) is not { } clip)
            {
                continue;
            }

            // The walk/strafe blender and upper-body channels change their weights each tick too. Sampling only
            // their newest weights made the pose step at simulation cadence even while its clip phase was smooth.
            bool sameSequence = _cur[c].Sequence == ch.Sequence && _prev[c].Sequence == ch.Sequence;
            bool compatibleMask = _cur[c].StartBone == _prev[c].StartBone || _cur[c].Alpha == 0f || _prev[c].Alpha == 0f;
            float weight = c == 0 ? 1f : sameSequence && compatibleMask
                ? _prev[c].Alpha + ((_cur[c].Alpha - _prev[c].Alpha) * alpha) : ch.BlendAlpha;
            if (!(weight > 0f))
            {
                continue;
            }

            (float frame, float clipFrame) = Interpolated(c, ch, alpha);
            // Ending an overlay clears its bone mask with its weight. While the outgoing weight still
            // contributes between ticks, keep its mask too: a draw/holster clip's root and legs must
            // never replace the grounded pose during the last rendered frames of an upper-body draw.
            Name startBone = sameSequence && _cur[c].Alpha == 0f ? _prev[c].StartBone : ch.StartBone;
            int start = BoneIndex(startBone);
            _channels[c] = new ChannelPose
            {
                Clip = clip,
                Sequence = ch.Sequence.ToString(),
                Frame = frame,
                ClipFrame = clipFrame,
                Frames = ch.Frames,
                Fps = ch.Clip.Fps,
                Alpha = weight,
                StartBone = start < 0 ? 0 : start,
                Loop = ch.bLoop,
                Mirror = ch.bMirror,
                GlobalPose = ch.bGlobalPose,
            };
        }

        RootLock rootLock = RootLock.None;
        PawnAnimChannel c0 = anim[0];
        if (pawn.Physics == EPhysics.PHYS_RootMotion && pawn.iLockRootMotionType != 0)
        {
            // The mesh's own lock once physics has set it (the pose build reads the instance's lock), else the
            // action's.
            int lockType = ((ISBasePawnPhysics)pawn).RootMotionMesh is PawnRootMotion { RootLockType: not 0 } locked
                ? locked.RootLockType
                : pawn.iLockRootMotionType;
            rootLock = RootLock.Translation;
            if ((lockType & 2) != 0)
            {
                rootLock |= RootLock.Yaw;
            }
        }

        Vector3? offset = null;
        Quaternion? turn = null;
        if (rootLock != RootLock.None)
        {
            RootLockOf(pawn, c0, out offset, out turn);
        }

        _applier.Apply(_channels, rootLock, c0.bBackward ? c0.AnimLast : 0f, offset, turn);
    }

    /// <summary>
    /// The locked root's offset in skeleton space (coop.md rule 24, anim.md rule 3.4): R(lock) - R(t) of channel 0 at
    /// the drawn frame (the backward forms for a backward play), in the mesh's axes; null when the mesh is not locked
    /// yet or the clip has no root data (the root is then pinned). With lock bit 2 also the turn that takes the yaw
    /// the root motion carried since the lock back out of the root (position and rotation), so the body stays where the
    /// move put it while the actor turns.
    /// (Decoded: pose build game 0x109ba4f0 at 0x109bcbac.)
    /// </summary>
    private void RootLockOf(SBasePawn pawn, PawnAnimChannel c0, out Vector3? offset, out Quaternion? turn)
    {
        offset = null;
        turn = null;
        if (((ISBasePawnPhysics)pawn).RootMotionMesh is not PawnRootMotion { RootLockType: not 0 } mesh)
        {
            return;
        }

        Basis3 skeleton = SkeletonBasis();
        float clipFrame = _channels[0].Clip is not null ? _channels[0].ClipFrame : c0.ClipFrame;
        if (RootLockPose.Offset(c0.Clip, clipFrame, c0.bMirror, mesh.LockedRoot, mesh.RootLockType, c0.bBackward, c0.bLoop,
                out FVector local))
        {
            new FRotator(0, _drawYaw, 0).GetAxes(out FVector x, out FVector y, out FVector z);
            Vector3 world = RenderSpace.Direction((x * local.X) + (y * local.Y) + (z * local.Z)) / RenderSpace.UnitsPerMetre;
            offset = skeleton.Inverse() * world;
        }

        if ((mesh.RootLockType & 2) != 0)
        {
            Basis3 unit = skeleton.Orthonormalized();
            int carried = FRotator.NormalizeAxis(mesh.CarriedRootTurn(c0, clipFrame));
            Basis3 back = RenderSpace.Rotation(new FRotator(0, -carried, 0));
            turn = (unit.Inverse() * back * unit).GetRotationQuaternion();
        }
    }

    /// <summary>
    /// A channel's frame between the last two ticks: interpolated while the same clip moves forward (no new play), and
    /// across the wrap of a loop (the frame went past 1 and came back near 0), else the last tick's. The walk channels
    /// loop all the time; without the wrap case each loop drew one tick ahead and then held that frame for a whole tick
    /// (a hitch every cycle, worst at walking speed where a cycle is about a second).
    /// (Estimated: draws between ticks interpolate the channel frame; the original draws once per tick.)
    /// </summary>
    private (float Frame, float ClipFrame) Interpolated(int c, PawnAnimChannel ch, float alpha)
    {
        TickChannel cur = _cur[c];
        TickChannel prev = _prev[c];
        if (cur.Sequence != ch.Sequence || prev.Sequence != cur.Sequence || (prev.Frame < 0f) != (cur.Frame < 0f))
        {
            return (ch.Frame, ch.ClipFrame);
        }

        if (cur.Frame >= prev.Frame)
        {
            return (prev.Frame + ((cur.Frame - prev.Frame) * alpha), prev.ClipFrame + ((cur.ClipFrame - prev.ClipFrame) * alpha));
        }

        // A loop's wrap: the frame fell by more than half a cycle in one tick. Anything else going back (a frame set
        // by hand, a new play of the same clip) shows the last tick's frame as before.
        if (!ch.bLoop || prev.Frame < 0f || prev.Frame - cur.Frame < 0.5f)
        {
            return (ch.Frame, ch.ClipFrame);
        }

        return (Wrapped(prev.Frame, cur.Frame + 1f, alpha), Wrapped(prev.ClipFrame, cur.ClipFrame + (ch.bBackward ? -1f : 1f), alpha));
    }

    /// <summary>The loop position <paramref name="alpha"/> of the way from <paramref name="from"/> to an unwrapped <paramref name="to"/>, back in [0, 1).</summary>
    private static float Wrapped(float from, float to, float alpha)
    {
        float f = from + ((to - from) * alpha);
        return f - MathF.Floor(f);
    }

    private readonly record struct TickChannel(Name Sequence, float Frame, float ClipFrame, float Alpha, Name StartBone);
}
