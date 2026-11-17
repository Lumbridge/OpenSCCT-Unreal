using System;
using System.Numerics;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Pose;

// The pose code is a port of OpenSCCT's Godot client (game/Scripts/Views/ChannelPoseApplier.cs and SimPawnView.cs).
// Its results depend on Godot's exact conventions, so the few Godot types it leans on are written out here on
// System.Numerics vectors instead of being mapped onto Matrix4x4 (which is row-vector and would invite a
// transposition mistake in every product).

/// <summary>
/// A 3x3 matrix as Godot's Basis: three column vectors, applied to column vectors (<c>basis * v</c>), products read
/// right to left (<c>a * b</c> applies b first).
/// </summary>
public readonly struct Basis3
{
    public readonly Vector3 X, Y, Z;

    public Basis3(Vector3 x, Vector3 y, Vector3 z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    public static Basis3 Identity => new(Vector3.UnitX, Vector3.UnitY, Vector3.UnitZ);

    /// <summary>Godot's Basis(Quaternion): the rotation matrix of a unit quaternion.</summary>
    public static Basis3 FromQuaternion(Quaternion q)
    {
        float d = q.LengthSquared();
        float s = 2f / d;
        float xs = q.X * s, ys = q.Y * s, zs = q.Z * s;
        float wx = q.W * xs, wy = q.W * ys, wz = q.W * zs;
        float xx = q.X * xs, xy = q.X * ys, xz = q.X * zs;
        float yy = q.Y * ys, yz = q.Y * zs, zz = q.Z * zs;
        return new Basis3(
            new Vector3(1f - (yy + zz), xy + wz, xz - wy),
            new Vector3(xy - wz, 1f - (xx + zz), yz + wx),
            new Vector3(xz + wy, yz - wx, 1f - (xx + yy)));
    }

    public static Basis3 FromScale(Vector3 s) => new(new Vector3(s.X, 0f, 0f), new Vector3(0f, s.Y, 0f), new Vector3(0f, 0f, s.Z));

    public static Vector3 operator *(Basis3 m, Vector3 v) => (m.X * v.X) + (m.Y * v.Y) + (m.Z * v.Z);

    public static Basis3 operator *(Basis3 a, Basis3 b) => new(a * b.X, a * b.Y, a * b.Z);

    /// <summary>Godot's ScaledLocal: the scale applied in the basis's own axes (<c>this * diag(scale)</c>).</summary>
    public Basis3 ScaledLocal(Vector3 s) => new(X * s.X, Y * s.Y, Z * s.Z);

    /// <summary>Godot's Scaled: the scale applied in the parent's axes (<c>diag(scale) * this</c>).</summary>
    public Basis3 Scaled(Vector3 s) => new(X * s, Y * s, Z * s);

    public float Determinant() => Vector3.Dot(X, Vector3.Cross(Y, Z));

    public Basis3 Inverse()
    {
        float det = Determinant();
        Vector3 r0 = Vector3.Cross(Y, Z) / det, r1 = Vector3.Cross(Z, X) / det, r2 = Vector3.Cross(X, Y) / det;
        return new Basis3(new Vector3(r0.X, r1.X, r2.X), new Vector3(r0.Y, r1.Y, r2.Y), new Vector3(r0.Z, r1.Z, r2.Z));
    }

    /// <summary>Godot's Orthonormalized: Gram-Schmidt over the columns, X first.</summary>
    public Basis3 Orthonormalized()
    {
        Vector3 x = Vector3.Normalize(X);
        Vector3 y = Vector3.Normalize(Y - (x * Vector3.Dot(x, Y)));
        Vector3 z = Vector3.Normalize(Z - (x * Vector3.Dot(x, Z)) - (y * Vector3.Dot(y, Z)));
        return new Basis3(x, y, z);
    }

    /// <summary>Godot's Basis.Scale: the column lengths, negated when the basis is a reflection.</summary>
    public Vector3 GetScale()
    {
        float sign = Determinant() < 0f ? -1f : 1f;
        return new Vector3(X.Length(), Y.Length(), Z.Length()) * sign;
    }

    /// <summary>Godot's GetRotationQuaternion: the rotation left after scale (and a reflection) is taken out.</summary>
    public Quaternion GetRotationQuaternion()
    {
        Basis3 m = Orthonormalized();
        if (m.Determinant() < 0f)
        {
            m = new Basis3(-m.X, -m.Y, -m.Z);
        }

        return m.GetQuaternion();
    }

    /// <summary>Godot's Basis::get_quaternion (the basis must be a rotation).</summary>
    private Quaternion GetQuaternion()
    {
        // m[row, column], as Godot's rows[row][column].
        Span<float> m = stackalloc float[9] { X.X, Y.X, Z.X, X.Y, Y.Y, Z.Y, X.Z, Y.Z, Z.Z };
        float trace = m[0] + m[4] + m[8];
        Span<float> t = stackalloc float[4];
        if (trace > 0f)
        {
            float s = MathF.Sqrt(trace + 1f);
            t[3] = s * 0.5f;
            s = 0.5f / s;
            t[0] = (m[(2 * 3) + 1] - m[(1 * 3) + 2]) * s;
            t[1] = (m[(0 * 3) + 2] - m[(2 * 3) + 0]) * s;
            t[2] = (m[(1 * 3) + 0] - m[(0 * 3) + 1]) * s;
        }
        else
        {
            int i = m[0] < m[4] ? (m[4] < m[8] ? 2 : 1) : (m[0] < m[8] ? 2 : 0);
            int j = (i + 1) % 3;
            int k = (i + 2) % 3;
            float s = MathF.Sqrt(m[(i * 3) + i] - m[(j * 3) + j] - m[(k * 3) + k] + 1f);
            t[i] = s * 0.5f;
            s = 0.5f / s;
            t[3] = (m[(k * 3) + j] - m[(j * 3) + k]) * s;
            t[j] = (m[(j * 3) + i] + m[(i * 3) + j]) * s;
            t[k] = (m[(k * 3) + i] + m[(i * 3) + k]) * s;
        }

        return new Quaternion(t[0], t[1], t[2], t[3]);
    }
}

/// <summary>An affine transform as Godot's Transform3D: <c>a * b</c> is parent a times local b, on column vectors.</summary>
public readonly struct Xform
{
    public readonly Basis3 Basis;
    public readonly Vector3 Origin;

    public Xform(Basis3 basis, Vector3 origin)
    {
        Basis = basis;
        Origin = origin;
    }

    public static Xform Identity => new(Basis3.Identity, Vector3.Zero);

    /// <summary>A bone's local transform: rotation, then its scale in its own axes, at the position.</summary>
    public static Xform FromTrs(Vector3 position, Quaternion rotation, Vector3 scale) =>
        new(Basis3.FromQuaternion(rotation).ScaledLocal(scale), position);

    public static Xform operator *(Xform a, Xform b) => new(a.Basis * b.Basis, (a.Basis * b.Origin) + a.Origin);

    public Xform AffineInverse()
    {
        Basis3 inverse = Basis.Inverse();
        return new Xform(inverse, -(inverse * Origin));
    }
}

/// <summary>Godot's quaternion operations. System.Numerics multiplies quaternions the same way (a * b applies b first).</summary>
public static class Quat
{
    /// <summary>Godot's CMP_EPSILON.</summary>
    private const float Epsilon = 0.00001f;

    public static Quaternion Normalized(Quaternion q) => Quaternion.Normalize(q);

    /// <summary>The inverse of a unit quaternion.</summary>
    public static Quaternion Inverse(Quaternion q) => Quaternion.Conjugate(q);

    public static Vector3 Rotate(Quaternion q, Vector3 v) => Vector3.Transform(v, q);

    public static Quaternion AxisAngle(Vector3 axis, float radians) => Quaternion.CreateFromAxisAngle(axis, radians);

    /// <summary>Godot's Quaternion.Slerp on unit quaternions: the short way round, linear when they nearly coincide.</summary>
    public static Quaternion Slerp(Quaternion from, Quaternion to, float weight)
    {
        float cosom = Quaternion.Dot(from, to);
        if (cosom < 0f)
        {
            cosom = -cosom;
            to = Quaternion.Negate(to);
        }

        float scale0, scale1;
        if (1f - cosom > Epsilon)
        {
            float omega = MathF.Acos(cosom);
            float sinom = MathF.Sin(omega);
            scale0 = MathF.Sin((1f - weight) * omega) / sinom;
            scale1 = MathF.Sin(weight * omega) / sinom;
        }
        else
        {
            scale0 = 1f - weight;
            scale1 = weight;
        }

        return new Quaternion(
            (scale0 * from.X) + (scale1 * to.X),
            (scale0 * from.Y) + (scale1 * to.Y),
            (scale0 * from.Z) + (scale1 * to.Z),
            (scale0 * from.W) + (scale1 * to.W));
    }

    /// <summary>Godot's IsEqualApprox against the identity.</summary>
    public static bool IsIdentity(Quaternion q) =>
        MathF.Abs(q.X) < Epsilon && MathF.Abs(q.Y) < Epsilon && MathF.Abs(q.Z) < Epsilon && MathF.Abs(q.W - 1f) < Epsilon;
}

/// <summary>
/// The simulation's UE space to the space the character files are made for (port of game/Scripts/Space/UeSpace.cs):
/// UE is unreal units, left-handed, X forward, Y right, Z up; the render space is metres, right-handed, Y up.
/// Positions: UE (x, y, z) becomes (x, z, y) / 100. Directions: (x, z, y). A glb's own space is this render space
/// seen from the mesh (after the actor's rotation and the mesh's RotOrigin and scale).
/// </summary>
public static class RenderSpace
{
    public const float UnitsPerMetre = 100f;

    public static Vector3 Direction(FVector direction) => new(direction.X, direction.Z, direction.Y);

    public static FVector DirectionToUe(Vector3 direction) => new(direction.X, direction.Z, direction.Y);

    /// <summary>
    /// The basis of a UE rotation: its columns are the rotated UE X, Z and Y axes (render X, Y and Z), each mapped.
    /// </summary>
    public static Basis3 Rotation(FRotator rotation)
    {
        rotation.GetAxes(out FVector x, out FVector y, out FVector z);
        return new Basis3(Direction(x), Direction(z), Direction(y));
    }

    /// <summary>
    /// The basis from a skeleton's (glb) space to the render space for a mesh drawn at <paramref name="yaw"/> (port
    /// of SkeletalModel.Place: the actor's rotation, then the mesh's scale in the actor's axes, then its RotOrigin).
    /// </summary>
    public static Basis3 SkeletonBasis(int yaw, FRotator rotOrigin, Vector3 meshScale) =>
        Rotation(new FRotator(0, yaw, 0)) *
        Rotation(rotOrigin).Scaled(Direction(new FVector(meshScale.X, meshScale.Y, meshScale.Z)));

    /// <summary>
    /// The rotation the pose build gives the aim bone (spec/native/aim-ik.md rule 4), in render space (port of
    /// game/Scripts/Views/AimPose.cs): the pitch about Z x <paramref name="facing"/>, then the yaw about Z, each as
    /// the axis-angle coords of 0x10fdd330 applied to vectors in UE space.
    /// </summary>
    public static Basis3 AimBasis(float yaw, float pitch, FVector facing)
    {
        var up = new FVector(0f, 0f, 1f);
        FVector side = (up ^ new FVector(facing.X, facing.Y, 0f)).SafeNormal();
        Vector3 x = Direction(Turn(DirectionToUe(Vector3.UnitX), side, pitch, up, yaw));
        Vector3 y = Direction(Turn(DirectionToUe(Vector3.UnitY), side, pitch, up, yaw));
        Vector3 z = Direction(Turn(DirectionToUe(Vector3.UnitZ), side, pitch, up, yaw));
        return new Basis3(x, y, z);
    }

    private static FVector Turn(FVector v, FVector pitchAxis, float pitch, FVector yawAxis, float yaw) =>
        Coords(yawAxis, yaw, Coords(pitchAxis, pitch, v));

    /// <summary>A vector transformed by the axis-angle coords (its rows dotted with the vector).</summary>
    private static FVector Coords(FVector n, float angle, FVector v)
    {
        float c = SvmMath.Cos(angle);
        float s = SvmMath.Sin(angle);
        float t = 1f - c;
        var xAxis = new FVector((n.X * n.X * t) + c, (n.X * n.Y * t) + (s * n.Z), (n.X * n.Z * t) - (s * n.Y));
        var yAxis = new FVector((n.X * n.Y * t) - (s * n.Z), (n.Y * n.Y * t) + c, (n.Y * n.Z * t) + (s * n.X));
        var zAxis = new FVector((n.X * n.Z * t) + (s * n.Y), (n.Y * n.Z * t) - (s * n.X), (n.Z * n.Z * t) + c);
        return new FVector(xAxis | v, yAxis | v, zAxis | v);
    }
}
