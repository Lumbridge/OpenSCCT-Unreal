using System;
using OpenScct.Content.Collision;
using OpenScct.Core;
using OpenScct.Physics.Collision;
using OpenScct.UnrealBridge;

internal static unsafe class ShadowTraceCheck
{
    public static int Run()
    {
        ShadowTraceApi shadow = default;
        delegate* unmanaged<ShadowTraceApi*, int> bind = &ShadowTraceExports.GetApi;
        BridgeFxHit hit = default;
        if (bind(&shadow) != 1 || shadow.Version != 1 || shadow.Trace(0, 0, 0, 100, 0, 0, &hit) != -1)
        {
            throw new InvalidOperationException("The shadow trace must distinguish unavailable collision from a clear ray.");
        }

        // The geometry behind the doorway must not receive a shadow through a closed door.
        var geometry = new CollisionDataBuilder();
        geometry.AddBox(new FVector(180, -64, -64), new FVector(190, 64, 64));
        int door = geometry.AddBoxHullCollider(ColliderKind.Mover, null, CollisionDataBuilder.WorldFlags,
            new FVector(-4, -36, -36), new FVector(4, 36, 36), new FVector(80, 0, 0));
        var world = CollisionWorld.Build(geometry.Build());
        var start = FVector.Zero;
        var end = new FVector(240, 0, 0);
        if (ShadowTraceExports.TraceScene(world, start, end, out hit) != 1 || hit.X is < 74 or > 77 ||
            hit.NormalX > -0.99f || hit.Time is <= 0 or >= 1)
        {
            throw new InvalidOperationException($"Closed door was not the first shadow receiver (x={hit.X}, nx={hit.NormalX}).");
        }

        world.SetColliderTransform(door, new FVector(80, 120, 0), default);
        if (ShadowTraceExports.TraceScene(world, start, end, out hit) != 1 || hit.X is < 178 or > 181 ||
            ShadowTraceExports.TraceScene(world, start, new FVector(150, 0, 0), out hit) != 0)
        {
            throw new InvalidOperationException("The open doorway must expose the far receiver and clear the shorter ray.");
        }

        Console.WriteLine("ShadowTrace v1 passed: unavailable collision, nearest closed door, surface normal, open door, clear ray.");
        return 0;
    }
}
