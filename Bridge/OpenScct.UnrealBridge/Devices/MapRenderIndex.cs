using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Devices;

/// <summary>One placed mesh of render/world.glb (an entry of its <c>extras.svmap.instances</c>).</summary>
public sealed class RenderInstance
{
    /// <summary>The simulation actor's map name: the Unreal level actor is tagged "Scct:" + this.</summary>
    public required string Actor { get; init; }

    /// <summary>The mesh node placed (the asset path with the converter's variant suffix).</summary>
    public required string Mesh { get; init; }

    public string Class { get; init; } = "";

    /// <summary>"mover" or "staticmesh".</summary>
    public string Kind { get; init; } = "";

    /// <summary>The converter marked the actor as one the simulation may move, hide or swap.</summary>
    public bool Gameplay { get; init; }

    /// <summary>Hidden in the map (bHidden): the importer places nothing for it.</summary>
    public bool Hidden { get; init; }

    /// <summary>Part of the sky box: the importer places nothing for it.</summary>
    public bool SkyZone { get; init; }

    public FVector Location { get; init; }
    public FRotator Rotation { get; init; }
    public FVector Scale { get; init; } = new(1f, 1f, 1f);
    public FVector PrePivot { get; init; }

    /// <summary>True when Tools/Import/import_map.py placed a level actor for it.</summary>
    public bool InLevel => !Hidden && !SkyZone;
}

/// <summary>One light of render/lighting.json.</summary>
public sealed class RenderLight
{
    /// <summary>The light actor's map name: the Unreal light is tagged "Scct:" + this.</summary>
    public required string Name { get; init; }

    /// <summary>Hidden lights are not placed by the importer.</summary>
    public bool Hidden { get; init; }

    /// <summary>An STriggerLight.</summary>
    public bool Triggered { get; init; }

    public bool InitiallyOn { get; init; } = true;

    /// <summary>The placed ELightType (1 steady, 2 pulse, 3 blink, 4 flicker, 5 strobe, 7 subtle pulse; 0 none).</summary>
    public int LightType { get; init; } = 1;

    public float Period { get; init; } = 32f;
    public float Phase { get; init; }
    public float Brightness { get; init; }
    public int Hue { get; init; }
    public int Saturation { get; init; }
    public float R { get; init; } = 1f;
    public float G { get; init; } = 1f;
    public float B { get; init; } = 1f;

    /// <summary>Where the map placed the light (the importer spawned the level light here).</summary>
    public FVector Location { get; init; }

    /// <summary>The placed rotation, in the original's 65536-per-turn units.</summary>
    public FRotator Rotation { get; init; }
}

/// <summary>
/// What a map's render part places, read from the same two files Tools/Import/import_map.py builds the Unreal level
/// from: the mesh instances of <c>render/world.glb</c> (its JSON chunk only; the geometry is not read) and the lights
/// of <c>render/lighting.json</c>. The bridge names level actors by these, so a name it hands to C++ is one the
/// importer tagged.
/// </summary>
public sealed class MapRenderIndex
{
    private const uint GlbMagic = 0x46546C67; // "glTF"
    private const uint JsonChunk = 0x4E4F534A; // "JSON"

    private readonly Dictionary<string, RenderInstance> _instances = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, RenderLight> _lightByName = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<RenderLight> _lights = [];

    /// <summary>The instances by actor name. An actor placed more than once keeps its first instance.</summary>
    public IReadOnlyDictionary<string, RenderInstance> Instances => _instances;

    /// <summary>The lights in the file's order.</summary>
    public IReadOnlyList<RenderLight> Lights => _lights;

    public RenderLight? FindLight(string name) => _lightByName.GetValueOrDefault(name);

    /// <summary>What was wrong with the render files (a file that could not be read, an entry that was skipped); empty when all was read.</summary>
    public IReadOnlyList<string> Warnings => _warnings;

    private readonly List<string> _warnings = [];

    /// <summary>
    /// Reads the render part of the map in <paramref name="mapFolder"/>. A missing or broken file leaves its part
    /// empty and one bad entry is skipped (see <see cref="Warnings"/>): the match still runs, with the world state of
    /// that part missing, instead of ending because of a render file the simulation does not need.
    /// </summary>
    public static MapRenderIndex Load(string mapFolder)
    {
        var index = new MapRenderIndex();
        string glb = Path.Combine(mapFolder, "render", "world.glb");
        if (File.Exists(glb))
        {
            try
            {
                index.ReadInstances(glb);
            }
            catch (Exception e) when (e is IOException or InvalidDataException or JsonException or InvalidOperationException or KeyNotFoundException)
            {
                index._warnings.Add("world.glb: " + e.Message);
            }
        }
        else
        {
            index._warnings.Add("world.glb is missing");
        }

        string lighting = Path.Combine(mapFolder, "render", "lighting.json");
        if (File.Exists(lighting))
        {
            try
            {
                index.ReadLights(lighting);
            }
            catch (Exception e) when (e is IOException or JsonException or InvalidOperationException or KeyNotFoundException)
            {
                index._warnings.Add("lighting.json: " + e.Message);
            }
        }
        else
        {
            index._warnings.Add("lighting.json is missing");
        }

        return index;
    }

