using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text.Json;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Pose;

public enum GlbTrackType
{
    Position,
    Rotation,
    Scale,
}

/// <summary>One bone track of a clip: the keys of a glTF animation channel, as stored (the glb's own space).</summary>
public sealed class GlbTrack
{
    internal GlbTrack(int bone, GlbTrackType type, bool step, float[] times, float[] values)
    {
        Bone = bone;
        Type = type;
        Step = step;
        Times = times;
        Values = values;
    }

    /// <summary>Index of the bone in the skin's joint order.</summary>
    public int Bone { get; }

    public GlbTrackType Type { get; }

    /// <summary>STEP interpolation: a key holds until the next one.</summary>
    public bool Step { get; }

    public float[] Times { get; }

    /// <summary>3 floats a key (position, scale) or 4 (rotation: x, y, z, w, unit length).</summary>
    public float[] Values { get; }
}

/// <summary>
/// One sequence of a character file. The keys are read from the file on the first use of the clip (a character has
/// hundreds of clips and a match plays a few dozen). Sampling follows Godot's linear tracks without loop wrap, which
/// is what the Godot client samples (game/Scripts/Render/Characters/AuthoredAnimationTracks.cs keeps the file's exact
/// keys): the first key before it, the last key after it, lerp for position and scale, slerp the short way for
/// rotation.
/// </summary>
public sealed class GlbClip
{
    private readonly GlbCharacter _owner;
    private readonly GlbCharacter.ChannelSource[] _sources;
    private GlbTrack[]? _tracks;
    private float _length;

    internal GlbClip(GlbCharacter owner, string name, GlbCharacter.ChannelSource[] sources)
    {
        _owner = owner;
        Name = name;
        _sources = sources;
    }

    public string Name { get; }

    public GlbTrack[] Tracks => _tracks ?? Read();

    /// <summary>Seconds of the sequence at rate 1 (its last key).</summary>
    public float Length
    {
        get
        {
            _ = Tracks;
            return _length;
        }
    }

    public Vector3 Position(int track, double time) => Vector(Tracks[track], time);

    public Vector3 Scale(int track, double time) => Vector(Tracks[track], time);

    public Quaternion Rotation(int track, double time)
    {
        GlbTrack t = Tracks[track];
        int key = KeyAt(t.Times, time, out float c);
        Quaternion a = QuaternionAt(t.Values, key);
        return c <= 0f || t.Step ? a : Quat.Slerp(a, QuaternionAt(t.Values, key + 1), c);
    }

    private static Vector3 Vector(GlbTrack t, double time)
    {
        int key = KeyAt(t.Times, time, out float c);
        Vector3 a = VectorAt(t.Values, key);
        return c <= 0f || t.Step ? a : Vector3.Lerp(a, VectorAt(t.Values, key + 1), c);
    }

    /// <summary>
    /// The key at or before <paramref name="time"/> and how far towards the next one the time is (0 at a key, before
    /// the first key and from the last key on: the track is clamped to its key range).
    /// </summary>
    private static int KeyAt(float[] times, double time, out float c)
    {
        c = 0f;
        int last = times.Length - 1;
        if (last <= 0 || time <= times[0])
        {
            return 0;
        }

        if (time >= times[last])
        {
            return last;
        }

        int low = 0, high = last;
        while (high - low > 1)
        {
            int middle = (low + high) >> 1;
            if (times[middle] <= time)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }

        c = (float)((time - times[low]) / ((double)times[high] - times[low]));
        return low;
    }

    private static Vector3 VectorAt(float[] values, int key) => new(values[key * 3], values[(key * 3) + 1], values[(key * 3) + 2]);

    private static Quaternion QuaternionAt(float[] values, int key) =>
        new(values[key * 4], values[(key * 4) + 1], values[(key * 4) + 2], values[(key * 4) + 3]);

