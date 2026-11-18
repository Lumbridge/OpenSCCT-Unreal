using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Equipment;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The entry points of the grenade preview (the console command nadepreview). Unreal asks for
/// <see cref="NadePreviewExports.GetApi"/> once (type "OpenScct.UnrealBridge.NadePreviewExports,
/// OpenScct.UnrealBridge", method "GetApi") and keeps the table. Source/OpenSCCT/Runtime/ScctNadePreviewApi.h
/// mirrors it field for field. Every entry returns 0 when no session runs and catches its exceptions (a failure
/// stops the session with the text as the bridge's last error).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NadePreviewApi
{
    public int Version;

    /// <summary>1 when the running session may show the preview (a match on this machine, or a practice round), else 0.</summary>
    public delegate* unmanaged[Cdecl]<int> IsAllowed;

    /// <summary>
    /// Writes this frame's preview: the head and up to capacity points of the path, the launch point first. Returns
    /// the points written; the head's State is None (and 0 is returned) when there is nothing to draw. Call it after
    /// the frame's view was read (the aim is that view's).
    /// </summary>
    public delegate* unmanaged[Cdecl]<BridgeNadePreview*, BridgeNadePoint*, int, int> GetPreview;
}

public static unsafe class NadePreviewExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(NadePreviewApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->IsAllowed = &IsAllowed;
        api->GetPreview = &GetPreview;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int IsAllowed()
    {
        try
        {
            return NadePreviewFeature.IsAllowed(Exports.Current) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPreview(BridgeNadePreview* head, BridgeNadePoint* points, int capacity)
    {
        if (head == null)
        {
            return 0;
        }

        *head = default;
        if (points == null || capacity <= 0 || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            int count = NadePreviewFeature.Of(session).Read(out BridgeNadePreview read, new Span<BridgeNadePoint>(points, capacity));
            *head = read;
            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
