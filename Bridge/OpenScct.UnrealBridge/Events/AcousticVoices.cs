using System;
using System.Collections.Generic;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Events;

/// <summary>How sound through walls is rendered (the Audio page's "Sound through walls").</summary>
internal enum AcousticMode
{
    /// <summary>Godot's default (MaterialTransmission on): the wall's material cuts the level and the top end, and a
    /// blocked source is heard from the opening it reaches the listener through.</summary>
    Transmission = 0,

    /// <summary>The original's DirectSound path (rule 18): occlusion only shrinks the source's minimum distance.</summary>
    Original = 1,

    /// <summary>Walls change nothing.</summary>
    Off = 2,
}

/// <summary>
/// The acoustics of each 3D voice Unreal plays, kept per voice key between frames: a port of Godot's
/// SoundSystem.SourceTransmission (SoundSystem.Transmission.cs), PropagatePoint (SoundSystem.Acoustics.cs) and
/// OccludedMinDistance (SoundSystem.cs, rule 18). Every voice's line to the listener is traced at most every 80 to
/// 115 ms and eight voices a frame; the levels ease every frame (60 ms), the rule 18 minimum distance over 1 s.
/// </summary>
internal sealed class AcousticVoices
{
    public const int FlagNeverOccluded = 1;

    public const int ResultBlocked = 1;
    public const int ResultIndirect = 2;
    public const int ResultCorner = 4;
    public const int ResultTraced = 8;

    private const int TraceBudget = 8;
    private const int CornerBudget = 12;

    private readonly AcousticMap _map;
    private readonly Dictionary<int, State> _states = [];
    private readonly List<int> _gone = [];
    private Func<FVector, FVector, PortalTransmission>? _passage;
    private Func<FVector, FVector, bool>? _clear;
    private double _now;
    private long _pass;
    private bool _probed;

    public AcousticVoices(AcousticMap map)
    {
        _map = map;
    }

    public int Tracked => _states.Count;

    /// <summary>
    /// One frame for every voice listed: the scene follows the listener, each voice's state is updated and its result
    /// written at the same index. Voices of earlier frames that are not listed are forgotten.
    /// </summary>
    public void Update(FVector listener, float deltaSeconds, AcousticMode mode, ReadOnlySpan<BridgeAcousticSource> sources,
        Span<BridgeAcousticResult> results)
    {
        float dt = Math.Clamp(deltaSeconds, 0f, 0.5f);
        _now += dt;
        _pass++;
        int listenerZone = _map.ZoneIndexAt(listener);
        _passage ??= _map.TracePassage;
        _clear ??= _map.Clear;
        if (mode == AcousticMode.Transmission)
        {
            if (!_probed)
            {
                // Godot probes four openings a frame from the start; here the first frame probes them all, so a
                // source heard in the first second is not taken for one behind closed doors.
                _map.Scene.ProbeAll(listener, _passage);
                _probed = true;
            }

            _map.Scene.Update(listenerZone, listener, _now, _passage);
        }

        int traceBudget = TraceBudget, cornerBudget = CornerBudget;
        for (int i = 0; i < sources.Length; i++)
        {
            ref readonly BridgeAcousticSource source = ref sources[i];
            if (!_states.TryGetValue(source.Key, out State? state))
            {
                state = new State();
                _states.Add(source.Key, state);
            }

            state.Pass = _pass;
            results[i] = Voice(state, listener, source, dt, mode, ref traceBudget, ref cornerBudget);
        }

        _gone.Clear();
        foreach (KeyValuePair<int, State> pair in _states)
        {
            if (pair.Value.Pass != _pass)
            {
                _gone.Add(pair.Key);
            }
        }

        foreach (int key in _gone)
        {
            _states.Remove(key);
        }
    }

    /// <summary>Forgets every voice (a new match, or a check that starts over).</summary>
    public void Reset()
    {
        _states.Clear();
        _probed = false;
    }