    private void ReadInstances(string path)
    {
        using FileStream file = File.OpenRead(path);
        Span<byte> header = stackalloc byte[20];
        file.ReadExactly(header);
        if (BinaryPrimitives.ReadUInt32LittleEndian(header) != GlbMagic ||
            BinaryPrimitives.ReadUInt32LittleEndian(header[16..]) != JsonChunk)
        {
            throw new InvalidDataException(path + " is not a glb file with a leading JSON chunk");
        }

        int length = BinaryPrimitives.ReadInt32LittleEndian(header[12..]);
        if (length <= 0 || length > file.Length - header.Length)
        {
            throw new InvalidDataException(path + " has a JSON chunk longer than the file");
        }

        byte[] json = new byte[length];
        file.ReadExactly(json);
        using JsonDocument doc = JsonDocument.Parse(json);
        if (!doc.RootElement.TryGetProperty("extras", out JsonElement extras) ||
            !extras.TryGetProperty("svmap", out JsonElement svmap) ||
            !svmap.TryGetProperty("instances", out JsonElement instances))
        {
            return;
        }

        if (instances.ValueKind != JsonValueKind.Array)
        {
            return;
        }

        foreach (JsonElement i in instances.EnumerateArray())
        {
            if (i.ValueKind != JsonValueKind.Object || Str(i, "actor") is not { Length: > 0 } actor || _instances.ContainsKey(actor))
            {
                continue;
            }

            FRotator rotation = default;
            if (i.TryGetProperty("rotation", out JsonElement r) && r.ValueKind == JsonValueKind.Object)
            {
                rotation = new FRotator((int)Num(r, "Pitch"), (int)Num(r, "Yaw"), (int)Num(r, "Roll"));
            }

            _instances[actor] = new RenderInstance
            {
                Actor = actor,
                Mesh = Str(i, "mesh") ?? "",
                Class = Str(i, "class") ?? "",
                Kind = Str(i, "kind") ?? "",
                Gameplay = Flag(i, "gameplay"),
                Hidden = Flag(i, "hidden"),
                SkyZone = Flag(i, "skyZone"),
                Location = Vec(i, "location", default),
                Rotation = rotation,
                Scale = Vec(i, "scale", new FVector(1f, 1f, 1f)),
                PrePivot = Vec(i, "prePivot", default),
            };
        }
    }

    private void ReadLights(string path)
    {
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path));
        if (!doc.RootElement.TryGetProperty("lights", out JsonElement lights))
        {
            return;
        }

        if (lights.ValueKind != JsonValueKind.Array)
        {
            return;
        }

        foreach (JsonElement l in lights.EnumerateArray())
        {
            if (l.ValueKind != JsonValueKind.Object || Str(l, "name") is not { Length: > 0 } name || _lightByName.ContainsKey(name))
            {
                continue;
            }

            // "rotation" is [pitch, yaw, roll] in rotator units.
            FVector turn = Vec(l, "rotation", default);

            FVector colour = Vec(l, "color", new FVector(1f, 1f, 1f));
            var light = new RenderLight
            {
                Name = name,
                Hidden = Flag(l, "hidden"),
                Triggered = Flag(l, "triggered"),
                InitiallyOn = !l.TryGetProperty("initially_on", out JsonElement on) || on.ValueKind != JsonValueKind.False,
                LightType = (int)Num(l, "ltype", 1f),
                Period = Num(l, "light_period", 32f),
                Phase = Num(l, "light_phase"),
                Brightness = Num(l, "brightness"),
                Hue = (int)Num(l, "hue"),
                Saturation = (int)Num(l, "saturation"),
                R = colour.X,
                G = colour.Y,
                B = colour.Z,
                Location = Vec(l, "location", default),
                Rotation = new FRotator((int)turn.X, (int)turn.Y, (int)turn.Z),
            };
            _lights.Add(light);
            _lightByName[name] = light;
        }
    }

    private static string? Str(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    private static bool Flag(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.True;

    private static float Num(JsonElement e, string name, float fallback = 0f) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? (float)v.GetDouble() : fallback;

    private static FVector Vec(JsonElement e, string name, FVector fallback)
    {
        if (!e.TryGetProperty(name, out JsonElement v) || v.ValueKind != JsonValueKind.Array || v.GetArrayLength() < 3)
        {
            return fallback;
        }

        for (int k = 0; k < 3; k++)
        {
            if (v[k].ValueKind != JsonValueKind.Number)
            {
                return fallback;
            }
        }

        return new FVector((float)v[0].GetDouble(), (float)v[1].GetDouble(), (float)v[2].GetDouble());
    }
}