    private GlbTrack[] Read()
    {
        var tracks = new GlbTrack[_sources.Length];
        float length = 0f;
        for (int i = 0; i < _sources.Length; i++)
        {
            GlbCharacter.ChannelSource source = _sources[i];
            string what = $"{_owner.Path}: {Name}/{_owner.BoneNames[source.Bone]}";
            float[] times = _owner.ReadFloats(source.Input, 1, what);
            int width = source.Type == GlbTrackType.Rotation ? 4 : 3;
            float[] values = _owner.ReadFloats(source.Output, width, what);
            if (times.Length == 0 || values.Length != times.Length * width)
            {
                throw new InvalidDataException($"{what}: animation time and value counts do not match.");
            }

            for (int key = 0; key < times.Length; key++)
            {
                float time = times[key];
                if (!float.IsFinite(time) || time < 0f || (key > 0 && time <= times[key - 1]))
                {
                    throw new InvalidDataException($"{what}: invalid animation key time {time}.");
                }

                length = MathF.Max(length, time);
            }

            foreach (float value in values)
            {
                if (!float.IsFinite(value))
                {
                    throw new InvalidDataException($"{what}: non-finite animation key.");
                }
            }

            if (source.Type == GlbTrackType.Rotation)
            {
                // Keys are stored a little off unit length; the slerp needs unit quaternions (the Godot client
                // normalizes them when it inserts the keys).
                for (int key = 0; key < times.Length; key++)
                {
                    Quaternion q = QuaternionAt(values, key);
                    if (q.LengthSquared() < 0.000001f)
                    {
                        throw new InvalidDataException($"{what}: zero animation quaternion.");
                    }

                    q = Quaternion.Normalize(q);
                    values[key * 4] = q.X;
                    values[(key * 4) + 1] = q.Y;
                    values[(key * 4) + 2] = q.Z;
                    values[(key * 4) + 3] = q.W;
                }
            }

            tracks[i] = new GlbTrack(source.Bone, source.Type, source.Step, times, values);
        }

        _length = MathF.Max(length, 0.001f);
        _tracks = tracks;
        return tracks;
    }
}

/// <summary>
/// A character file (<c>shared/chars/&lt;Package&gt;/&lt;Mesh&gt;.glb</c>) as the pose build needs it: the skin's bones in
/// joint order with their rest pose, the clips by sequence name and the mesh placement the converter wrote in
/// <c>extras.svmap</c>. Everything is in the file's own space (metres, Y up, right-handed). Read once per path
/// (port of the parts of game/Scripts/Render/Characters/SkeletalModel.cs that do not draw).
/// </summary>
public sealed class GlbCharacter
{
    private static readonly Dictionary<string, GlbCharacter> s_loaded = new(StringComparer.OrdinalIgnoreCase);

    private readonly byte[] _bin;
    private readonly AccessorSource[] _accessors;
    private readonly Dictionary<string, GlbClip> _clips = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, int> _bones = new(StringComparer.Ordinal);

    internal readonly record struct ChannelSource(int Bone, GlbTrackType Type, bool Step, int Input, int Output);

    /// <summary>Where an accessor's floats are in the binary chunk; Count &lt; 0 for one the pose code cannot read.</summary>
    private readonly record struct AccessorSource(int Offset, int Count, int Components, string? Problem);

