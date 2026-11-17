using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Physics.Collision;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// The map's acoustics as the bridge reads them: the zone openings of render/lighting.json ("fogPortals", the world
/// model's zone portals, as Godot's MapAcoustics.FromLighting reads them), the portal distance table of
/// audio/audio.json ("zoneDist", ULevel's ZoneDist) and the client world's collision and zones. It answers the
/// occlusion questions of rule 18 and owns the <see cref="AcousticScene"/>.
/// </summary>
internal sealed class AcousticMap
{
    private readonly PlaySession _session;
    private readonly byte[] _zoneDist;
    private readonly int _zoneCount;

    private AcousticMap(PlaySession session, AcousticPortal[] portals, byte[] zoneDist, int zoneCount)
    {
        _session = session;
        _zoneDist = zoneDist;
        _zoneCount = zoneCount;
        Scene = new AcousticScene(portals, zoneCount);
    }

    public AcousticScene Scene { get; }

    public int ZoneCount => _zoneCount;

    /// <summary>Line checks made for sound (occlusion, openings and corner searches), for the log.</summary>
    public long Queries { get; set; }

    /// <summary>What went wrong reading the map's files (empty when both were read).</summary>
    public List<string> Warnings { get; } = [];

    public static AcousticMap Load(PlaySession session)
    {
        var warnings = new List<string>();
        var portals = new List<AcousticPortal>();
        byte[] zoneDist = [];
        int zoneCount = 0;
        string? folder = session.MapFolder;
        if (folder is not null)
        {
            string lighting = Path.Combine(folder, "render", "lighting.json");
            try
            {
                ReadPortals(lighting, portals);
                if (portals.Count == 0)
                {
                    warnings.Add($"{lighting} has no fogPortals: sound takes no way through openings");
                }
            }
            catch (Exception e) when (e is IOException or JsonException or InvalidOperationException or KeyNotFoundException)
            {
                warnings.Add($"{lighting}: {e.Message}");
            }

            string audio = Path.Combine(folder, "audio", "audio.json");
            try
            {
                (zoneDist, zoneCount) = ReadZoneDist(audio);
                if (zoneCount == 0)
                {
                    warnings.Add($"{audio} has no zoneDist: no zone term in the occlusion");
                }
            }
            catch (Exception e) when (e is IOException or JsonException or InvalidOperationException or KeyNotFoundException)
            {
                warnings.Add($"{audio}: {e.Message}");
            }
        }

        var map = new AcousticMap(session, portals.ToArray(), zoneDist, zoneCount);
        map.Warnings.AddRange(warnings);
        return map;
    }

    /// <summary>The client world's collision, or null before the level has one.</summary>
    public ICollisionWorld? Collision => GadgetWorld.Collision(_session.Client.World.Level.Info);

    /// <summary>The zone number of a point (0: the default zone, or no collision).</summary>
    public int ZoneIndexAt(FVector location) => Collision?.GetZoneIndex(location) ?? 0;

    /// <summary>The portal distance between two zone numbers; 0 when the map has no table (no zone term then).</summary>
    public int ZoneDistance(int a, int b) =>
        a >= 0 && b >= 0 && a < _zoneCount && b < _zoneCount ? _zoneDist[(a * _zoneCount) + b] : 0;

    /// <summary>The actor's SoundOcclusion (0, OCCLUSION_Default, for actor 0 or one the world does not have).</summary>
    public byte SoundOcclusionOf(int actorId) =>
        actorId != 0 && _session.Client.World.Level.FindActor(actorId) is { } actor ? (byte)actor.SoundOcclusion : (byte)0;

    /// <summary>GetOcclusion's two line checks. False when the line from the listener is clear.</summary>
    public bool OcclusionBetween(FVector listener, FVector source, out OcclusionHits hits)
    {
        hits = default;
        Queries++;
        return Collision is { } collision && AcousticRules.TraceOcclusion(collision, listener, source, out hits);
    }

    /// <summary>Rule 18's factor for a blocked line (1 unoccluded), or null when the original leaves it unoccluded.</summary>
    public float? OcclusionFactor(in OcclusionHits hits) =>
        AcousticRules.Occlusion(FVector.Dist(hits.NearListener, hits.NearSource), hits.TypeNearListener, hits.TypeNearSource,
            hits.ZoneNearListener, hits.ZoneNearSource, ZoneDistance(hits.ZoneNearListener, hits.ZoneNearSource));

    /// <summary>What an opening lets through now (Godot's SoundSystem.TracePassage): open, or a closed door's material.</summary>
    public PortalTransmission TracePassage(FVector a, FVector b)
    {
        if (!OcclusionBetween(a, b, out OcclusionHits hit) ||
            (hit.TypeNearListener == AcousticRules.OcclusionTypeNone && hit.TypeNearSource == AcousticRules.OcclusionTypeNone))
        {
            return PortalTransmission.Open;
        }

        TransmissionLevels material = AcousticRules.Through(FVector.Dist(hit.NearListener, hit.NearSource),
            hit.TypeNearListener, hit.TypeNearSource);
        return new(material.DirectGain * 0.2f, material.DirectHF * 0.5f);
    }

    /// <summary>True when nothing blocks the line (a corner search's segment).</summary>
    public bool Clear(FVector from, FVector to) => !OcclusionBetween(from, to, out _);

    private static void ReadPortals(string path, List<AcousticPortal> into)
    {
        if (!File.Exists(path))
        {
            throw new FileNotFoundException("missing");
        }

        using JsonDocument json = JsonDocument.Parse(File.ReadAllBytes(path));
        if (!json.RootElement.TryGetProperty("fogPortals", out JsonElement portals) || portals.ValueKind != JsonValueKind.Array)
        {
            return;
        }

        foreach (JsonElement portal in portals.EnumerateArray())
        {
            JsonElement plane = portal.GetProperty("plane");
            var vertices = new List<FVector>();
            foreach (JsonElement v in portal.GetProperty("vertices").EnumerateArray())
            {
                vertices.Add(new FVector(v[0].GetSingle(), v[1].GetSingle(), v[2].GetSingle()));
            }

            if (vertices.Count >= 3)
            {
                into.Add(new AcousticPortal(portal.GetProperty("back").GetInt32(), portal.GetProperty("front").GetInt32(),
                    new FVector(plane[0].GetSingle(), plane[1].GetSingle(), plane[2].GetSingle()), vertices.ToArray()));
            }
        }
    }

    /// <summary>audio.json's "zoneDist": a square array of portal distances (as AudioLibrary's MapAudioDoc reads it).</summary>
    private static (byte[] Table, int Count) ReadZoneDist(string path)
    {
        if (!File.Exists(path))
        {
            throw new FileNotFoundException("missing");
        }

        using JsonDocument json = JsonDocument.Parse(File.ReadAllBytes(path));
        if (!json.RootElement.TryGetProperty("zoneDist", out JsonElement dist) || dist.ValueKind != JsonValueKind.Array)
        {
            return ([], 0);
        }

        int n = dist.GetArrayLength();
        var table = new byte[n * n];
        int i = 0;
        foreach (JsonElement row in dist.EnumerateArray())
        {
            int j = 0;
            foreach (JsonElement v in row.EnumerateArray())
            {
                if (j < n)
                {
                    table[(i * n) + j] = (byte)Math.Clamp(v.GetInt32(), 0, 255);
                }

                j++;
            }

            i++;
        }

        return (table, n);
    }
}
