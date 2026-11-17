using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Social;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The main menu dashboard's table (Source/OpenSCCT/Runtime/ScctDashboardApi.h mirrors it): the master's status, the
/// game and hub lists, the changelog and the master version line of <see cref="MenuDashboard"/>, which the social
/// table's Update runs each frame. Every entry catches its exceptions (none may cross into native code); an unexpected
/// one goes to the main table's LastError and the call returns its empty answer. Strings are UTF-16, zero terminated.
/// New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct DashboardApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, void> SetWatched;
    public delegate* unmanaged[Cdecl]<DashboardInfoBlock*, int> GetInfo;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<int, char*, int, char*, int, char*, int, char*, int, char*, int, int> GetRelease;
    public delegate* unmanaged[Cdecl]<int, void> SetPreview;
}

public static unsafe class DashboardExports
{
    public const int ApiVersion = 1;

    private static MenuDashboard Dashboard => SocialBridge.Instance.Dashboard;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(DashboardApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->SetWatched = &SetWatched;
        api->GetInfo = &GetInfo;
        api->GetText = &GetText;
        api->GetRelease = &GetRelease;
        api->SetPreview = &SetPreview;
        return ApiVersion;
    }

    /// <summary>A page that shows the dashboard is on screen (1) or not (0): the feeds are polled only while it is.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetWatched(int watched)
    {
        try
        {
            Dashboard.SetWatched(watched != 0);
        }
        catch (Exception e)
        {
            Failed(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInfo(DashboardInfoBlock* info)
    {
        if (info == null)
        {
            return 0;
        }

        try
        {
            DashboardInfoBlock block = default;
            Dashboard.Fill(ref block);
            *info = block;
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            *info = default;
            return 0;
        }
    }

    /// <summary>A text (<see cref="DashboardText"/>). Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, Dashboard.Text((DashboardText)kind));
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>The release at <paramref name="index"/> (newest first): tag, name, date, plain notes and its page. Returns 0 past the end.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRelease(int index, char* tag, int tagCapacity, char* name, int nameCapacity, char* date, int dateCapacity,
        char* notes, int notesCapacity, char* url, int urlCapacity)
    {
        try
        {
            if (!Dashboard.Release(index, out string t, out string n, out string d, out string x, out string u))
            {
                return 0;
            }

            OnlineTextCopy.Buffer(tag, tagCapacity, t);
            OnlineTextCopy.Buffer(name, nameCapacity, n);
            OnlineTextCopy.Buffer(date, dateCapacity, d);
            OnlineTextCopy.Buffer(url, urlCapacity, u);
            // The notes' full length, so a caller can ask again with room for all of them.
            return Math.Max(1, OnlineTextCopy.Buffer(notes, notesCapacity, x));
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Made-up data for checks (-ScctDashboardPreview): nothing is asked of the master.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetPreview(int preview)
    {
        try
        {
            Dashboard.SetPreview(preview != 0);
        }
        catch (Exception e)
        {
            Failed(e);
        }
    }

    private static void Failed(Exception e) => Exports.ReportFailure(e.ToString());
}
