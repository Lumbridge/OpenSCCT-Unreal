using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Events;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Entry points the audio's unattended checks use to force a state the game would reach only later (a round's
/// music, a sound at a chosen place). Unreal asks for <see cref="AudioCheckExports.GetApi"/> (type "OpenScct.UnrealBridge.AudioCheckExports,
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

    /// <summary>
    /// Version 2. Plays the named Sound at X, Y, Z (world units) with the radius given (0: the default hearer radius)
    /// as a PlaySound call of no actor: a test emitter that arrives as an ordinary Play record (a looping sound loops
    /// until the session ends). Returns the call's id, 0 when no session runs or the sound is unknown or has no file.
    /// </summary>
    public delegate* unmanaged[Cdecl]<char*, float, float, float, float, int> PlaySoundAt;
}

public static unsafe class AudioCheckExports
{
    public const int ApiVersion = 2;

    [UnmanagedCallersOnly]
    public static int GetApi(AudioCheckApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->PlayMusic = &PlayMusic;
        api->PlaySoundAt = &PlaySoundAt;
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

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PlaySoundAt(char* sound, float x, float y, float z, float radius)
    {
        if (sound == null || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            return EventsFeature.Of(session).PlaySoundForCheck(new string(sound), new OpenScct.Core.FVector(x, y, z), Math.Max(radius, 0f));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
