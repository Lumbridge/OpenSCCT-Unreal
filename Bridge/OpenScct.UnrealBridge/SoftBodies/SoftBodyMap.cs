using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.SoftBodies;

/// <summary>
/// One soft body instance of a map as render/world.glb has it: the instance's place, its softBody block and the
/// triangles and texture coordinates of its library mesh (vertex i and i + N, the back face, on particle i).
/// </summary>
internal sealed class SoftBodySource
{
    /// <summary>The simulation actor's map name: the Unreal level actor is tagged "Scct:" + this.</summary>
    public required string Actor { get; init; }

    public required JsonElement Block { get; init; }

    public FVector Location { get; init; }

    /// <summary>The placed rotation, in the original's 65536-per-turn units.</summary>
    public FRotator Rotation { get; init; }

    public FVector Scale { get; init; } = new(1f, 1f, 1f);

    public FVector PrePivot { get; init; }

    /// <summary>The mesh's triangles as glTF lists them (counter-clockwise), indices into 2N vertices.</summary>
    public required int[] Indices { get; init; }

    /// <summary>Two floats a vertex, 2N vertices.</summary>
    public required float[] Uvs { get; init; }

    /// <summary>The library mesh's normals of the front vertices, in the actor's local space (uu axes); empty when the mesh has none.</summary>
    public required FVector[] FrontNormals { get; init; }
}

/// <summary>
/// Reads the soft bodies of a map from render/world.glb: the instances with a softBody block (as the Godot client's
/// MapBuilder.AddInstance takes them, hidden ones left out as the importer leaves them out) and, from the binary
/// chunk, the triangles and texture coordinates of each one's library mesh. Positions are not read: the simulation's
/// particles are the vertices.
/// </summary>
internal static class SoftBodyMap
{
    private const uint GlbMagic = 0x46546C67; // "glTF"
    private const uint JsonChunk = 0x4E4F534A; // "JSON"
    private const uint BinChunk = 0x004E4942; // "BIN\0"

    /// <summary>The instances found, and what was wrong with the ones that could not be read.</summary>
    public static (List<SoftBodySource> Bodies, List<string> Warnings, JsonDocument? Document) Load(string mapFolder)
    {
        var bodies = new List<SoftBodySource>();
        var warnings = new List<string>();
        string path = Path.Combine(mapFolder, "render", "world.glb");
        if (!File.Exists(path))
        {
            warnings.Add("world.glb is missing");
            return (bodies, warnings, null);
        }

        byte[] file = File.ReadAllBytes(path);
        if (file.Length < 20 || BinaryPrimitives.ReadUInt32LittleEndian(file) != GlbMagic ||
            BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(16)) != JsonChunk)
        {
            warnings.Add("world.glb is not a glb file with a leading JSON chunk");
            return (bodies, warnings, null);
        }

