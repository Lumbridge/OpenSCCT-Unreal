using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Events;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Entry points the audio's unattended checks use to force a state the game would reach only later (a round's
/// music). Unreal asks for <see cref="AudioCheckExports.GetApi"/> (type "OpenScct.UnrealBridge.AudioCheckExports,
/// OpenScct.UnrealBridge", method "GetApi"). Source/OpenSCCT/Runtime/ScctAudioCheckApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct AudioCheckApi
{
    public int Version;

    /// <summary>
    /// Starts the named Sound (UTF-16, zero terminated: "Amb_Aqua.Ambiances.aqua_musicA") as the music, faded in over
    /// the seconds given, through the same path as the script's PlayMusic. Returns 1 when it plays, 0 when no session
    /// runs or the sound is unknown or resolves to no file.
    /// </summary>
    public delegate* unmanaged[Cdecl]<char*, float, int> PlayMusic;
}

public static unsafe class AudioCheckExports
{
    public const int ApiVersion = 1;

    [UnmanagedCallersOnly]
    public static int GetApi(AudioCheckApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->PlayMusic = &PlayMusic;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PlayMusic(char* song, float fadeIn)
    {
        if (song == null || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            return EventsFeature.Of(session).PlayMusicForCheck(new string(song), Math.Max(fadeIn, 0f)) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
