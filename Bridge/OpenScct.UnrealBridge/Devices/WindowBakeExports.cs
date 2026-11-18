using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
using OpenScct.Content;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Events;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public struct WindowBakeEdit
{
    public int Name, Mesh, Primitive, Element;
    public float R, G, B;
    public int HasPoint;
    public float X, Y, Z, NX, NY, NZ;
}

// A snapshot belongs to one play session. A subsequent break/reset supersedes an in-flight calculation.
internal sealed class WindowBake
{
    private readonly WindowLightTransfer? _transfer;
    private readonly List<string> _panes = [];
    private readonly WorldState? _world;
    private readonly Dictionary<string, int> _names = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<(string Actor, int Section), (int Mesh, int Primitive)> _meshes = [];
    private readonly Dictionary<string, List<int>> _pageMaterials = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> _broken = new(StringComparer.OrdinalIgnoreCase);
    private Task<WindowBakeEdit[]>? _pending;
    private int _revision, _working, _applied;
    public WindowBakeEdit[] Edits = [];

    public WindowBake() { }

    public WindowBake(BridgeSession bridge)
    {
        var session = bridge.Session;
        string folder = session.MapFolder;
        string path = Path.Combine(folder, WindowLightTransfer.RelativePath);
        if (!File.Exists(path)) return;
        using var file = File.OpenRead(path);
        var transfer = WindowLightTransfer.Load(file);
        if (session.Client.Map?.Loaded is not { } loaded) return;
        var ids = transfer.Panes.Select(p => p.Actor).ToHashSet(StringComparer.OrdinalIgnoreCase);
        _world = WorldState.Of(bridge);
        foreach (var record in loaded.Gameplay.Actors)
            if (ids.Contains(record.Id) && loaded.ActorOf(record) is SVitre) _panes.Add(record.Id);
        foreach (var batch in transfer.Batches)
            foreach (var resource in batch.Resources) _names.TryAdd(resource.Name, EventNames.Id(resource.Name));
        ReadGeometry(Path.Combine(folder, "render"));
        _transfer = transfer;
    }

