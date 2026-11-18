using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using OpenScct.Net;

namespace OpenScct.UnrealBridge.Devices;

// Scene geometry from the client's own simulation. Row kinds are mirrored in ScctWorldGeometryApi.h.
internal sealed class WorldGeometry(BridgeSession session)
{
    public List<GeometryRow> Rows { get; } = [];
    public List<GeometryVertex> Vertices { get; } = [];
    private readonly List<(Conveyor Belt, int Owner, int Material, float Tile, int Axis)> _scrolls = [];
    private readonly List<(Conveyor Belt, int Owner)> _steps = [];
    private readonly List<ConveyorStepFace> _faces = [];
    private readonly Dictionary<Conveyor, int[]> _meshes = [];
    private readonly List<ConveyorItemPlace> _items = [];
    private readonly List<WorldConsole> _consoles = [];
    private ConveyorItemFlow? _flow;
    private bool _bound;

    private void Bind()
    {
        if (session.Session.Client.Map?.Loaded is not { } map) return;
        var world = WorldState.Of(session);
        string render = Path.Combine(session.Session.MapFolder, "render");
        var belts = new List<Conveyor>();
        var lengths = new Dictionary<Conveyor, float>();
        Dictionary<string, float>? sizes = null;
        foreach (var record in map.Gameplay.Actors)
        {
            switch (map.ActorOf(record))
            {
                case SComputerObjectiveTrigger { bPS2Objective: true } computer when !computer.Mesh.IsNone:
                    try
                    {
                        if (WorldConsole.Load(world, render, record.Id, computer) is { } console) _consoles.Add(console);
                    }
                    catch (Exception e) when (e is IOException or InvalidDataException or JsonException or ArgumentException)
                    {
                        Exports.ReportFailure($"Console {record.Id}: {e.Message}");
                    }
                    break;
                case Conveyor belt:
                    belts.Add(belt);
                    _scrolls.Add((belt, world.Intern(record.Id + ConveyorLayout.BeltPart), 0, ConveyorLayout.BeltTile, 1));
                    _scrolls.Add((belt, world.Intern(record.Id + ConveyorLayout.RollersPart), 0, 2f * MathF.PI * ConveyorLayout.RollerRadius, 0));
                    foreach (var target in belt.ScrollTargets)
                        if (!string.IsNullOrEmpty(target.Target) && target.UnitsPerTile != 0)
                            _scrolls.Add((belt, world.Intern(target.Target), world.Intern(target.Material.Path), target.UnitsPerTile, target.Axis));
                    if (belt.Look == EConveyorLook.CL_Escalator)
                        _steps.Add((belt, world.Intern(record.Id + ConveyorLayout.StepsPart)));
                    var ids = new int[belt.ItemMeshes.Count];
                    float length = 0;
                    int first = 0;
                    if (ids.Length > 0) sizes ??= ItemSizes(Path.Combine(render, "world.glb"));
                    for (int i = 0; i < ids.Length; i++)
                    {
                        if (belt.ItemMeshes[i].Path is not { } path || !sizes!.TryGetValue(path, out float extent)) continue;
                        ids[i] = world.Intern(path);
                        if (first == 0) first = ids[i];
                        length = Math.Max(length, extent);
                    }
                    for (int i = 0; i < ids.Length; i++) if (ids[i] == 0) ids[i] = first;
                    _meshes[belt] = ids;
                    lengths[belt] = length > 0 ? length : 32f;
                    break;
            }
        }
        if (belts.Count > 0) _flow = new ConveyorItemFlow(belts, c => lengths[c] * c.ItemScale);
        _bound = true;
    }

    // Godot derives item spacing from the widest flat dimension of the render mesh, in UE units.
    private static Dictionary<string, float> ItemSizes(string file)
    {
        var result = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase);
        if (!File.Exists(file)) return result;
        using var doc = MapRenderIndex.ReadGlbJson(file);
        var root = doc.RootElement;
        var accessors = root.GetProperty("accessors");
        var meshes = root.GetProperty("meshes");
        foreach (var node in root.GetProperty("nodes").EnumerateArray())
        {
            if (!node.TryGetProperty("mesh", out var index) || !node.TryGetProperty("name", out var name)) continue;
            float x0 = float.PositiveInfinity, z0 = x0, x1 = float.NegativeInfinity, z1 = x1;
            foreach (var part in meshes[index.GetInt32()].GetProperty("primitives").EnumerateArray())
            {
                var position = accessors[part.GetProperty("attributes").GetProperty("POSITION").GetInt32()];
                if (!position.TryGetProperty("min", out var min) || !position.TryGetProperty("max", out var max)) continue;
                x0 = Math.Min(x0, min[0].GetSingle()); z0 = Math.Min(z0, min[2].GetSingle());
                x1 = Math.Max(x1, max[0].GetSingle()); z1 = Math.Max(z1, max[2].GetSingle());
            }
            string key = name.GetString()!;
            result.TryAdd(key, Math.Max(0, Math.Max(x1 - x0, z1 - z0)) * 100f);
            result.TryAdd(key.Split('#')[0], result[key]);
        }
        return result;
    }

    public int Prepare()
    {
        Rows.Clear();
        Vertices.Clear();
        if (!_bound) Bind();
        if (!_bound) return 0;
        double tick = session.Session.Client.ViewTimeFixed / (double)NetTimeline.Fraction;
        foreach (var s in _scrolls)
        {
            double tiles = s.Belt.PhaseAt(tick) / s.Tile;
            Rows.Add(new GeometryRow { Kind = 1, Owner = s.Owner, Material = s.Material, Part = s.Axis, Phase = -(float)(tiles - Math.Floor(tiles)) });
        }
        foreach (var s in _steps)
        {
            int first = Vertices.Count;
            double phase = s.Belt.PhaseAt(tick);
            ConveyorSteps.Build(s.Belt.WorldPath, s.Belt.HalfWidth, phase, _faces);
            float u = s.Belt.Width / ConveyorLayout.BeltTile;
            foreach (var f in _faces)
            {
                GeometryVertex V(FVector p, float x, float y) => new(p.X, p.Y, p.Z, f.Normal.X, f.Normal.Y, f.Normal.Z, x, y);
                var a = V(f.A, 0, f.V0); var b = V(f.B, u, f.V0);
                var c = V(f.C, u, f.V1); var d = V(f.D, 0, f.V1);
                Vertices.Add(a); Vertices.Add(b); Vertices.Add(c);
                Vertices.Add(a); Vertices.Add(c); Vertices.Add(d);
            }
            Rows.Add(new GeometryRow { Kind = 2, Owner = s.Owner, First = first, Count = Vertices.Count - first, Phase = (float)phase, Scale = new(1, 1, 1) });
        }
        if (_flow != null)
        {
            _flow.AdvanceTo((int)Math.Ceiling(tick));
            _flow.Place(tick, _items);
            foreach (var p in _items)
            {
                var meshes = _meshes[p.Origin];
                if (p.Scale > 0 && p.Mesh >= 0 && p.Mesh < meshes.Length && meshes[p.Mesh] != 0)
                    Rows.Add(new GeometryRow { Kind = 3, Asset = meshes[p.Mesh], Location = p.Location, Scale = new(p.Scale, p.Scale, p.Scale), Rotation = p.Belt, Spin = p.Spin });
            }
        }
        foreach (var c in _consoles) c.Draw(Rows, Vertices, (session.Session.Client.World.TickCount + (double)session.Alpha) / 60.0);
        return Rows.Count;
    }
}
