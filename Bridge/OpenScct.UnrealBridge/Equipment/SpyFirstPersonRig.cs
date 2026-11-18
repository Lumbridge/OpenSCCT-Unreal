using System;
using System.Numerics;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>
/// Fits the spy's stock two-handed grip to the pistol held in front of the camera, once when the eye view loads: the
/// right arm is solved to hold the pistol at its ready place, the left arm to put the support hand where the stock
/// stance has it relative to the gun. The pose it leaves in the arms is then kept for the whole match; only the
/// models move with the draw.
/// <para>A port of OpenSCCT's game/Scripts/Views/SpyFirstPersonRig.cs (the eye view is OpenSCCT's own; the original
/// game draws the spy from behind only). Every transform is in the camera's frame: X forward, Y up, Z right,
/// metres.</para>
/// </summary>
internal static class SpyFirstPersonRig
{
    /// <summary>Where the pistol's hand frame sits in front of the eye when ready.</summary>
    public static Xform Ready { get; } = new(
        Turn(Vector3.UnitY, .032f) * Turn(Vector3.UnitZ, .013f) * Turn(Vector3.UnitX, .045f), new Vector3(.294f, -.132f, .143f));

    private static readonly Vector3 s_supportUpperArm = Vector3.Normalize(new Vector3(-.83f, -.52f, -.20f));

    /// <summary>A rotation about an axis (Godot's Basis(axis, angle)).</summary>
    public static Basis3 Turn(Vector3 axis, float radians) => Basis3.FromQuaternion(Quat.AxisAngle(axis, radians));

    /// <summary>
    /// Poses the arms of <paramref name="model"/> (bone by bone relative to the parent, into the three spans) and
    /// gives the pistol's place in the camera's frame. False when the skeleton lacks an arm bone or the stock stance.
    /// <paramref name="supportError"/> is how far the support wrist ended from where the stance wants it, in metres.
    /// </summary>
    public static bool TryPose(EquipmentModel model, in Xform ready, Span<Vector3> positions, Span<Quaternion> rotations,
        Span<Vector3> scales, out Xform weapon, out float supportError, StudioClips? clips = null)
    {
        weapon = ready;
        supportError = -1f;
        GlbCharacter skeleton = model.Character;
        var left = new Arm(model, "L");
        var right = new Arm(model, "R");
        if (!left.Valid || !right.Valid || (clips?.Find("WaitStSpFd2") ?? skeleton.Find("WaitStSpFd2")) is not { } stance)
        {
            return false;
        }

        model.Sample(stance, .25f, positions, rotations, scales);

        // The world pistol mounts on the right hand. Capture the stock support wrist and elbow in that frame.
        Xform toGun = model.Global(right.Wrist, positions, rotations, scales).AffineInverse();
        Xform supportWrist = toGun * model.Global(left.Wrist, positions, rotations, scales);
        Vector3 supportElbow = Point(toGun, model.Global(left.Forearm, positions, rotations, scales).Origin);
        for (int i = 0; i < skeleton.BoneCount; i++)
        {
            string name = skeleton.BoneNames[i];
            bool finger = name.StartsWith("B L Finger", StringComparison.Ordinal) || name.StartsWith("B R Finger", StringComparison.Ordinal);
            positions[i] = skeleton.RestPosition[i];
            scales[i] = skeleton.RestScale[i];
            if (!finger)
            {
                rotations[i] = skeleton.RestRotation[i];
            }
        }

        // The arms hang under the camera with their mesh placement only, so the camera's frame to the skeleton's is
        // that placement taken back out.
        Xform toCamera = model.ActorToSkeleton;
        Xform toSkeleton = toCamera.AffineInverse();
        weapon = toCamera * right.Solve(positions, rotations, Point(toSkeleton, new Vector3(-.105f, -.265f, .195f)),
            toSkeleton.Basis * new Vector3(-.32f, .58f, .74f), toSkeleton * ready);
        Xform wrist = weapon * supportWrist;
        Vector3 elbow = wrist.Origin + (Normalized(Point(weapon, supportElbow) - wrist.Origin) * left.ForeLength);
        left.Solve(positions, rotations, Point(toSkeleton, elbow + (s_supportUpperArm * left.UpperLength)),
            toSkeleton.Basis * -s_supportUpperArm, toSkeleton * wrist);
        supportError = Vector3.Distance(Point(toCamera, model.Global(left.Wrist, positions, rotations, scales).Origin), wrist.Origin);
        return true;
    }

    private static Vector3 Point(in Xform x, Vector3 p) => (x.Basis * p) + x.Origin;

    /// <summary>Godot's Normalized: a zero vector stays zero.</summary>
    private static Vector3 Normalized(Vector3 v)
    {
        float length = v.Length();
        return length > 0f ? v / length : Vector3.Zero;
    }

