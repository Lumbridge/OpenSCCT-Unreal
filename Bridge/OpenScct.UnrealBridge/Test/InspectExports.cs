using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Physics.Collision;
using OpenScct.UnrealBridge.Test;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The play inspector's reads (Source/OpenSCCT/Test/ScctPlayInspector.h, the Godot client's PlayInspector, InspectPose
/// and InspectNet): each client pawn's animation channel 0 and clip-less channels, the local pawn's snaps, the sound
/// counts, and traces against the client world's collision (the floor under a pawn, a wall ahead, a camera inside
/// the level's geometry). Unreal asks for <see cref="InspectExports.GetApi"/> once (type
/// "OpenScct.UnrealBridge.InspectExports, OpenScct.UnrealBridge", method "GetApi") and keeps the table.
/// Source/OpenSCCT/Test/ScctInspectApi.h mirrors it field for field. No entry throws: a failure returns 0 (or -1)
/// with the reason in LastError.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct InspectApi
{
    public int Version;

    /// <summary>Writes the session's counters (<see cref="BridgeInspectCounters"/>). Returns its id, 0 when none runs.</summary>
    public delegate* unmanaged[Cdecl]<BridgeInspectCounters*, int> GetCounters;

    /// <summary>Writes every living client pawn (<see cref="BridgeInspectPawn"/>). Returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<BridgeInspectPawn*, int, int> GetPawns;

    /// <summary>Copies the text of a name id GetPawns gave. Returns its length.</summary>
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetName;

    /// <summary>
    /// Sweeps a box of half-size <c>extent</c> (3 floats; zeros for a line) from <c>start</c> to <c>end</c> (3 floats
    /// each) through the client world's level geometry (BSP, movers, static meshes; no pawns). Returns 1 with the hit
    /// location and the normal's Z in <c>hit</c> (4 floats), 0 for no hit, -1 when no world can be traced.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float*, float*, float*, float*, int> Trace;

    /// <summary>1 when a box of half-size <c>extent</c> at <c>at</c> (3 floats) is inside level geometry, 0 when clear, -1 with no world.</summary>
    public delegate* unmanaged[Cdecl]<float*, float, int> PointInSolid;

    /// <summary>Copies the last failure's text. Returns its length.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

public static unsafe class InspectExports
{
    public const int ApiVersion = 1;

    /// <summary>World geometry for traces (PlayInspector.WorldTrace).</summary>
    private const ETraceFlags WorldTrace = ETraceFlags.TRACE_Level | ETraceFlags.TRACE_Movers | ETraceFlags.TRACE_LevelGeometry;

    private static string s_lastError = "";
    private static bool s_tracking;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(InspectApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        // From now on every session keeps the inspector's per-tick figures from its first tick.
        if (!s_tracking)
        {
            s_tracking = true;
            Exports.SessionStarted += session => InspectTrack.Of(session);
        }

        api->Version = ApiVersion;
        api->GetCounters = &GetCounters;
        api->GetPawns = &GetPawns;
        api->GetName = &GetName;
        api->Trace = &Trace;
        api->PointInSolid = &PointInSolid;
        api->LastError = &LastError;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCounters(BridgeInspectCounters* counters)
    {
        if (counters == null)
        {
            return 0;
        }

        try
        {
            *counters = default;
            if (Exports.Current is not { IsDisposed: false } session)
            {
                return 0;
            }

            InspectTrack.Of(session).ReadCounters(ref *counters);
            return session.Id;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPawns(BridgeInspectPawn* pawns, int capacity)
    {
        if (pawns == null || capacity <= 0 || Exports.Current is not { IsDisposed: false } session)
        {
            return 0;
        }

        try
        {
            return InspectTrack.Of(session).ReadPawns(new Span<BridgeInspectPawn>(pawns, capacity));
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetName(int id, char* buffer, int capacity)
    {
        string text = "";
        try
        {
            if (Exports.Current is { IsDisposed: false } session)
            {
                text = InspectTrack.Of(session).NameOf(id);
            }
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
        }

        return Copy(text, buffer, capacity);
    }

    private static CollisionWorld? Collision() =>
        Exports.Current is { IsDisposed: false } session ? session.Session.Client.Map?.Physics?.Collision : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Trace(float* start, float* end, float* extent, float* hit)
    {
        if (start == null || end == null)
        {
            return -1;
        }

        try
        {
            if (Collision() is not { } collision)
            {
                s_lastError = "No client world to trace.";
                return -1;
            }

            FVector size = extent != null ? new FVector(extent[0], extent[1], extent[2]) : default;
            if (!collision.LineCheck(out FCheckResult result, null, new FVector(end[0], end[1], end[2]),
                new FVector(start[0], start[1], start[2]), size, WorldTrace))
            {
                return 0;
            }

            if (hit != null)
            {
                hit[0] = result.Location.X;
                hit[1] = result.Location.Y;
                hit[2] = result.Location.Z;
                hit[3] = result.Normal.Z;
            }

            return 1;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PointInSolid(float* at, float extent)
    {
        if (at == null)
        {
            return -1;
        }

        try
        {
            if (Collision() is not { } collision)
            {
                s_lastError = "No client world to trace.";
                return -1;
            }

            return collision.PointCheck(out _, null, new FVector(at[0], at[1], at[2]), new FVector(extent, extent, extent), WorldTrace) ? 1 : 0;
        }
        catch (Exception e)
        {
            s_lastError = e.ToString();
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity) => Copy(s_lastError, buffer, capacity);

    private static int Copy(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int length = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, length).CopyTo(new Span<char>(buffer, length));
            buffer[length] = '\0';
        }

        return text.Length;
    }
}
