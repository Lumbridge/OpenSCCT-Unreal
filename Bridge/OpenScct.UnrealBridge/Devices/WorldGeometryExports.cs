using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public struct GeometryRow
{
    public int Kind, Owner, Asset, Material, Picture, Part, First, Count;
    public FVector Location, Scale;
    public FRotator Rotation;
    public float Phase;
    public int Spin;
}

[StructLayout(LayoutKind.Sequential)]
public record struct GeometryVertex(float X, float Y, float Z, float Nx, float Ny, float Nz, float U, float V);

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WorldGeometryApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int> Prepare;
    public delegate* unmanaged[Cdecl]<GeometryRow*, int, int> Rows;
    public delegate* unmanaged[Cdecl]<int, GeometryVertex*, int, int> Vertices;
}

public static unsafe class WorldGeometryExports
{
    [UnmanagedCallersOnly]
    public static int GetApi(WorldGeometryApi* api)
    {
        api->Version = 1;
        api->Prepare = &Prepare;
        api->Rows = &Rows;
        api->Vertices = &Vertices;
        return 1;
    }

    private static Devices.WorldGeometry? Current => Exports.Current is { IsDisposed: false } s
        ? s.Feature(x => new Devices.WorldGeometry(x)) : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Prepare()
    {
        try { return Current?.Prepare() ?? 0; }
        catch (Exception e) { Exports.ReportFailure(e.ToString()); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Rows(GeometryRow* data, int capacity)
    {
        try
        {
            if (data == null || capacity <= 0 || Current is not { } g) return 0;
            int n = Math.Min(capacity, g.Rows.Count);
            CollectionsMarshal.AsSpan(g.Rows)[..n].CopyTo(new Span<GeometryRow>(data, n));
            return n;
        }
        catch (Exception e) { Exports.ReportFailure(e.ToString()); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Vertices(int first, GeometryVertex* data, int count)
    {
        try
        {
            if (data == null || count <= 0 || Current is not { } g || first < 0 || first > g.Vertices.Count - count) return 0;
            CollectionsMarshal.AsSpan(g.Vertices).Slice(first, count).CopyTo(new Span<GeometryVertex>(data, count));
            return count;
        }
        catch (Exception e) { Exports.ReportFailure(e.ToString()); return 0; }
    }
}
