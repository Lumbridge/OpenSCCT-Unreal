using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Server;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>
/// One weapon or gadget mesh file as the equipment code needs it: its skeleton and clips (a
/// <see cref="GlbCharacter"/>), where it is on disk, the bones effects hang on and how the skeleton sits inside its
/// actor. Read once per file and shared.
/// <para>A mesh asset is named as the simulation names it, <c>Package.Mesh</c>, and lives at
/// <c>shared/chars/&lt;Package&gt;/&lt;Mesh&gt;.glb</c> (port of SkeletalModel.FindGlb in
/// game/Scripts/Render/Characters/SkeletalModel.cs).</para>
/// </summary>
public sealed class EquipmentModel
{
    private static readonly Dictionary<string, EquipmentModel?> s_loaded = new(StringComparer.OrdinalIgnoreCase);

    private static readonly Dictionary<string, string> s_failures = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Why a mesh file that exists could not be read (by asset name), or null when it was read or never tried.</summary>
    public static string? FailureOf(string asset)
    {
        lock (s_loaded)
        {
            return s_failures.GetValueOrDefault(asset);
        }
    }

    private EquipmentModel(string asset, string path, GlbCharacter character, Xform skeletonFrame)
    {
        Asset = asset;
        Path = path;
        Character = character;
        AssetId = EquipmentNames.Id(asset);
        PathId = EquipmentNames.Id(RelativeToContent(path));
        MuzzleBone = character.FindBone("FX");
        CasingBone = character.FindBone("FX_DOUILLE");
        LaserBone = character.FindBone("Laser");
        // The actor's frame to the skeleton's: the mesh's RotOrigin and scale (SkeletalModel.Place), then whatever
        // nodes the file has above its skeleton.
        ActorToSkeleton = new Xform(RenderSpace.SkeletonBasis(0, character.RotOrigin, character.MeshScale), Vector3.Zero) * skeletonFrame;
    }

    /// <summary>The asset as the simulation names it ("SWeaponFirst.M160").</summary>
    public string Asset { get; }

    public string Path { get; }

    public GlbCharacter Character { get; }

    public int AssetId { get; }

    /// <summary>Name id of the file's path relative to the content root, with forward slashes.</summary>
    public int PathId { get; }

    /// <summary>The muzzle ("FX"), the casing ejection ("FX_DOUILLE") and the laser socket ("Laser"); -1 when the skeleton lacks one.</summary>
    public int MuzzleBone { get; }

    public int CasingBone { get; }

    public int LaserBone { get; }

    /// <summary>
    /// The skeleton's transform inside its actor, in render space (metres, Y up): a point of the skeleton times this
    /// is the point in the actor's frame, where X is the actor's forward, Y its up and Z its right.
    /// </summary>
    public Xform ActorToSkeleton { get; }

    /// <summary>The file of a mesh asset under <paramref name="sharedRoot"/>, or null when the content has none.</summary>
    public static string? FindGlb(string asset, string? sharedRoot)
    {
        if (string.IsNullOrEmpty(asset) || sharedRoot is null)
        {
            return null;
        }

        int dot = asset.IndexOf('.', StringComparison.Ordinal);
        if (dot <= 0)
        {
            return null;
        }

        string path = System.IO.Path.Combine(sharedRoot, "chars", asset[..dot], asset[(dot + 1)..] + ".glb");
        return File.Exists(path) ? path : null;
    }

    /// <summary>
    /// The model of a mesh asset, or null when the content has no such file or the file is not a skinned model the
    /// pose code can read (a bad file must not stop the match: the caller draws nothing for it).
    /// </summary>
    public static EquipmentModel? Load(string? asset, string? sharedRoot)
    {
        if (string.IsNullOrEmpty(asset) || FindGlb(asset, sharedRoot) is not { } path)
        {
            return null;
        }

        lock (s_loaded)
        {
            if (!s_loaded.TryGetValue(path, out EquipmentModel? model))
            {
                try
                {
                    model = new EquipmentModel(asset, path, GlbCharacter.Load(path), ReadSkeletonFrame(path));
                }
                catch (Exception e)
                {
                    // Whatever is wrong with the file (cut short, not skinned, a node or accessor out of range): the
                    // pawn then carries nothing drawn for it, and the match goes on. The reason is kept for whoever
                    // asks why a mesh is missing.
                    model = null;
                    s_failures[asset] = e.GetType().Name + ": " + e.Message;
                }

                s_loaded[path] = model;
            }

            return model;
        }
    }