    private GlbCharacter(string path, JsonElement root, byte[] bin)
    {
        Path = path;
        _bin = bin;
        _accessors = ReadAccessors(root);

        if (!root.TryGetProperty("skins", out JsonElement skins) || skins.GetArrayLength() == 0)
        {
            throw new InvalidDataException($"{path}: no skin.");
        }

        // Indexing a JSON array walks it from the start, so the nodes are listed once.
        var nodes = new List<JsonElement>();
        foreach (JsonElement node in root.GetProperty("nodes").EnumerateArray())
        {
            nodes.Add(node);
        }

        var parentNode = new int[nodes.Count];
        Array.Fill(parentNode, -1);
        for (int n = 0; n < nodes.Count; n++)
        {
            if (nodes[n].TryGetProperty("children", out JsonElement children))
            {
                foreach (JsonElement child in children.EnumerateArray())
                {
                    parentNode[child.GetInt32()] = n;
                }
            }
        }

        var boneOfNode = new int[nodes.Count];
        Array.Fill(boneOfNode, -1);
        var joints = new List<int>();
        foreach (JsonElement joint in skins[0].GetProperty("joints").EnumerateArray())
        {
            boneOfNode[joint.GetInt32()] = joints.Count;
            joints.Add(joint.GetInt32());
        }

        int count = joints.Count;
        BoneNames = new string[count];
        Parents = new int[count];
        RestPosition = new Vector3[count];
        RestRotation = new Quaternion[count];
        RestScale = new Vector3[count];
        for (int b = 0; b < count; b++)
        {
            JsonElement node = nodes[joints[b]];
            if (node.TryGetProperty("matrix", out _))
            {
                throw new InvalidDataException($"{path}: joint {b} has a matrix; the pose build needs translation, rotation and scale.");
            }

            BoneNames[b] = node.TryGetProperty("name", out JsonElement name) ? name.GetString() ?? "" : "";
            _bones[BoneNames[b]] = b;
            // A joint under a node that is not a joint is a root: the converter writes the skeleton at the scene's top.
            Parents[b] = parentNode[joints[b]] >= 0 ? boneOfNode[parentNode[joints[b]]] : -1;
            RestPosition[b] = node.TryGetProperty("translation", out JsonElement t) ? Vector3Of(t) : Vector3.Zero;
            RestRotation[b] = node.TryGetProperty("rotation", out JsonElement r)
                ? Quaternion.Normalize(new Quaternion(r[0].GetSingle(), r[1].GetSingle(), r[2].GetSingle(), r[3].GetSingle()))
                : Quaternion.Identity;
            RestScale[b] = node.TryGetProperty("scale", out JsonElement s) ? Vector3Of(s) : Vector3.One;
        }

        if (root.TryGetProperty("extras", out JsonElement extras) && extras.TryGetProperty("svmap", out JsonElement sv))
        {
            if (sv.TryGetProperty("rotOrigin", out JsonElement rot) && rot.GetArrayLength() >= 3)
            {
                RotOrigin = new FRotator(rot[0].GetInt32(), rot[1].GetInt32(), rot[2].GetInt32());
            }

            if (sv.TryGetProperty("meshScale", out JsonElement scale) && scale.GetArrayLength() >= 3)
            {
                MeshScale = Vector3Of(scale);
            }
        }

        if (root.TryGetProperty("animations", out JsonElement animations))
        {
            ReadClips(animations, boneOfNode);
        }
    }

    public string Path { get; }

    /// <summary>The bones in the skin's joint order (the order of the package's bones, which Pawn.BoneMirrors counts).</summary>
    public string[] BoneNames { get; }

    /// <summary>Each bone's parent in the same order, -1 for a root.</summary>
    public int[] Parents { get; }

    public Vector3[] RestPosition { get; }

    public Quaternion[] RestRotation { get; }

    public Vector3[] RestScale { get; }

    /// <summary>The mesh's RotOrigin (UE rotator units): how the mesh is turned inside its actor.</summary>
    public FRotator RotOrigin { get; }

    /// <summary>The mesh's scale per UE axis.</summary>
    public Vector3 MeshScale { get; } = Vector3.One;

    public int BoneCount => BoneNames.Length;

    public int ClipCount => _clips.Count;

    /// <summary>The index of a bone by name, or -1.</summary>
    public int FindBone(string name) => _bones.TryGetValue(name, out int b) ? b : -1;

    /// <summary>The clip of a sequence by name (case does not matter, as UE names), or null.</summary>
    public GlbClip? Find(string name) => _clips.TryGetValue(name, out GlbClip? clip) ? clip : null;

    /// <summary>The character of <paramref name="path"/>, read on the first call.</summary>
    public static GlbCharacter Load(string path)
    {
        string full = System.IO.Path.GetFullPath(path);
        lock (s_loaded)
        {
            if (!s_loaded.TryGetValue(full, out GlbCharacter? character))
            {
                character = Read(full);
                s_loaded[full] = character;
            }

            return character;
        }
    }

