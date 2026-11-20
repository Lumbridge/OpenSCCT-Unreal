using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Game.Equipment;
using OpenScct.Physics.Collision;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ShadowTraceApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, BridgeFxHit*, int> Trace;
}

/// <summary>World visibility and the nearest receiving surface for projected character shadows.</summary>
public static unsafe class ShadowTraceExports
{
    public const int ApiVersion = 1;

    [UnmanagedCallersOnly]
    public static int GetApi(ShadowTraceApi* api)
    {
        api->Version = ApiVersion;
        api->Trace = &Trace;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Trace(float sx, float sy, float sz, float ex, float ey, float ez, BridgeFxHit* hit)
    {
        if (hit == null)
        {
            return -1;
        }

        *hit = default;
        try
        {
            if (Exports.Current is not { IsDisposed: false } session ||
                GadgetWorld.Collision(session.Session.Client.World.Level.Info) is not { } world)
            {
                return -1;
            }

            return TraceScene(world, new FVector(sx, sy, sz), new FVector(ex, ey, ez), out *hit);
        }
        catch (Exception error)
        {
            Exports.ReportFailure(error.ToString());
            return -1;
        }
    }

    /// <summary>Uses rendered mesh faces and the current position of doors. Characters do not block this ray.</summary>
    public static int TraceScene(ICollisionWorld world, FVector start, FVector end, out BridgeFxHit hit)
    {
        // The receiver needs the closest surface, so let the query test all nearer candidates.
        const ETraceFlags flags = ETraceFlags.TRACE_World | ETraceFlags.TRACE_VisibleSurface;
        if (world.LineCheck(out FCheckResult result, null, end, start, FVector.Zero, flags) && result.bHit)
        {
            hit = BridgeFxHit.From(result);
            return 1;
        }

        hit = default;
        return 0;
    }
}
