using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Combat;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;
using EquipmentK = OpenScct.Game.Equipment.K;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public struct MountedLaserRay
{
    public float HolderX, HolderY, HolderZ;
    public float OriginX, OriginY, OriginZ;
    public float DirX, DirY, DirZ;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct LaserTraceApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, MountedLaserRay*, float*, int> Trace;
}

/// <summary>Finds the first surface along the visible game model's laser. This query only affects rendering.</summary>
public static unsafe class LaserTraceExports
{
    public const int ApiVersion = 1;
    private const ETraceFlags Surfaces = ETraceFlags.TRACE_World | ETraceFlags.TRACE_Others | ETraceFlags.TRACE_VisibleSurface;
    [ThreadStatic] private static List<FCheckResult>? s_surfaceHits;
    [ThreadStatic] private static List<PillShape>? s_shapes;

    [UnmanagedCallersOnly]
    public static int GetApi(LaserTraceApi* api)
    {
        api->Version = ApiVersion;
        api->Trace = &Trace;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Trace(int holderId, MountedLaserRay* ray, float* distance)
    {
        try
        {
            if (ray == null || distance == null || Exports.Current is not { IsDisposed: false } session ||
                session.Session.Client.World.Level.FindActor(holderId) is not SPAWNDEFENSE { bDeleteMe: false } holder)
            {
                return -1;
            }

            return TraceScene(holder, *ray, out *distance);
        }
        catch (Exception error)
        {
            Exports.ReportFailure(error.ToString());
            return -1;
        }
    }

    internal static int TraceScene(SPAWNDEFENSE holder, MountedLaserRay ray, out float distance)
    {
        distance = 0;
        var anchor = new FVector(ray.HolderX, ray.HolderY, ray.HolderZ);
        var origin = new FVector(ray.OriginX, ray.OriginY, ray.OriginZ);
        var direction = new FVector(ray.DirX, ray.DirY, ray.DirZ).SafeNormal();
        if (holder.XLevel is not { } level || GadgetWorld.Collision(holder) is not { } world || direction.IsZero())
        {
            return -1;
        }

        // Camera/weapon picking must also stop on drawn props that allow players to walk through them.
        Actor? source = holder.MyWeapon;
        if (VisibleHit(world, source, anchor, origin, out _))
        {
            return -1;
        }

        var end = origin + direction * EquipmentK.TraceLength;
        bool hit = VisibleHit(world, source, origin, end, out FCheckResult surface);
        if (hit)
        {
            end = surface.Location;
        }

        // Intersect the current character pose directly. A bullet query uses simplified cover and
        // could discard a visible character standing beside an oversized collision hull.
        var pose = level.GetService<IPillPose>() ?? EstimatedPillPose.Instance;
        var shapes = s_shapes ??= [];
        foreach (var pawn in GadgetWorld.Pawns(level))
        {
            if (pawn == holder || pawn.bHidden || pawn.bDeleteMe ||
                PillLineCheck.DistToSegment(origin, end, pawn.Location) >= TraceHitZones.PawnPassScale * pawn.CollisionHeight)
            {
                continue;
            }

            shapes.Clear();
            pose.GetPills(pawn, shapes);
            if (PillLineCheck.Check(shapes, origin, end, out _, out var point, out _, out _))
            {
                hit = true;
                end = point;
            }
        }
        distance = Math.Clamp((end - origin) | direction, 0f, EquipmentK.TraceLength);
        return hit ? 1 : 0;
    }

    private static bool VisibleHit(ICollisionWorld world, Actor? source, FVector start, FVector end, out FCheckResult hit)
    {
        if (!world.LineCheck(out hit, source, end, start, FVector.Zero, Surfaces) || !hit.bHit)
        {
            return false;
        }
        if (hit.Collider >= 0)
        {
            return true;
        }
        // An actor's interaction cylinder has no drawable face. Continue to the next mesh or brush.
        var hits = s_surfaceHits ??= [];
        world.MultiLineCheck(hits, source, end, start, FVector.Zero, Surfaces);
        foreach (var candidate in hits)
        {
            if (candidate.Collider >= 0 && candidate.bHit)
            {
                hit = candidate;
                return true;
            }
        }
        return false;
    }
}
