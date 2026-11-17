using System;
using System.Collections.Generic;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Events;

/// <summary>How much an opening lets through, as probed against its current state (an open or a closed door).</summary>
internal readonly record struct PortalTransmission(float Gain, float HF)
{
    public static PortalTransmission Open => new(1f, 1f);
}

/// <summary>A real opening polygon between two BSP zones of the map (render/lighting.json "fogPortals").</summary>
internal sealed class AcousticPortal
{
    public AcousticPortal(int back, int front, FVector normal, FVector[] vertices)
    {
        Back = back;
        Front = front;
        Normal = normal.SafeNormal();
        Vertices = vertices;
        foreach (FVector point in vertices)
        {
            Center += point;
        }

        Center /= Math.Max(1, vertices.Length);
        for (int i = 1; i + 1 < vertices.Length; i++)
        {
            Area += ((vertices[i] - vertices[0]) ^ (vertices[i + 1] - vertices[0])).Size() * 0.5f;
        }
    }

    /// <summary>The opening's area in square units (a fan of the polygon), which weights it as a reverb aperture.</summary>
    public float Area { get; }

    public int Back { get; }

    public int Front { get; }

    public FVector Normal { get; }

    public FVector[] Vertices { get; }

    public FVector Center { get; }

    /// <summary>The point of the opening nearest <paramref name="point"/>: its projection when inside, else the nearest edge point.</summary>
    public FVector ClosestPoint(FVector point)
    {
        FVector projected = point - (Normal * ((point - Center) | Normal));
        bool inside = true;
        FVector closest = Center;
        float distance = float.PositiveInfinity;
        for (int i = 0; i < Vertices.Length; i++)
        {
            FVector a = Vertices[i], edge = Vertices[(i + 1) % Vertices.Length] - a;
            FVector inward = Normal ^ edge;
            if ((inward | (Center - a)) < 0f)
            {
                inward = -inward;
            }

            if ((inward | (projected - a)) < -0.01f)
            {
                inside = false;
            }

            float t = edge.SizeSquared() > 0f ? Math.Clamp(((projected - a) | edge) / edge.SizeSquared(), 0f, 1f) : 0f;
            FVector candidate = a + (edge * t);
            float d = FVector.DistSquared(candidate, point);
            if (d < distance)
            {
                closest = candidate;
                distance = d;
            }
        }

        return inside ? projected : closest;
    }
}

/// <summary>
/// A way sound takes from the listener to a zone: its length, what it lets through, its hops and the first and last
/// openings on it (the first is where the listener hears it from).
/// </summary>
internal readonly record struct AcousticPath(bool Valid, float Distance, float Gain, float HF, int Hops,
    FVector FirstOpening, FVector LastOpening);

/// <summary>
/// Room coupling through the map's actual opening polygons, centred on the listener: a port of Godot's AcousticScene
/// (game/Scripts/Audio/AcousticScene.cs). Each update probes four openings against the collision (doors and movers in
/// their current state) and, when the listener moved or a tenth of a second passed, finds the cheapest way from the
/// listener's zone to every other zone, at most four openings deep and 60 m long.
/// </summary>
internal sealed class AcousticScene
{
    private readonly AcousticPortal[] _portals;
    private readonly List<int>[] _links;
    private readonly PortalTransmission[] _transmission;
    private readonly AcousticPath[] _paths;
    private readonly float[] _cost;
    private readonly bool[] _visited;
    private int _nextPortal, _zone = -1;
    private double _nextPaths, _nextProbes;
    private FVector _listener;

    public AcousticScene(AcousticPortal[] portals, int zoneCount)
    {
        _portals = portals;
        int count = Math.Max(1, zoneCount);
        foreach (AcousticPortal portal in portals)
        {
            count = Math.Max(count, Math.Max(portal.Back, portal.Front) + 1);
        }

        count = Math.Clamp(count, 1, 256);
        _links = new List<int>[count];
        for (int i = 0; i < count; i++)
        {
            _links[i] = [];
        }

        _transmission = new PortalTransmission[portals.Length];
        for (int i = 0; i < portals.Length; i++)
        {
            AcousticPortal portal = portals[i];
            if (portal.Back < 0 || portal.Front < 0 || portal.Back >= count || portal.Front >= count)
            {
                continue;
            }

            _links[portal.Back].Add(i);
            _links[portal.Front].Add(i);

            // An opening lets nothing through before its first probe (notably when joining behind a closed door).
            _transmission[i] = new PortalTransmission(0f, 1f);
        }

        _paths = new AcousticPath[count];
        _cost = new float[count];
        _visited = new bool[count];
    }