        int jsonLength = BinaryPrimitives.ReadInt32LittleEndian(file.AsSpan(12));
        int binHeader = 20 + jsonLength;
        if (jsonLength <= 0 || binHeader + 8 > file.Length ||
            BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(binHeader + 4)) != BinChunk)
        {
            warnings.Add("world.glb has no binary chunk after its JSON");
            return (bodies, warnings, null);
        }

        int binLength = BinaryPrimitives.ReadInt32LittleEndian(file.AsSpan(binHeader));
        ReadOnlySpan<byte> bin = file.AsSpan(binHeader + 8, Math.Min(binLength, file.Length - binHeader - 8));
        JsonDocument doc = JsonDocument.Parse(file.AsMemory(20, jsonLength));
        JsonElement root = doc.RootElement;
        if (!root.TryGetProperty("extras", out JsonElement extras) || !extras.TryGetProperty("svmap", out JsonElement svmap) ||
            !svmap.TryGetProperty("instances", out JsonElement instances) || instances.ValueKind != JsonValueKind.Array)
        {
            return (bodies, warnings, doc);
        }

        var meshOfNode = new Dictionary<string, int>(StringComparer.Ordinal);
        if (root.TryGetProperty("nodes", out JsonElement nodes))
        {
            foreach (JsonElement n in nodes.EnumerateArray())
            {
                if (n.TryGetProperty("name", out JsonElement name) && n.TryGetProperty("mesh", out JsonElement mesh))
                {
                    meshOfNode.TryAdd(name.GetString() ?? "", mesh.GetInt32());
                }
            }
        }

        foreach (JsonElement i in instances.EnumerateArray())
        {
            if (!i.TryGetProperty("softBody", out JsonElement block) || block.ValueKind != JsonValueKind.Object ||
                Flag(i, "hidden") || Flag(i, "skyZone"))
            {
                continue;
            }

            string actor = Str(i, "actor") ?? "";
            string meshName = Str(i, "mesh") ?? "";
            if (!meshOfNode.TryGetValue(meshName, out int meshIndex))
            {
                warnings.Add(actor + ": mesh " + meshName + " is not in the file");
                continue;
            }

            try
            {
                JsonElement prim = root.GetProperty("meshes")[meshIndex].GetProperty("primitives")[0];
                JsonElement attributes = prim.GetProperty("attributes");
                int vertices = root.GetProperty("accessors")[attributes.GetProperty("POSITION").GetInt32()].GetProperty("count").GetInt32();
                int[] indices = ReadIndices(root, bin, prim.GetProperty("indices").GetInt32());
                float[] uvs = attributes.TryGetProperty("TEXCOORD_0", out JsonElement uv)
                    ? ReadFloats(root, bin, uv.GetInt32(), 2)
                    : new float[vertices * 2];
                FVector[] normals = [];
                if (attributes.TryGetProperty("NORMAL", out JsonElement nrm))
                {
                    float[] raw = ReadFloats(root, bin, nrm.GetInt32(), 3);
                    // The converter writes glTF as (X, Z, Y) / 100 of the original's local space.
                    normals = new FVector[vertices / 2];
                    for (int v = 0; v < normals.Length; v++)
                    {
                        normals[v] = new FVector(raw[v * 3], raw[(v * 3) + 2], raw[(v * 3) + 1]);
                    }
                }

                FRotator rotation = default;
                if (i.TryGetProperty("rotation", out JsonElement r) && r.ValueKind == JsonValueKind.Object)
                {
                    rotation = new FRotator((int)Num(r, "Pitch"), (int)Num(r, "Yaw"), (int)Num(r, "Roll"));
                }

                bodies.Add(new SoftBodySource
                {
                    Actor = actor,
                    Block = block,
                    Location = Vec(i, "location", default),
                    Rotation = rotation,
                    Scale = Vec(i, "scale", new FVector(1f, 1f, 1f)),
                    PrePivot = Vec(i, "prePivot", default),
                    Indices = indices,
                    Uvs = uvs.Length == vertices * 2 ? uvs : new float[vertices * 2],
                    FrontNormals = normals,
                });
            }
            catch (Exception e) when (e is KeyNotFoundException or InvalidOperationException or IndexOutOfRangeException or ArgumentOutOfRangeException)
            {
                warnings.Add(actor + ": mesh " + meshName + " could not be read (" + e.Message + ")");
            }
        }

        return (bodies, warnings, doc);
    }

    private static int[] ReadIndices(JsonElement root, ReadOnlySpan<byte> bin, int accessor)
    {
        JsonElement a = root.GetProperty("accessors")[accessor];
        int count = a.GetProperty("count").GetInt32();
        int type = a.GetProperty("componentType").GetInt32();
        int size = type switch { 5125 => 4, 5123 => 2, 5121 => 1, _ => throw new InvalidOperationException("index type " + type) };
        (int offset, int stride) = View(root, a, size);
        var result = new int[count];
        for (int k = 0; k < count; k++)
        {
            ReadOnlySpan<byte> at = bin.Slice(offset + (k * stride), size);
            result[k] = size == 4 ? (int)BinaryPrimitives.ReadUInt32LittleEndian(at) : size == 2 ? BinaryPrimitives.ReadUInt16LittleEndian(at) : at[0];
        }

        return result;
    }

    private static float[] ReadFloats(JsonElement root, ReadOnlySpan<byte> bin, int accessor, int components)
    {
        JsonElement a = root.GetProperty("accessors")[accessor];
        if (a.GetProperty("componentType").GetInt32() != 5126)
        {
            throw new InvalidOperationException("accessor " + accessor + " is not float");
        }

        int count = a.GetProperty("count").GetInt32();
        (int offset, int stride) = View(root, a, components * 4);
        var result = new float[count * components];
        for (int k = 0; k < count; k++)
        {
            for (int c = 0; c < components; c++)
            {
                result[(k * components) + c] = BinaryPrimitives.ReadSingleLittleEndian(bin.Slice(offset + (k * stride) + (c * 4), 4));
            }
        }

        return result;
    }

    private static (int Offset, int Stride) View(JsonElement root, JsonElement accessor, int elementSize)
    {
        JsonElement view = root.GetProperty("bufferViews")[accessor.GetProperty("bufferView").GetInt32()];
        int offset = (view.TryGetProperty("byteOffset", out JsonElement vo) ? vo.GetInt32() : 0) +
            (accessor.TryGetProperty("byteOffset", out JsonElement ao) ? ao.GetInt32() : 0);
        int stride = view.TryGetProperty("byteStride", out JsonElement s) ? s.GetInt32() : elementSize;
        return (offset, stride);
    }

    private static string? Str(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    private static bool Flag(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.True;

    private static float Num(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? (float)v.GetDouble() : 0f;

    private static FVector Vec(JsonElement e, string name, FVector fallback)
    {
        if (!e.TryGetProperty(name, out JsonElement v) || v.ValueKind != JsonValueKind.Array || v.GetArrayLength() < 3)
        {
            return fallback;
        }

        return new FVector((float)v[0].GetDouble(), (float)v[1].GetDouble(), (float)v[2].GetDouble());
    }
}