    private void ReadGeometry(string folder)
    {
        using var bin = new BinaryReader(File.OpenRead(Path.Combine(folder, "world.glb")));
        if (bin.ReadUInt32() != 0x46546c67 || bin.ReadUInt32() != 2) throw new InvalidDataException("Invalid map glTF");
        bin.ReadUInt32();
        int size = bin.ReadInt32();
        if (bin.ReadUInt32() != 0x4e4f534a || size < 0 || size > bin.BaseStream.Length - 20) throw new InvalidDataException("Invalid glTF JSON chunk");
        using var doc = JsonDocument.Parse(bin.ReadBytes(size));
        var root = doc.RootElement;
        var nodes = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        foreach (var n in root.GetProperty("nodes").EnumerateArray())
            if (n.TryGetProperty("mesh", out var m) && n.TryGetProperty("name", out var name)) nodes[name.GetString()!] = m.GetInt32();
        var meshes = root.GetProperty("meshes");
        if (root.TryGetProperty("extras", out var extras) && extras.TryGetProperty("svmap", out var svmap) && svmap.TryGetProperty("instances", out var instances))
            foreach (var instance in instances.EnumerateArray())
            {
                string actor = instance.GetProperty("actor").GetString()!;
                if (!_names.ContainsKey(actor) || !nodes.TryGetValue(instance.GetProperty("mesh").GetString()!, out int mesh)) continue;
                int index = 0;
                foreach (var p in meshes[mesh].GetProperty("primitives").EnumerateArray())
                {
                    int section = p.TryGetProperty("extras", out var ext) && ext.TryGetProperty("scctSection", out var s) ? s.GetInt32() : index;
                    _meshes[(actor, section)] = (mesh, index++);
                }
            }
        using var materials = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(folder, "materials.json")));
        if (!materials.RootElement.TryGetProperty("lightmapped", out var pages) || !root.TryGetProperty("materials", out var materialsArray)) return;
        var byMaterial = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var p in pages.EnumerateArray()) byMaterial[p.GetProperty("name").GetString()!] = p.GetProperty("lightmap").GetString()!;
        int materialIndex = 0;
        foreach (var m in materialsArray.EnumerateArray())
        {
            if (m.TryGetProperty("name", out var name) && byMaterial.TryGetValue(name.GetString()!, out var page))
            {
                if (!_pageMaterials.TryGetValue(page, out var list)) _pageMaterials[page] = list = [];
                list.Add(materialIndex);
            }
            materialIndex++;
        }
    }

    // -1 means no new result, zero explicitly restores the intact materials and meshes.
    public int Poll()
    {
        if (_transfer is null) return -1;
        bool changed = false;
        foreach (string id in _panes)
        {
            if (_world?.PlacementActor(id) is not SVitre { bDeleteMe: false } pane) continue;
            bool broken = pane.bAlreadyDestroyed || pane.DrawType == EDrawType.DT_None || pane.StaticMesh.IsNone;
            changed |= broken ? _broken.Add(id) : _broken.Remove(id);
        }
        if (changed) _revision++;
        if (_pending is { IsCompleted: true } finished)
        {
            _pending = null;
            if (_working == _revision)
            {
                _applied = _revision;
                Edits = finished.GetAwaiter().GetResult();
                return Edits.Length;
            }
        }
        if (_applied == _revision) return -1;
        if (_broken.Count == 0)
        {
            _applied = _revision;
            Edits = [];
            return 0;
        }
        if (_pending is null)
        {
            _working = _revision;
            var state = new HashSet<string>(_broken, StringComparer.OrdinalIgnoreCase);
            _pending = Task.Run(() => Calculate(_transfer, state));
        }
        return -1;
    }

    private WindowBakeEdit[] Calculate(WindowLightTransfer transfer, HashSet<string> state)
    {
        var values = new Dictionary<(string Kind, string Name, int Section, int Element), Vector3>();
        var points = new Dictionary<(string Kind, string Name, int Section, int Element), WindowLightTransfer.Destination>();
        foreach (var batch in transfer.Batches)
        {
            float[] result = transfer.EvaluateFlat(batch, state);
            for (int i = 0; i < batch.Samples.Count; i++)
            {
                var sample = batch.Samples[i];
                foreach (var d in sample.Destinations)
                {
                    float Delta(int c) => WindowLightTransfer.Encode(result[i*3+c], d.Dither) - WindowLightTransfer.Encode(sample.Initial[c], d.Dither);
                    var delta = new Vector3(Delta(0), Delta(1), Delta(2)) * d.Weight;
                    if (delta == Vector3.Zero) continue;
                    var r = batch.Resources[d.Resource];
                    var key = (r.Kind, r.Name, r.Surface, d.Element);
                    values[key] = values.GetValueOrDefault(key) + delta;
                    if (d.Point is not null && d.Normal is not null) points[key] = d;
                }
            }
        }
        var rows = new List<WindowBakeEdit>(values.Count);
        foreach (var ((kind, name, section, element), delta) in values)
        {
            var row = new WindowBakeEdit { Name = _names[name], Mesh = -1, Primitive = -1, Element = element, R = delta.X, G = delta.Y, B = delta.Z };
            if (kind == "mesh")
            {
                if (!_meshes.TryGetValue((name, section), out var target)) continue;
                (row.Mesh, row.Primitive) = target;
                if (points.TryGetValue((kind, name, section, element), out var dest))
                {
                    row.HasPoint = 1;
                    (row.X, row.Y, row.Z) = (dest.Point![0], dest.Point[1], dest.Point[2]);
                    (row.NX, row.NY, row.NZ) = (dest.Normal![0], dest.Normal[1], dest.Normal[2]);
                }
            }
            rows.Add(row);
        }
        // Keep the source section of every primitive, including intact sibling sections of a replaced mesh.
        var changedActors = rows.Where(row => row.Mesh >= 0).Select(row => row.Name).ToHashSet();
        foreach (var ((actor, section), target) in _meshes)
            if (changedActors.Contains(_names[actor]))
                rows.Add(new() { Name = _names[actor], Mesh = target.Mesh, Primitive = target.Primitive, Element = -1, HasPoint = section });
        // Resource-to-material bindings for transient runtime textures, whose UObject names are generated.
        foreach (var (page, materials) in _pageMaterials)
            if (_names.TryGetValue(page, out int id))
                foreach (int m in materials) rows.Add(new() { Name = id, Mesh = -2, Element = m });
        return rows.ToArray();
    }
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WindowBakeApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int> Poll;
    public delegate* unmanaged[Cdecl]<WindowBakeEdit*, int, int> Copy;
}

public static unsafe class WindowBakeExports
{
    [UnmanagedCallersOnly]
    public static int GetApi(WindowBakeApi* api)
    {
        api->Version = 1;
        api->Poll = &Poll;
        api->Copy = &Copy;
        return 1;
    }

    private static WindowBake? Current => Exports.Current is { IsDisposed: false } s && s.Session.Client.Map?.Loaded is not null
        ? s.Feature(static session =>
        {
            try { return new WindowBake(session); }
            catch (Exception e) { Exports.ReportFailure("Window bake: " + e.Message); return new WindowBake(); }
        }) : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Poll()
    {
        try { return Current?.Poll() ?? -1; }
        catch (Exception e) { Exports.ReportFailure("Window bake update: " + e.Message); return -1; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Copy(WindowBakeEdit* rows, int count)
    {
        if (Current?.Edits is not { } edits || count < edits.Length || rows == null) return 0;
        edits.AsSpan().CopyTo(new Span<WindowBakeEdit>(rows, count));
        return edits.Length;
    }
}
