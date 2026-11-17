using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The offline match's table (Source/OpenSCCT/Runtime/ScctOfflineMatchApi.h mirrors it): what the local match that
/// plays was really started with, as the simulation's game rules read it. The Offline page's options (practice,
/// sidearms, rifles, responsive smoke, match size) travel through the level URL and StartLocalEx into PlaySettings,
/// and this hands back PlaySettings.GameOptions as the map loader used it, so the log shows what reached the rules.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OfflineMatchApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetGameOptions;
}

public static unsafe class OfflineMatchExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(OfflineMatchApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetGameOptions = &GetGameOptions;
        return ApiVersion;
    }

    /// <summary>The game options of the local match that plays ("?GameMode=5...?SvmResponsiveSmoke=1?SvmMatchPlayers=4"), empty for none or an online round.</summary>
    public static string GameOptions() =>
        Exports.Current is { IsRemote: false } current ? current.Session.Loader?.GameOptions ?? string.Empty : string.Empty;

    /// <summary>Writes the options, cut to fit and zero terminated. Returns their full length, 0 when no local match plays.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetGameOptions(char* buffer, int capacity)
    {
        try
        {
            string text = GameOptions();
            if (buffer != null && capacity > 0)
            {
                int count = Math.Min(text.Length, capacity - 1);
                text.AsSpan(0, count).CopyTo(new Span<char>(buffer, capacity));
                buffer[count] = '\0';
            }

            return text.Length;
        }
        catch (Exception)
        {
            // Nothing may cross into native code.
            return 0;
        }
    }
}