    /// <summary>One arm of the spy skeleton: clavicle, upper arm, forearm, hand.</summary>
    private readonly struct Arm
    {
        private readonly EquipmentModel _model;
        private readonly int _clavicle;
        private readonly int _upper;

        public Arm(EquipmentModel model, string side)
        {
            _model = model;
            GlbCharacter skeleton = model.Character;
            _clavicle = skeleton.FindBone($"B {side} Clavicle");
            _upper = skeleton.FindBone($"B {side} UpperArm");
            Forearm = skeleton.FindBone($"B {side} ForeArm");
            Wrist = skeleton.FindBone($"B {side} Hand");
        }

        public int Forearm { get; }

        public int Wrist { get; }

        public bool Valid => _clavicle >= 0 && _upper >= 0 && Forearm >= 0 && Wrist >= 0;

        public float UpperLength => _model.Character.RestPosition[Forearm].Length();

        public float ForeLength => _model.Character.RestPosition[Wrist].Length();

        /// <summary>
        /// Two-bone solve in the skeleton's space: the shoulder at <paramref name="shoulder"/>, the elbow bent toward
        /// <paramref name="pole"/>, the hand at <paramref name="target"/> with its rotation. Returns the hand's pose.
        /// </summary>
        public Xform Solve(Span<Vector3> positions, Span<Quaternion> rotations, Vector3 shoulder, Vector3 pole, in Xform target)
        {
            GlbCharacter skeleton = _model.Character;
            Xform clavicleRest = GlobalRest(_clavicle);
            Basis3 upperRest = GlobalRest(_upper).Basis.Orthonormalized();
            Basis3 foreRest = LocalRest(Forearm).Basis.Orthonormalized();
            Basis3 handRest = LocalRest(Wrist).Basis.Orthonormalized();
            float upperLength = UpperLength;
            float foreLength = ForeLength;
            Vector3 delta = target.Origin - shoulder;
            Vector3 direction = Normalized(delta);
            float distance = Math.Clamp(delta.Length(), .04f, upperLength + foreLength - .003f);
            Vector3 bend = Normalized(pole - (direction * Vector3.Dot(pole, direction)));
            float along = ((upperLength * upperLength) - (foreLength * foreLength) + (distance * distance)) / (2f * distance);
            float height = MathF.Sqrt(MathF.Max(0f, (upperLength * upperLength) - (along * along)));
            Vector3 elbow = shoulder + (direction * along) + (bend * height);
            Vector3 wrist = shoulder + (direction * distance);
            Basis3 upper = Aim(upperRest, Normalized(elbow - shoulder));
            Basis3 fore = Aim(upper * foreRest, Normalized(wrist - elbow));

            // Carry most of the wrist's roll through the forearm to keep the sleeve from twisting sharply.
            Vector3 axis = fore.X;
            Basis3 wristBasis = target.Basis.Orthonormalized();
            Vector3 from = Normalized(Slide((fore * handRest).Y, axis));
            Vector3 to = Normalized(Slide(wristBasis.Y, axis));
            if (from.LengthSquared() > .5f && to.LengthSquared() > .5f)
            {
                fore = Turn(axis, SignedAngle(from, to, axis) * .70f) * fore;
            }

            var clavicle = new Xform(clavicleRest.Basis, shoulder - (clavicleRest.Basis * skeleton.RestPosition[_upper]));
            var upperPose = new Xform(upper, shoulder);
            var forePose = new Xform(fore, elbow);
            var handPose = new Xform(wristBasis, wrist);
            int parent = skeleton.Parents[_clavicle];
            Xform spine = parent >= 0 ? GlobalRest(parent) : Xform.Identity;
            SetPose(positions, rotations, _clavicle, spine.AffineInverse() * clavicle);
            SetPose(positions, rotations, _upper, clavicle.AffineInverse() * upperPose);
            SetPose(positions, rotations, Forearm, upperPose.AffineInverse() * forePose);
            SetPose(positions, rotations, Wrist, forePose.AffineInverse() * handPose);
            return handPose;
        }

        private Xform LocalRest(int bone)
        {
            GlbCharacter s = _model.Character;
            return Xform.FromTrs(s.RestPosition[bone], s.RestRotation[bone], s.RestScale[bone]);
        }

        private Xform GlobalRest(int bone)
        {
            GlbCharacter s = _model.Character;
            return _model.Global(bone, s.RestPosition, s.RestRotation, s.RestScale);
        }

        private static void SetPose(Span<Vector3> positions, Span<Quaternion> rotations, int bone, in Xform pose)
        {
            positions[bone] = pose.Origin;
            rotations[bone] = Quaternion.Normalize(pose.Basis.GetRotationQuaternion());
        }

        /// <summary>The basis turned the short way so its X axis points along <paramref name="direction"/>.</summary>
        private static Basis3 Aim(Basis3 basis, Vector3 direction) =>
            Basis3.FromQuaternion(EquipmentMath.ShortestArc(basis.X, direction)) * basis;

        /// <summary>Godot's Slide: the part of <paramref name="v"/> across <paramref name="normal"/>.</summary>
        private static Vector3 Slide(Vector3 v, Vector3 normal) => v - (normal * Vector3.Dot(v, normal));

        /// <summary>Godot's SignedAngleTo: the angle from one direction to another, negative when it turns against <paramref name="axis"/>.</summary>
        private static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
        {
            Vector3 cross = Vector3.Cross(from, to);
            float angle = MathF.Atan2(cross.Length(), Vector3.Dot(from, to));
            return Vector3.Dot(cross, axis) < 0f ? -angle : angle;
        }
    }
}
