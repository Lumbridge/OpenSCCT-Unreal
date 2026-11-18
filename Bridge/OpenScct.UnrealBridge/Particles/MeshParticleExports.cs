using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScctGame.Render.Particles;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public struct BridgeMeshParticleLayer
{
    public int OwnerId, MeshId, Capacity, DrawStyle, TextureId, Unlit;
    public float AlphaRef;
}

[StructLayout(LayoutKind.Sequential)]
public struct BridgeMeshParticle
{
    public int Layer;
    public float X, Y, Z, Pitch, Yaw, Roll, ScaleX, ScaleY, ScaleZ;
    public byte R, G, B, A;

    internal BridgeMeshParticle(int layer, MeshInstance particle)
    {
        const float degrees = 360f / 65536f;
        Layer = layer;
        (X, Y, Z) = (particle.Location.X, particle.Location.Y, particle.Location.Z);
        (Pitch, Yaw, Roll) = (particle.Rotation.Pitch * degrees, particle.Rotation.Yaw * degrees, particle.Rotation.Roll * degrees);
        (ScaleX, ScaleY, ScaleZ) = (particle.Scale.X, particle.Scale.Y, particle.Scale.Z);
        (R, G, B, A) = (particle.Color.R, particle.Color.G, particle.Color.B, particle.Color.A);
    }
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct MeshParticleApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<BridgeMeshParticleLayer*, int, int> Layers;
    public delegate* unmanaged[Cdecl]<int, void> Trigger;
    public delegate* unmanaged[Cdecl]<int, float, float, float, float, float, float, void> Place;
    public delegate* unmanaged[Cdecl]<BridgeMeshParticle*, int, int> Frame;
}

public static unsafe class MeshParticleExports
{
    [UnmanagedCallersOnly]
    public static int GetApi(MeshParticleApi* api)
    {
        api->Version = 1;
        api->Layers = &Layers;
        api->Trigger = &Trigger;
        api->Place = &Place;
        api->Frame = &Frame;
        return api->Version;
    }

    private static PlacedMeshParticles? Current => Exports.Current is { IsDisposed: false } s ? PlacedMeshParticles.Of(s) : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Layers(BridgeMeshParticleLayer* layers, int capacity)
    {
        try { return Current?.Layers(new Span<BridgeMeshParticleLayer>(layers, capacity)) ?? 0; }
        catch (Exception e) { Console.Error.WriteLine($"MeshEmitter definitions: {e}"); return -1; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Trigger(int owner)
    {
        try { Current?.Trigger(owner); }
        catch (Exception e) { Console.Error.WriteLine($"MeshEmitter trigger: {e}"); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Place(int owner, float x, float y, float z, float pitch, float yaw, float roll)
    {
        try
        {
            const float units = 65536f / 360f;
            Current?.Place(owner, new FVector(x, y, z), new FRotator((int)MathF.Round(pitch * units),
                (int)MathF.Round(yaw * units), (int)MathF.Round(roll * units)));
        }
        catch (Exception e) { Console.Error.WriteLine($"MeshEmitter placement: {e}"); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Frame(BridgeMeshParticle* particles, int capacity)
    {
        try { return Current?.Frame(new Span<BridgeMeshParticle>(particles, capacity)) ?? 0; }
        catch (Exception e) { Console.Error.WriteLine($"MeshEmitter frame: {e}"); return -1; }
    }
}