    /// <summary>A bone's transform in the skeleton's space from a pose given bone by bone relative to the parent.</summary>
    public Xform Global(int bone, ReadOnlySpan<Vector3> positions, ReadOnlySpan<Quaternion> rotations, ReadOnlySpan<Vector3> scales)
    {
        int[] parents = Character.Parents;
        Xform pose = Xform.FromTrs(positions[bone], rotations[bone], scales[bone]);
        for (int parent = parents[bone]; parent >= 0; parent = parents[parent])
        {
            pose = Xform.FromTrs(positions[parent], rotations[parent], scales[parent]) * pose;
        }

        return pose;
    }

    /// <summary>
    /// Writes the pose of <paramref name="clip"/> at <paramref name="frame"/> of its length (0 to 1): every bone at
    /// rest, then the clip's keys over the bones it has tracks for.
    /// </summary>
    public void Sample(GlbClip clip, float frame, Span<Vector3> positions, Span<Quaternion> rotations, Span<Vector3> scales)
    {
        GlbCharacter c = Character;
        c.RestPosition.CopyTo(positions);
        c.RestRotation.CopyTo(rotations);
        c.RestScale.CopyTo(scales);
        double time = Math.Clamp(frame, 0f, 1f) * clip.Length;
        GlbTrack[] tracks = clip.Tracks;
        for (int t = 0; t < tracks.Length; t++)
        {
            int bone = tracks[t].Bone;
            switch (tracks[t].Type)
            {
                case GlbTrackType.Position:
                    positions[bone] = clip.Position(t, time);
                    break;
                case GlbTrackType.Rotation:
                    rotations[bone] = clip.Rotation(t, time);
                    break;
                case GlbTrackType.Scale:
                    scales[bone] = clip.Scale(t, time);
                    break;
            }
        }
    }

    private static string RelativeToContent(string path)
    {
        string full = System.IO.Path.GetFullPath(path);
        if (ContentPaths.Root is { } root)
        {
            string relative = System.IO.Path.GetRelativePath(System.IO.Path.GetFullPath(root), full);
            if (!relative.StartsWith("..", StringComparison.Ordinal) && !System.IO.Path.IsPathRooted(relative))
            {
                return relative.Replace('\\', '/');
            }
        }

        // Content outside the configured root (a map's own shared folder): the full path.
        return full.Replace('\\', '/');
    }

    /// <summary>
    /// The transform of the nodes above the skeleton's root joint. The converter writes the game's own meshes with the
    /// skeleton at the scene's top (identity here); OpenSCCT's own pistols keep theirs under a placed node
    /// ("MercPistolFrame"), which <see cref="GlbCharacter"/> does not read. Only the nodes' rest transforms are
    /// read: a clip that moves one of these nodes (the pistols' "Root") is not followed.
    /// </summary>
    private static Xform ReadSkeletonFrame(string path)
    {
        using FileStream file = File.OpenRead(path);
        Span<byte> header = stackalloc byte[20];
        file.ReadExactly(header);
        if (BinaryPrimitives.ReadUInt32LittleEndian(header) != 0x46546C67 || BinaryPrimitives.ReadUInt32LittleEndian(header[16..]) != 0x4E4F534A)
        {
            throw new InvalidDataException($"{path}: not a glb with a JSON chunk first.");
        }

        byte[] json = new byte[BinaryPrimitives.ReadInt32LittleEndian(header[12..])];
        file.ReadExactly(json);
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement root = document.RootElement;
        var nodes = new List<JsonElement>();
        foreach (JsonElement node in root.GetProperty("nodes").EnumerateArray())
        {
            nodes.Add(node);
        }

        var parent = new int[nodes.Count];
        Array.Fill(parent, -1);
        for (int n = 0; n < nodes.Count; n++)
        {
            if (nodes[n].TryGetProperty("children", out JsonElement children))
            {
                foreach (JsonElement child in children.EnumerateArray())
                {
                    parent[child.GetInt32()] = n;
                }
            }
        }

        var joints = new HashSet<int>();
        int first = -1;
        foreach (JsonElement joint in root.GetProperty("skins")[0].GetProperty("joints").EnumerateArray())
        {
            joints.Add(joint.GetInt32());
            first = first < 0 ? joint.GetInt32() : first;
        }

        // Up from the first joint to the top of the skeleton, then on through the nodes that are not joints.
        int at = first;
        while (at >= 0 && parent[at] >= 0 && joints.Contains(parent[at]))
        {
            at = parent[at];
        }

        Xform frame = Xform.Identity;
        for (int n = at < 0 ? -1 : parent[at]; n >= 0; n = parent[n])
        {
            frame = NodeTransform(nodes[n]) * frame;
        }

        return frame;
    }