    public int PortalCount => _portals.Length;

    public int ListenerZone => _zone;

    public long PortalQueries { get; private set; }

    public AcousticPath PathTo(int zone) => zone >= 0 && zone < _paths.Length ? _paths[zone] : default;

    /// <summary>
    /// The openings of a zone as seen from <paramref name="origin"/> for the reverb geometry probe (Godot's
    /// AcousticScene.ReverbApertures): each opening's centre, weighted by its solid angle; coincident polygons count
    /// once, and when there are more than the span holds the weakest are dropped.
    /// </summary>
    public int ReverbApertures(int zone, FVector origin, Span<FVector> points, Span<float> weights)
    {
        if (zone < 0 || zone >= _links.Length)
        {
            return 0;
        }

        int count = 0;
        foreach (int index in _links[zone])
        {
            AcousticPortal portal = _portals[index];
            FVector delta = portal.Center - origin;
            float w = portal.Area * Math.Abs(delta.SafeNormal() | portal.Normal) / (4f * MathF.PI * Math.Max(64f * 64f, delta.SizeSquared()));
            int duplicate = -1;
            for (int i = 0; i < count; i++)
            {
                if (FVector.DistSquared(points[i], portal.Center) < 8f * 8f)
                {
                    duplicate = i;
                    break;
                }
            }

            if (duplicate >= 0)
            {
                weights[duplicate] = Math.Max(weights[duplicate], w);
                continue;
            }

            if (count < points.Length)
            {
                points[count] = portal.Center;
                weights[count++] = w;
                continue;
            }

            int smallest = 0;
            for (int i = 1; i < count; i++)
            {
                if (weights[i] < weights[smallest])
                {
                    smallest = i;
                }
            }

            if (w > weights[smallest])
            {
                points[smallest] = portal.Center;
                weights[smallest] = w;
            }
        }

        return count;
    }

    /// <summary>Godot's AcousticScene.ReflectedGain: what a room's reverb field keeps on its way to the listener.</summary>
    public static float ReflectedGain(AcousticPath path) =>
        !path.Valid ? 0f : path.Hops == 0 ? 1f : path.Gain / (1f + (path.Distance / 650f));

    /// <summary>Probes every opening at once (a check, or the first update of a session).</summary>
    public void ProbeAll(FVector listener, Func<FVector, FVector, PortalTransmission> passage)
    {
        for (int index = 0; index < _portals.Length; index++)
        {
            Probe(index, listener, passage);
        }
    }

    public void Update(int listenerZone, FVector listener, double now, Func<FVector, FVector, PortalTransmission> passage)
    {
        if (listenerZone < 0 || listenerZone >= _links.Length)
        {
            return;
        }

        // Four opening probes per update, not per source; duplicate zone polygons are alternative routes, never
        // summed energy.
        for (int n = 0; now >= _nextProbes && n < Math.Min(4, _portals.Length); n++)
        {
            Probe(_nextPortal++ % _portals.Length, listener, passage);
        }

        if (now >= _nextProbes)
        {
            _nextProbes = now + (1.0 / 60);
        }

        if (_zone == listenerZone && now < _nextPaths && FVector.DistSquared(listener, _listener) < 32f * 32f)
        {
            return;
        }

        _zone = listenerZone;
        _listener = listener;
        _nextPaths = now + 0.1;
        Array.Clear(_paths);
        Array.Clear(_visited);
        Array.Fill(_cost, float.PositiveInfinity);
        _paths[listenerZone] = new(true, 0f, 1f, 1f, 0, listener, listener);
        _cost[listenerZone] = 0f;
        for (int iteration = 0; iteration < _links.Length; iteration++)
        {
            int selected = -1;
            float lowest = float.PositiveInfinity;
            for (int i = 0; i < _cost.Length; i++)
            {
                if (!_visited[i] && _cost[i] < lowest)
                {
                    lowest = _cost[i];
                    selected = i;
                }
            }

            if (selected < 0)
            {
                break;
            }

            _visited[selected] = true;
            AcousticPath current = _paths[selected];
            if (current.Hops >= 4)
            {
                continue;
            }

            foreach (int index in _links[selected])
            {
                AcousticPortal portal = _portals[index];
                int next = portal.Front == selected ? portal.Back : portal.Front;
                if (_visited[next])
                {
                    continue;
                }

                FVector opening = portal.ClosestPoint(current.LastOpening);
                float length = current.Distance + FVector.Dist(current.LastOpening, opening);
                float gain = current.Gain * _transmission[index].Gain * (current.Hops == 0 ? 1f : 0.7f);
                if (length > 6000f || gain < 0.001f)
                {
                    continue;
                }

                float cost = length - (MathF.Log(Math.Max(gain, 0.00001f)) * 600f);
                if (cost >= _cost[next])
                {
                    continue;
                }

                _cost[next] = cost;
                _paths[next] = new(true, length, gain, current.HF * _transmission[index].HF * 0.9f,
                    current.Hops + 1, current.Hops == 0 ? opening : current.FirstOpening, opening);
            }
        }
    }

