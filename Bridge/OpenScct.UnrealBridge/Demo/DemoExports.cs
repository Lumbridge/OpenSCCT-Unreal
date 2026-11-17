using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Demo;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>DemoApi.GetInfo's answer (Source/OpenSCCT/Runtime/ScctDemoApi.h FScctDemoInfo).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct DemoInfoBlock
{
    public int Flags;     // DemoFlags
    public int Revision;  // goes up whenever anything here or in the texts changed
}

/// <summary>
/// The demo table (Source/OpenSCCT/Runtime/ScctDemoApi.h mirrors it): the player's demo-saving choice and its status,
/// all of <see cref="DemoSaving"/>. Unreal asks for <see cref="GetApi"/> once and keeps the table. Every entry catches
/// its exceptions (none may cross into native code); an unexpected one goes to the main table's LastError and the call
/// returns its empty answer. Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct DemoApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, void> Configure;
    public delegate* unmanaged[Cdecl]<DemoInfoBlock*, int> GetInfo;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
}

public static unsafe class DemoExports
{
    public const int ApiVersion = 1;

    /// <summary>DemoApi.Configure's flags.</summary>
    public const int ConfigureOffline = 1;

    public const int ConfigureOnline = 2;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(DemoApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Configure = &Configure;
        api->GetInfo = &GetInfo;
        api->GetText = &GetText;
        return ApiVersion;
    }

    /// <summary>The demo folder and which matches are saved (ConfigureOffline, ConfigureOnline).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Configure(char* folder, int flags)
    {
        try
        {
            DemoSaving.Configure(folder == null ? null : new string(folder), (flags & ConfigureOffline) != 0, (flags & ConfigureOnline) != 0);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInfo(DemoInfoBlock* info)
    {
        if (info == null)
        {
            return 0;
        }

        try
        {
            *info = new DemoInfoBlock { Flags = DemoSaving.Flags(), Revision = DemoSaving.Revision() };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            *info = default;
            return 0;
        }
    }

    /// <summary>A text (<see cref="DemoText"/>). Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, DemoSaving.Text((DemoText)kind));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }
}
