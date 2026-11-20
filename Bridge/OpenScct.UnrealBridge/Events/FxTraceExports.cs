using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Physics.Collision;

namespace OpenScct.UnrealBridge;

/// <summary>Where an effect's line met the client's collision (Source/OpenSCCT/Runtime/ScctFxTraceApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeFxHit
{
    /// <summary>The point hit and the surface's normal there.</summary>
    public float X, Y, Z, NormalX, NormalY, NormalZ;

    /// <summary>Simulation id of the actor hit, 0 for the level's own geometry.</summary>
    public int ActorId;

    /// <summary>How far along the line the hit is, 0 at its start and 1 at its end.</summary>
    public float Time;

    internal static BridgeFxHit From(in FCheckResult result)
    {
        FVector normal = result.Normal.SafeNormal();
        return new BridgeFxHit
        {
            X = result.Location.X, Y = result.Location.Y, Z = result.Location.Z,
            NormalX = normal.X, NormalY = normal.Y, NormalZ = normal.Z,
            ActorId = result.Actor is { } actor && actor is not LevelInfo ? actor.ActorId : 0,
            Time = result.Time,
        };
    }
}

/// <summary>
/// The particle collision trace for Unreal's effects: the imported level has no Unreal collision (the simulation owns
/// it), so a casing or a spark that bounces asks the client world's collision, as the Godot client's
/// EffectViews.TraceOf does for every colliding emitter (level and world geometry, trace flags 0x84). It reads the
/// client world and never writes it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FxTraceApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, BridgeFxHit*, int> Trace;
}

public static unsafe class FxTraceExports
{
    public const int ApiVersion = 1;

    /// <summary>ParticleK.TraceFlags of the Godot client: the level and world geometry, not pawns or projectors.</summary>
    private const ETraceFlags TraceFlags = (ETraceFlags)0x84;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(FxTraceApi* api)
    {
        api->Version = ApiVersion;
        api->Trace = &Trace;
        return ApiVersion;
    }

    /// <summary>
    /// The first surface on the line from start to end in the client world, into <paramref name="hit"/>. Returns 1
    /// when the line meets one, 0 when it is clear or there is no session.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Trace(float sx, float sy, float sz, float ex, float ey, float ez, BridgeFxHit* hit)
    {
        try
        {
            if (hit == null)
            {
                return 0;
            }

            *hit = default;
            if (Exports.Current is not { IsDisposed: false } session ||
                GadgetWorld.Collision(session.Session.Client.World.Level.Info) is not { } world)
            {
                return 0;
            }

            var start = new FVector(sx, sy, sz);
            var end = new FVector(ex, ey, ez);
            if (!world.LineCheck(out FCheckResult r, null, end, start, FVector.Zero, TraceFlags) || !r.bHit)
            {
                return 0;
            }

            *hit = BridgeFxHit.From(r);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
