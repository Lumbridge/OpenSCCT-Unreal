using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of vision entry points Unreal calls (Source/OpenSCCT/Runtime/ScctVisionApi.h mirrors it): the values
/// thermal, EMF and motion vision draw each actor by. Unreal asks for <see cref="VisionExports.GetApi"/> once and
/// keeps the table. Every entry works on the running session and returns 0 when there is none. Name and class ids
/// are ids into the world table's names (WorldExports.GetName).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct VisionApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<BridgeVisionPawn*, int, int> GetPawns;
    public delegate* unmanaged[Cdecl]<BridgeVisionActor*, int, int> GetActors;
    public delegate* unmanaged[Cdecl]<BridgeVisionLevel*, int> GetLevel;
    public delegate* unmanaged[Cdecl]<BridgeVisionCamera*, BridgeVisionLock*, int, int> GetMotionLocks;
}

public static unsafe class VisionExports
{
    /// <summary>1: pawns, actors, the level and the motion tracker's locks. 2: the level's GlowThreshold.</summary>
    public const int ApiVersion = 2;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(VisionApi* api)
    {
        api->Version = ApiVersion;
        api->GetPawns = &GetPawns;
        api->GetActors = &GetActors;
        api->GetLevel = &GetLevel;
        api->GetMotionLocks = &GetMotionLocks;
        return ApiVersion;
    }

    /// <summary>The running session's vision state, or null.</summary>
    private static VisionState? State => Exports.Current is { IsDisposed: false } session ? VisionState.Of(session) : null;

    /// <summary>Writes every pawn's vision values. Returns how many pawns there are (a null buffer asks for the count).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPawns(BridgeVisionPawn* rows, int capacity)
    {
        try
        {
            return State?.GetPawns(SpanOf(rows, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the other actors the vision modes draw by their own values. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActors(BridgeVisionActor* rows, int capacity)
    {
        try
        {
            return State?.GetActors(SpanOf(rows, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the level's vision values. Returns 1, or 0 while no map is loaded.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLevel(BridgeVisionLevel* level)
    {
        try
        {
            if (level == null || State is not { } state || state.GetLevel(out BridgeVisionLevel row) == 0)
            {
                return 0;
            }

            *level = row;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Boxes the local merc's motion tracker locks from <paramref name="camera"/> and writes them in the 640 x 480
    /// HUD frame. Returns how many locks there are; 0 without a camera, a tracker or locks.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMotionLocks(BridgeVisionCamera* camera, BridgeVisionLock* rows, int capacity)
    {
        try
        {
            return camera == null ? 0 : State?.GetMotionLocks(*camera, SpanOf(rows, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static Span<T> SpanOf<T>(T* buffer, int capacity)
        where T : unmanaged =>
        buffer == null || capacity <= 0 ? default : new Span<T>(buffer, capacity);
}