    private static Xform NodeTransform(JsonElement node)
    {
        if (node.TryGetProperty("matrix", out JsonElement m))
        {
            // glTF matrices are column-major: three basis columns, then the translation.
            return new Xform(
                new Basis3(
                    new Vector3(m[0].GetSingle(), m[1].GetSingle(), m[2].GetSingle()),
                    new Vector3(m[4].GetSingle(), m[5].GetSingle(), m[6].GetSingle()),
                    new Vector3(m[8].GetSingle(), m[9].GetSingle(), m[10].GetSingle())),
                new Vector3(m[12].GetSingle(), m[13].GetSingle(), m[14].GetSingle()));
        }

        Vector3 position = node.TryGetProperty("translation", out JsonElement t)
            ? new Vector3(t[0].GetSingle(), t[1].GetSingle(), t[2].GetSingle()) : Vector3.Zero;
        Quaternion rotation = node.TryGetProperty("rotation", out JsonElement r)
            ? Quaternion.Normalize(new Quaternion(r[0].GetSingle(), r[1].GetSingle(), r[2].GetSingle(), r[3].GetSingle())) : Quaternion.Identity;
        Vector3 scale = node.TryGetProperty("scale", out JsonElement s)
            ? new Vector3(s[0].GetSingle(), s[1].GetSingle(), s[2].GetSingle()) : Vector3.One;
        return Xform.FromTrs(position, rotation, scale);
    }
}

/// <summary>Small conversions the equipment code shares.</summary>
internal static class EquipmentMath
{
    /// <summary>UE rotator units (65536 a turn) as degrees from -180 to 180.</summary>
    public static float Degrees(int rotatorUnits) => (short)rotatorUnits * (360f / 65536f);

    /// <summary>A transform as the struct pose bones cross in: translation, unit rotation, scale.</summary>
    public static BridgeBone ToBone(in Xform x)
    {
        Vector3 scale = x.Basis.GetScale();
        Quaternion q = Quaternion.Normalize(x.Basis.GetRotationQuaternion());
        return new BridgeBone
        {
            Px = x.Origin.X,
            Py = x.Origin.Y,
            Pz = x.Origin.Z,
            Qx = q.X,
            Qy = q.Y,
            Qz = q.Z,
            Qw = q.W,
            Sx = scale.X,
            Sy = scale.Y,
            Sz = scale.Z,
        };
    }

    public static Xform FromBone(in BridgeBone b) =>
        Xform.FromTrs(new Vector3(b.Px, b.Py, b.Pz), new Quaternion(b.Qx, b.Qy, b.Qz, b.Qw), new Vector3(b.Sx, b.Sy, b.Sz));

    /// <summary>A UE place (unreal units, UE axes) and rotation as a render-space transform (port of UeSpace.TransformToGodot).</summary>
    public static Xform FromUe(FVector location, FRotator rotation) =>
        new(RenderSpace.Rotation(rotation), RenderSpace.Direction(location) / RenderSpace.UnitsPerMetre);

    /// <summary>Godot's Transform3D.InterpolateWith: the origin and scale lerped, the rotation slerped.</summary>
    public static Xform Interpolate(in Xform from, in Xform to, float weight)
    {
        Vector3 scale = Vector3.Lerp(from.Basis.GetScale(), to.Basis.GetScale(), weight);
        Quaternion rotation = Quat.Slerp(Quaternion.Normalize(from.Basis.GetRotationQuaternion()), Quaternion.Normalize(to.Basis.GetRotationQuaternion()), weight);
        return new Xform(Basis3.FromQuaternion(Quaternion.Normalize(rotation)).ScaledLocal(scale), Vector3.Lerp(from.Origin, to.Origin, weight));
    }

    /// <summary>Godot's Quaternion(from, to): the shortest turn taking one direction to another.</summary>
    public static Quaternion ShortestArc(Vector3 from, Vector3 to)
    {
        Vector3 a = Vector3.Normalize(from);
        Vector3 b = Vector3.Normalize(to);
        float d = Vector3.Dot(a, b);
        if (d < -1f + 1e-6f)
        {
            // Opposite directions: half a turn about any axis across them.
            Vector3 axis = MathF.Abs(a.X) > MathF.Abs(a.Z) ? new Vector3(-a.Y, a.X, 0f) : new Vector3(0f, -a.Z, a.Y);
            return Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), MathF.PI);
        }

        Vector3 c = Vector3.Cross(a, b);
        float s = MathF.Sqrt((1f + d) * 2f);
        return Quaternion.Normalize(new Quaternion(c.X / s, c.Y / s, c.Z / s, s * 0.5f));
    }
}