    private static GlbCharacter Read(string path)
    {
        using FileStream file = File.OpenRead(path);
        Span<byte> header = stackalloc byte[12];
        file.ReadExactly(header);
        if (BinaryPrimitives.ReadUInt32LittleEndian(header) != 0x46546C67 || BinaryPrimitives.ReadUInt32LittleEndian(header[4..]) != 2)
        {
            throw new InvalidDataException($"{path}: not a glTF 2 binary file.");
        }

        byte[]? json = null, bin = null;
        Span<byte> chunk = stackalloc byte[8];
        while (file.Position + 8 <= file.Length && (json is null || bin is null))
        {
            file.ReadExactly(chunk);
            int length = BinaryPrimitives.ReadInt32LittleEndian(chunk);
            uint type = BinaryPrimitives.ReadUInt32LittleEndian(chunk[4..]);
            if (length < 0 || file.Position + length > file.Length)
            {
                throw new InvalidDataException($"{path}: a chunk runs past the end of the file.");
            }

            if (type == 0x4E4F534A && json is null)
            {
                json = new byte[length];
                file.ReadExactly(json);
            }
            else if (type == 0x004E4942 && bin is null)
            {
                bin = new byte[length];
                file.ReadExactly(bin);
            }
            else
            {
                file.Seek(length, SeekOrigin.Current);
            }
        }

        if (json is null)
        {
            throw new InvalidDataException($"{path}: no JSON chunk.");
        }

        using JsonDocument document = JsonDocument.Parse(json);
        return new GlbCharacter(path, document.RootElement, bin ?? []);
    }

    private void ReadClips(JsonElement animations, int[] boneOfNode)
    {
        var samplers = new List<(int Input, int Output, string Interpolation)>();
        var sources = new List<ChannelSource>();
        var seen = new HashSet<(int, GlbTrackType)>();
        foreach (JsonElement animation in animations.EnumerateArray())
        {
            string name = animation.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? "" : "";
            if (name.Length == 0 || _clips.ContainsKey(name))
            {
                throw new InvalidDataException($"{Path}: missing or duplicate animation name '{name}'.");
            }

            samplers.Clear();
            foreach (JsonElement sampler in animation.GetProperty("samplers").EnumerateArray())
            {
                samplers.Add((sampler.GetProperty("input").GetInt32(), sampler.GetProperty("output").GetInt32(),
                    sampler.TryGetProperty("interpolation", out JsonElement i) ? i.GetString() ?? "LINEAR" : "LINEAR"));
            }

            sources.Clear();
            seen.Clear();
            foreach (JsonElement channel in animation.GetProperty("channels").EnumerateArray())
            {
                JsonElement target = channel.GetProperty("target");
                if (!target.TryGetProperty("node", out JsonElement node))
                {
                    continue;
                }

                string path = target.GetProperty("path").GetString() ?? "";
                if (path == "weights")
                {
                    continue;
                }

                GlbTrackType type = path switch
                {
                    "translation" => GlbTrackType.Position,
                    "rotation" => GlbTrackType.Rotation,
                    "scale" => GlbTrackType.Scale,
                    _ => throw new InvalidDataException($"{Path}: {name}: unsupported animation path '{path}'."),
                };
                int bone = boneOfNode[node.GetInt32()];
                if (bone < 0 || !seen.Add((bone, type)))
                {
                    throw new InvalidDataException($"{Path}: {name}: a track of a node that is not a bone, or a second {path} track of one bone.");
                }

                (int input, int output, string interpolation) = samplers[channel.GetProperty("sampler").GetInt32()];
                bool step = interpolation switch
                {
                    "LINEAR" => false,
                    "STEP" => true,
                    _ => throw new InvalidDataException(
                        $"{Path}: {name}/{BoneNames[bone]}: {interpolation} keys are not supported (the converter writes LINEAR)."),
                };
                sources.Add(new ChannelSource(bone, type, step, input, output));
            }

            _clips[name] = new GlbClip(this, name, sources.ToArray());
        }
    }

