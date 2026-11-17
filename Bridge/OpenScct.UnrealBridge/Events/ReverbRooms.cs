using System;
using OpenScct.Core;
using OpenScct.Physics.Collision;

namespace OpenScct.UnrealBridge.Events;

/// <summary>The reverb rooms of one session: zone lookups over the acoustic map and the geometry probes' slots.</summary>
internal sealed class ReverbRooms(AcousticMap map)
{
    /// <summary>The listener's probe and one per source-room field Unreal keeps, with room to spare.</summary>
    public const int Slots = 8;

    private readonly ReverbRoomProbe[] _probes = CreateProbes();
    private Func<FVector, FVector, PortalTransmission>? _passage;
    private double _now;

    private static ReverbRoomProbe[] CreateProbes()
    {
        var probes = new ReverbRoomProbe[Slots];
        for (int i = 0; i < Slots; i++)
        {
            probes[i] = new ReverbRoomProbe();
        }

        return probes;
    }

    public void Locate(FVector listener, float deltaSeconds, bool updateScene, ReadOnlySpan<float> points, Span<BridgeReverbRoom> rooms)
    {
        _now += Math.Clamp(deltaSeconds, 0f, 0.5f);
        ICollisionWorld? collision = map.Collision;
        if (updateScene && collision is not null)
        {
            _passage ??= map.TracePassage;
            int zone = map.ZoneIndexAt(listener);
            if (map.Scene.ListenerZone < 0)
            {
                map.Scene.ProbeAll(listener, _passage);
            }

            map.Scene.Update(zone, listener, _now, _passage);
        }

        for (int i = 0; i < rooms.Length; i++)
        {
            var point = new FVector(points[i * 3], points[(i * 3) + 1], points[(i * 3) + 2]);
            ref BridgeReverbRoom room = ref rooms[i];
            room = default;
            if (collision is null)
            {
                continue;
            }

            room.ZoneIndex = collision.GetZoneIndex(point);
            if (collision.GetZoneInfo(point) is { } info)
            {
                room.Flags |= 1;
                room.ZoneX = info.Location.X;
                room.ZoneY = info.Location.Y;
                room.ZoneZ = info.Location.Z;
            }

            AcousticPath path = map.Scene.PathTo(room.ZoneIndex);
            if (path.Valid)
            {
                room.Flags |= 2;
                room.PathDistance = path.Distance;
                room.PathGain = path.Gain;
                room.PathHF = path.HF;
                room.Hops = path.Hops;
                room.OpeningX = path.LastOpening.X;
                room.OpeningY = path.LastOpening.Y;
                room.OpeningZ = path.LastOpening.Z;
                room.ReflectedGain = AcousticScene.ReflectedGain(path);
            }
        }
    }

    public int Probe(int slot, FVector origin, int zoneIndex, int maxRays, out BridgeReverbGeometry geometry)
    {
        geometry = default;
        if (slot < 0 || slot >= Slots)
        {
            return 0;
        }

        ReverbRoomProbe probe = _probes[slot];
        int traced = 0;
        if (map.Collision is { } collision)
        {
            Span<FVector> apertures = stackalloc FVector[6];
            Span<float> weights = stackalloc float[6];
            for (; traced < Math.Clamp(maxRays, 0, 64); traced++)
            {
                if (probe.NeedsSweep)
                {
                    int count = map.Scene.ReverbApertures(zoneIndex, origin, apertures, weights);
                    probe.Begin(origin, apertures[..count], weights[..count]);
                }

                probe.Add(ReverbRoomProbe.Trace(collision, probe.Origin, probe.NextDirection));
                map.Queries++;
            }
        }

        ReverbRoomGeometry g = probe.Geometry;
        geometry = new BridgeReverbGeometry
        {
            Valid = g.Valid ? 1 : 0,
            Size = g.Size,
            Volume = g.Volume,
            Area = g.Area,
            Openness = g.Openness,
            Decay = g.Decay,
            HFDecayRatio = g.HFDecayRatio,
            LFDecayRatio = g.LFDecayRatio,
            EarlyDelay = g.EarlyDelay,
            LateDelay = g.LateDelay,
            Diffusion = g.Diffusion,
            WetGain = g.WetGain,
            Rays = (int)Math.Min(probe.Rays, int.MaxValue),
            Sweeps = (int)Math.Min(probe.Sweeps, int.MaxValue),
        };
        return traced;
    }

    public void Reset(int slot)
    {
        for (int i = 0; i < Slots; i++)
        {
            if (slot < 0 || slot == i)
            {
                _probes[i].Reset();
            }
        }
    }
}