    private BridgeAcousticResult Voice(State v, FVector listener, in BridgeAcousticSource source, float dt, AcousticMode mode,
        ref int traceBudget, ref int cornerBudget)
    {
        var location = new FVector(source.X, source.Y, source.Z);
        float minDistance = Math.Max(source.MinDistance, 0.001f);
        bool exempt = mode == AcousticMode.Off || (source.Flags & FlagNeverOccluded) != 0 ||
            _map.SoundOcclusionOf(source.ActorId) == AcousticRules.SoundOcclusionNone;
        int flags = 0;

        // SourceTransmission: the line from the listener, traced again when either end moved 16 uu or its time came.
        if (exempt)
        {
            v.WallBlocked = false;
            v.FactorTarget = 1f;
            v.Transmission = v.TransmissionTarget = TransmissionLevels.Clear;
            v.Initialized = false;
        }
        else
        {
            bool changed = FVector.DistSquared(v.TraceListener, listener) > 16f * 16f || FVector.DistSquared(v.TraceSource, location) > 16f * 16f;
            if (traceBudget > 0 && (!v.Initialized || changed || _now >= v.NextTrace))
            {
                traceBudget--;
                flags |= ResultTraced;
                v.TraceListener = listener;
                v.TraceSource = location;
                v.NextTrace = _now + 0.08 + ((source.Key & 7) * 0.005);
                v.WallBlocked = _map.OcclusionBetween(listener, location, out OcclusionHits hits);
                v.Hits = hits;
                v.FactorTarget = v.WallBlocked ? _map.OcclusionFactor(hits) ?? 1f : 1f;
                v.TransmissionTarget = v.WallBlocked && mode == AcousticMode.Transmission
                    ? AcousticRules.Through(FVector.Dist(hits.NearListener, hits.NearSource), hits.TypeNearListener, hits.TypeNearSource)
                    : TransmissionLevels.Clear;
                if (!v.Initialized)
                {
                    v.Transmission = v.TransmissionTarget;
                }

                v.Initialized = true;
            }

            v.Transmission = v.Transmission.Approach(v.TransmissionTarget, dt);
        }

        // Rule 18 (OccludedMinDistance): the factor shrinks the minimum distance, down to 0.1 uu, eased in proportion
        // to the time since it last settled and settled after a second.
        float minTarget = Math.Max(minDistance * v.FactorTarget, AcousticRules.OccludedMinDistance);
        double since = _now - v.SettledAt;
        if (!v.Settled || since >= AcousticRules.OcclusionSettleSeconds)
        {
            v.OccludedMinDistance = minTarget;
            v.SettledAt = _now;
            v.Settled = true;
        }
        else
        {
            v.OccludedMinDistance += (minTarget - v.OccludedMinDistance) * (float)since;
        }

        // PropagatePoint: a blocked source reached through openings (or round a corner of its own zone) is heard from
        // the first opening on the way, as far away as the way is long.
        TransmissionLevels wall = v.Transmission;
        float directDistance = FVector.Dist(location, listener);
        AcousticPath path = default;
        if (mode == AcousticMode.Transmission && !exempt)
        {
            path = _map.Scene.PathTo(_map.ZoneIndexAt(location));
            if ((!path.Valid || path.Hops == 0) && v.WallBlocked)
            {
                CornerPathSearch search = v.Corner;
                bool moved = !search.Initialized || FVector.DistSquared(search.Listener, listener) > 32f * 32f ||
                    FVector.DistSquared(search.Source, location) > 32f * 32f;
                if (moved || (search.Complete && _now >= v.NextCornerSearch))
                {
                    OcclusionHits hits = v.Hits;
                    search.Begin(listener, location, hits.NearListener, hits.NearSource, hits.NormalNearListener, hits.NormalNearSource);
                    v.NextCornerSearch = _now + 0.25;
                }

                cornerBudget -= search.Step(Math.Min(3, cornerBudget), _clear ??= _map.Clear);
                AcousticPath corner = search.PathFrom(listener);
                if (corner.Valid)
                {
                    path = corner;
                    flags |= ResultCorner;
                }
            }
        }

        bool indirect = path.Valid && path.Hops > 0 && wall.DirectHF < 0.999f;
        if (indirect)
        {
            v.LastPath = path;
            flags |= ResultIndirect;
        }

        float target = indirect ? 1f : 0f;
        v.PropagationMix += (target - v.PropagationMix) * (dt > 0f ? 1f - MathF.Exp(-dt / 0.06f) : 1f);
        path = v.LastPath;
        float blend = path.Valid ? v.PropagationMix : 0f;
        FVector opening = path.Valid ? path.FirstOpening : location;
        FVector apparent = location + ((opening - location) * blend);
        float routedDistance = path.Distance + FVector.Dist(path.LastOpening, location);
        float distance = directDistance + ((routedDistance - directDistance) * blend);
        float pathGain = path.Gain * 0.85f;
        float directGain = wall.DirectGain + ((pathGain - wall.DirectGain) * blend);
        float directHF = wall.DirectHF + ((path.HF - wall.DirectHF) * blend);

        // What Unreal's curve gives at the emitter's place, and what the voice should have instead.
        float plain = DistanceGain(FVector.Dist(apparent, listener), minDistance, source.Radius);
        float wanted = mode switch
        {
            AcousticMode.Transmission => DistanceGain(distance, minDistance, source.Radius) * directGain,
            AcousticMode.Original => DistanceGain(directDistance, v.OccludedMinDistance, source.Radius),
            _ => plain,
        };

        if (v.WallBlocked)
        {
            flags |= ResultBlocked;
        }

        return new BridgeAcousticResult
        {
            Key = source.Key,
            Flags = flags,
            Occlusion = 1f - Math.Clamp(v.FactorTarget, 0f, 1f),
            OccludedMinDistance = v.OccludedMinDistance,
            DirectGain = mode == AcousticMode.Transmission ? directGain : 1f,
            DirectHF = mode == AcousticMode.Transmission ? directHF : 1f,
            LowPassHz = mode == AcousticMode.Transmission ? AcousticRules.LowPassHz(directHF) : 20000f,
            VolumeScale = plain > 0f ? Math.Clamp(wanted / plain, 0f, 1f) : 1f,
            X = apparent.X,
            Y = apparent.Y,
            Z = apparent.Z,
            Distance = distance,
            PropagationMix = blend,
            Hops = path.Valid && blend > 0f ? path.Hops : 0,
            SourceZone = _map.ZoneIndexAt(location),
        };
    }

    /// <summary>The original's distance gain (ScctAudioSubsystem::DistanceGain): 1 inside the minimum, 0 past 100 x radius.</summary>
    public static float DistanceGain(float distance, float minDistance, float radius)
    {
        if (radius > 0f && distance >= 100f * radius)
        {
            return 0f;
        }

        return distance <= minDistance ? 1f : minDistance / (minDistance + (0.5f * (distance - minDistance)));
    }

    private sealed class State
    {
        public TransmissionLevels Transmission = TransmissionLevels.Clear, TransmissionTarget = TransmissionLevels.Clear;
        public bool Initialized;
        public double NextTrace;
        public FVector TraceListener, TraceSource;
        public bool WallBlocked;
        public OcclusionHits Hits;
        public float FactorTarget = 1f;
        public float OccludedMinDistance;
        public double SettledAt;
        public bool Settled;
        public float PropagationMix;
        public AcousticPath LastPath;
        public readonly CornerPathSearch Corner = new();
        public double NextCornerSearch;
        public long Pass;
    }
}