    /// <summary>Lists where every accessor's data is, so a clip can read its keys later without the JSON.</summary>
    private static AccessorSource[] ReadAccessors(JsonElement root)
    {
        var views = new List<(int Offset, int Length, int Stride, int Buffer)>();
        if (root.TryGetProperty("bufferViews", out JsonElement bufferViews))
        {
            foreach (JsonElement view in bufferViews.EnumerateArray())
            {
                views.Add((view.TryGetProperty("byteOffset", out JsonElement o) ? o.GetInt32() : 0, view.GetProperty("byteLength").GetInt32(),
                    view.TryGetProperty("byteStride", out JsonElement s) ? s.GetInt32() : 0,
                    view.TryGetProperty("buffer", out JsonElement b) ? b.GetInt32() : 0));
            }
        }

        var accessors = new List<AccessorSource>();
        if (!root.TryGetProperty("accessors", out JsonElement list))
        {
            return [];
        }

        foreach (JsonElement accessor in list.EnumerateArray())
        {
            int components = accessor.GetProperty("type").GetString() switch
            {
                "SCALAR" => 1,
                "VEC2" => 2,
                "VEC3" => 3,
                "VEC4" => 4,
                _ => 0,
            };
            int count = accessor.GetProperty("count").GetInt32();
            string? problem = null;
            int offset = 0;
            if (accessor.GetProperty("componentType").GetInt32() != 5126)
            {
                problem = "is not made of floats";
            }
            else if (accessor.TryGetProperty("sparse", out _))
            {
                problem = "is sparse";
            }
            else if (!accessor.TryGetProperty("bufferView", out JsonElement viewIndex) || viewIndex.GetInt32() >= views.Count)
            {
                problem = "has no buffer view";
            }
            else
            {
                (int viewOffset, int viewLength, int stride, int buffer) = views[viewIndex.GetInt32()];
                int own = accessor.TryGetProperty("byteOffset", out JsonElement o) ? o.GetInt32() : 0;
                offset = viewOffset + own;
                if (buffer != 0)
                {
                    problem = "is not in the file's binary chunk";
                }
                else if (stride != 0 && stride != components * 4)
                {
                    problem = "is interleaved";
                }
                else if (own + ((long)count * components * 4) > viewLength)
                {
                    problem = "runs past its buffer view";
                }
            }

            accessors.Add(new AccessorSource(offset, count, components, problem));
        }

        return accessors.ToArray();
    }

    /// <summary>The floats of accessor <paramref name="index"/>, which must hold <paramref name="components"/> a key.</summary>
    internal float[] ReadFloats(int index, int components, string what)
    {
        if (index < 0 || index >= _accessors.Length)
        {
            throw new InvalidDataException($"{what}: accessor {index} does not exist.");
        }

        AccessorSource accessor = _accessors[index];
        if (accessor.Problem is { } problem)
        {
            throw new InvalidDataException($"{what}: accessor {index} {problem}.");
        }

        int bytes = accessor.Count * accessor.Components * 4;
        if (accessor.Components != components || accessor.Offset < 0 || accessor.Offset + (long)bytes > _bin.Length)
        {
            throw new InvalidDataException($"{what}: accessor {index} has the wrong shape or runs past the file's data.");
        }

        var values = new float[accessor.Count * accessor.Components];
        ReadOnlySpan<byte> source = _bin.AsSpan(accessor.Offset, bytes);
        if (BitConverter.IsLittleEndian)
        {
            source.CopyTo(MemoryMarshal.AsBytes(values.AsSpan()));
        }
        else
        {
            for (int i = 0; i < values.Length; i++)
            {
                values[i] = BinaryPrimitives.ReadSingleLittleEndian(source[(i * 4)..]);
            }
        }

        return values;
    }

    private static Vector3 Vector3Of(JsonElement array) => new(array[0].GetSingle(), array[1].GetSingle(), array[2].GetSingle());
}