internal readonly record struct ReverbRoomHit(bool Hit, float Distance, FVector Normal, int Surface = 0, int Occlusion = 0);

internal readonly record struct ReverbRoomGeometry(bool Valid, float Size, float Volume, float Area, float Openness,
    float Decay, float HFDecayRatio, float LFDecayRatio, float EarlyDelay, float LateDelay, float Diffusion, float WetGain);

/// <summary>
/// Bounded, incremental sampling of the acoustic volume around a point, a port of Godot's ReverbGeometryProbe
/// (game/Scripts/Audio/ReverbGeometryProbe.cs): 26 fixed directions plus up to six towards the zone's openings,
/// traced against the level, its geometry and movers; each complete sweep gives a room size, openness and
/// Sabine-style decay times from the surfaces' broad absorption classes. Distances enter in world units.
/// </summary>
internal sealed class ReverbRoomProbe
{
    public const float Range = 8192f;
    private const int BaseRays = 26, MaxRays = 32;

    /// <summary>Metres per world unit (Godot's AudioK.DistanceFactor).</summary>
    private const float DistanceFactor = 0.0127f;

    /// <summary>Movers, the level and its geometry with the hit material, nearest hit (Godot's TraceReverbGeometry).</summary>
    private const ETraceFlags Flags = ETraceFlags.TRACE_Material | ETraceFlags.TRACE_LevelGeometry | ETraceFlags.TRACE_Level |
        ETraceFlags.TRACE_Movers;

    private static readonly FVector[] Basis = MakeBasis();
    private readonly FVector[] _directions = new FVector[MaxRays];
    private readonly float[] _weights = new float[MaxRays];
    private readonly ReverbRoomHit[] _hits = new ReverbRoomHit[MaxRays];
    private int _count, _next;

    public FVector Origin { get; private set; }

    public ReverbRoomGeometry Geometry { get; private set; }

    public long Rays { get; private set; }

    public long Sweeps { get; private set; }

    public bool NeedsSweep => _count == 0 || _next == _count;

    public FVector NextDirection => _directions[_next];

    public void Reset()
    {
        _count = _next = 0;
        Geometry = default;
        Rays = Sweeps = 0;
    }

    public static ReverbRoomHit Trace(ICollisionWorld collision, FVector origin, FVector direction)
    {
        // A nearest-hit query, so live movers are compared with the BSP behind them.
        if (!collision.LineCheck(out FCheckResult hit, null, origin + (direction * Range), origin, default, Flags) ||
            !hit.bHit || hit.bFakeBackdrop)
        {
            return new(false, Range, default);
        }

        return new(true, FVector.Dist(origin, hit.Location), hit.Normal, hit.SurfaceType,
            (collision as CollisionWorld)?.OcclusionTypeOf(hit.Material) ?? 0);
    }

    public void Begin(FVector origin, ReadOnlySpan<FVector> apertures, ReadOnlySpan<float> weights)
    {
        Origin = origin;
        _next = 0;
        _count = BaseRays;
        Basis.CopyTo(_directions, 0);
        float total = 0f;
        for (int i = 0; i < Math.Min(apertures.Length, MaxRays - BaseRays); i++)
        {
            FVector direction = (apertures[i] - origin).SafeNormal();
            float weight = Math.Clamp(weights[i], 0f, 0.15f);
            if (direction.SizeSquared() < 0.5f || weight <= 0f)
            {
                continue;
            }

            _directions[_count] = direction;
            _weights[_count++] = weight;
            total += weight;
        }

        float scale = total > 0.35f ? 0.35f / total : 1f;
        for (int i = BaseRays; i < _count; i++)
        {
            _weights[i] *= scale;
        }

        Array.Fill(_weights, (1f - Math.Min(total, 0.35f)) / BaseRays, 0, BaseRays);
    }

    public void Add(ReverbRoomHit hit)
    {
        _hits[_next++] = hit;
        Rays++;
        if (_next != _count)
        {
            return;
        }

        Sweeps++;
        ReverbRoomGeometry measured = Estimate(_hits.AsSpan(0, _count), _directions.AsSpan(0, _count), _weights.AsSpan(0, _count));
        if (measured.Valid)
        {
            Geometry = measured;
        }
    }

