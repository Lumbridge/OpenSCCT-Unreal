using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Test;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The entry points of the practice grab-area overlay. Unreal asks for <see cref="GrabAreaExports.GetApi"/> once
/// (type "OpenScct.UnrealBridge.GrabAreaExports, OpenScct.UnrealBridge", method "GetApi") and keeps the table.
/// Source/OpenSCCT/Test/ScctGrabAreaApi.h mirrors it field for field. Every entry returns 0 when no session runs and
/// catches its exceptions (a failure stops the session with the text as the bridge's last error).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct GrabAreaApi
{
    public int Version;

    /// <summary>1 when the running session is a practice round (the level's PracticeRules on), else 0.</summary>
    public delegate* unmanaged[Cdecl]<int> IsPracticeRound;

    /// <summary>
    /// Writes every living mercenary's grab window as of the last tick (<see cref="BridgeGrabArea"/>). Returns the
    /// count written; 0 outside a practice round.
    /// </summary>
    public delegate* unmanaged[Cdecl]<BridgeGrabArea*, int, int> GetGrabAreas;
}

public static unsafe class GrabAreaExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(GrabAreaApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->IsPracticeRound = &IsPracticeRound;
        api->GetGrabAreas = &GetGrabAreas;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int IsPracticeRound()
    {
        try
        {
            return GrabAreaReader.IsPracticeRound(Exports.Current?.Session) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetGrabAreas(BridgeGrabArea* areas, int capacity)
    {
        if (areas == null || capacity <= 0 || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            return GrabAreaReader.Read(session.Session, new Span<BridgeGrabArea>(areas, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