    private void Probe(int index, FVector listener, Func<FVector, FVector, PortalTransmission> passage)
    {
        AcousticPortal portal = _portals[index];
        FVector point = (portal.ClosestPoint(listener) * 0.75f) + (portal.Center * 0.25f);
        _transmission[index] = passage(point + (portal.Normal * 16f), point - (portal.Normal * 16f));
        PortalQueries++;
    }
}

/// <summary>
/// A bounded edge search when a wall blocks two points of one zone (Godot's CornerPathSearch): candidates step out
/// sideways from the wall's two faces at 0.64 to 10 m, and every segment of one must be clear; no way is inferred
/// from zone membership. The work is spread over frames and the result kept per voice.
/// </summary>
internal sealed class CornerPathSearch
{
    private static readonly float[] Radii = [64f, 128f, 256f, 512f, 1024f];
    private FVector _near, _far, _nearNormal, _farNormal, _tangent, _first, _last;
    private int _candidate, _segment;
    private AcousticPath _path;

    public FVector Listener { get; private set; }

    public FVector Source { get; private set; }

    public bool Initialized { get; private set; }

    public bool Complete { get; private set; }

    public void Begin(FVector listener, FVector source, FVector near, FVector far, FVector nearNormal, FVector farNormal)
    {
        Listener = listener;
        Source = source;
        _near = near;
        _far = far;
        _nearNormal = nearNormal.SafeNormal();
        _farNormal = farNormal.SafeNormal();
        if (_nearNormal.SizeSquared() < 0.5f)
        {
            _nearNormal = (listener - source).SafeNormal();
        }

        if (_farNormal.SizeSquared() < 0.5f)
        {
            _farNormal = -_nearNormal;
        }

        _tangent = (_nearNormal ^ new FVector(0f, 0f, 1f)).SafeNormal();
        if (_tangent.SizeSquared() < 0.5f)
        {
            _tangent = (_nearNormal ^ new FVector(1f, 0f, 0f)).SafeNormal();
        }

        _candidate = _segment = 0;
        _path = default;
        Initialized = true;
        Complete = _tangent.SizeSquared() < 0.5f;
    }

    public int Step(int budget, Func<FVector, FVector, bool> clear)
    {
        int used = 0;
        while (!Complete && used < budget)
        {
            if (_segment == 0)
            {
                float radius = Radii[_candidate / 2] * ((_candidate & 1) == 0 ? 1f : -1f);
                _first = _near + (_nearNormal * 8f) + (_tangent * radius);
                _last = _far + (_farNormal * 8f) + (_tangent * radius);
            }

            FVector a = _segment == 0 ? Listener : _segment == 1 ? _first : _last;
            FVector b = _segment == 0 ? _first : _segment == 1 ? _last : Source;
            used++;
            if (!clear(a, b))
            {
                Next();
                continue;
            }

            if (++_segment < 3)
            {
                continue;
            }

            float distance = FVector.Dist(Listener, _first) + FVector.Dist(_first, _last);
            float total = distance + FVector.Dist(_last, Source);
            if (total <= 6000f && (!_path.Valid || total < _path.Distance + FVector.Dist(_path.LastOpening, Source)))
            {
                _path = new(true, distance, 0.85f, 0.8f, 1, _first, _last);
            }

            Next();
        }

        return used;
    }

    public AcousticPath PathFrom(FVector listener) => !_path.Valid ? default : _path with
    {
        Distance = FVector.Dist(listener, _path.FirstOpening) + FVector.Dist(_path.FirstOpening, _path.LastOpening),
    };

    private void Next()
    {
        _segment = 0;
        _candidate++;
        Complete = _candidate >= Radii.Length * 2 || ((_candidate & 1) == 0 && _path.Valid);
    }
}