    private static ReverbRoomGeometry Estimate(ReadOnlySpan<ReverbRoomHit> hits, ReadOnlySpan<FVector> directions, ReadOnlySpan<float> weights)
    {
        double volume = 0, area = 0, absMid = 0, absHF = 0, absLF = 0, open = 0, total = 0;
        float nearest = float.PositiveInfinity, invalid = 0f;
        for (int i = 0; i < hits.Length; i++)
        {
            ReverbRoomHit hit = hits[i];
            float w = weights[i];
            total += w;
            if (!hit.Hit)
            {
                open += w;
                continue;
            }

            if (!float.IsFinite(hit.Distance) || hit.Distance < 1f)
            {
                invalid += w;
                continue;
            }

            double r = Math.Clamp(hit.Distance, 1f, Range) * DistanceFactor;
            double cosine = Math.Max(0.15, Math.Abs(directions[i] | hit.Normal.SafeNormal()));
            double surface = w * r * r / cosine;
            volume += w * r * r * r / 3;
            area += surface;
            (float mid, float hf, float lf) = Absorption(hit.Surface, hit.Occlusion);
            absMid += surface * mid;
            absHF += surface * hf;
            absLF += surface * lf;
            nearest = Math.Min(nearest, (float)r);
        }

        if (total <= 0 || invalid > total * 0.25)
        {
            return default; // The anchor is in solid space: the previous safe response stays.
        }

        float openness = (float)Math.Clamp(open / total, 0, 1);
        if (area < 0.001)
        {
            return new(true, 1f, 0f, 0f, 1f, 0.12f, 0.5f, 1f, 0.001f, 0.001f, 0.2f, 0.001f);
        }

        float meanFreePath = (float)(4 * volume / area);
        float RT(double absorption)
        {
            double survival = Math.Clamp((1 - (absorption / area)) * (1 - openness), 0.001, 0.999);
            return Math.Clamp((float)(13.81551 * meanFreePath / (343 * -Math.Log(survival))), 0.12f, 8f);
        }

        float decay = RT(absMid), hfDecay = RT(absHF), lfDecay = RT(absLF);

        // Opposed walls tell a bounded room from a ground plane in the open.
        int pairs = 0;
        for (int axis = 0; axis < 3 && hits.Length >= 6; axis++)
        {
            if (hits[axis * 2].Hit && hits[(axis * 2) + 1].Hit)
            {
                pairs++;
            }
        }

        float support = 0.15f + (0.85f * pairs / 3f);
        float wet = Math.Clamp(MathF.Pow(1f - openness, 1.7f) * support, 0.001f, 1f);
        float v = (float)(4 * Math.PI * volume), s = (float)(4 * Math.PI * area);
        return new(true, Math.Clamp(MathF.Cbrt(v), 1f, 100f), v, s, openness, decay,
            Math.Clamp(hfDecay / decay, 0.1f, 2f), Math.Clamp(lfDecay / decay, 0.1f, 2f),
            Math.Clamp(2f * nearest / 343f, 0.001f, 0.25f), Math.Clamp(meanFreePath / 343f, 0.001f, 0.1f),
            Math.Clamp(0.25f + (0.65f * (1f - openness) * pairs / 3f), 0.1f, 1f), wet);
    }

    /// <summary>Broad energy absorption (mid, high, low) of a surface type and occlusion class: estimates, not measured spectra.</summary>
    private static (float Mid, float HF, float LF) Absorption(int surface, int occlusion) =>
        occlusion == 10 || surface is 1 or 13 ? (0.45f, 0.7f, 0.18f) :
        surface is 4 or 20 or 21 ? (0.35f, 0.6f, 0.18f) :
        occlusion == 7 || surface is 7 or 8 ? (0.18f, 0.28f, 0.12f) :
        occlusion is 3 or 4 || surface == 19 ? (0.08f, 0.1f, 0.18f) :
        surface is 14 or 16 or 17 ? (0.08f, 0.12f, 0.05f) : (0.12f, 0.25f, 0.08f);

    private static FVector[] MakeBasis()
    {
        var result = new FVector[BaseRays];
        result[0] = new(1, 0, 0);
        result[1] = new(-1, 0, 0);
        result[2] = new(0, 1, 0);
        result[3] = new(0, -1, 0);
        result[4] = new(0, 0, 1);
        result[5] = new(0, 0, -1);
        int at = 6;
        for (int x = -1; x <= 1; x++)
        {
            for (int y = -1; y <= 1; y++)
            {
                for (int z = -1; z <= 1; z++)
                {
                    if (Math.Abs(x) + Math.Abs(y) + Math.Abs(z) > 1)
                    {
                        result[at++] = new FVector(x, y, z).SafeNormal();
                    }
                }
            }
        }

        return result;
    }
}
