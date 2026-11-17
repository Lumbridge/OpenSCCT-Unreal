using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.UnrealBridge.Events;

namespace OpenScct.UnrealBridge;

/// <summary>What an animation sequence's audio segment is (<see cref="BridgeSequenceSound.Flags"/>).</summary>
public static class BridgeSequenceFlags
{
    /// <summary>The segment's sound is a 3D sound (its definition's Positional); otherwise it plays flat.</summary>
    public const int Positional = 1;

    /// <summary>The track's target actor is not in the map: the sound comes from the controller's own place.</summary>
    public const int AtController = 2;

    /// <summary>The library does not know the segment's sound (FileId is 0).</summary>
    public const int Unknown = 4;
}

/// <summary>
/// One audio segment an animation sequence plays now (Source/OpenSCCT/Runtime/ScctSequenceAudioApi.h
/// FScctSequenceSound), read each frame like the ambient sounds.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSequenceSound
{
    /// <summary>The segment instance's key (its controller, the sequence's generation and the segment), never reused in a session.</summary>
    public int Key;

    /// <summary>The segment instance's id text (EventsApi GetName), for the log.</summary>
    public int NameId;

    /// <summary>The sound's name id (EventsApi GetName).</summary>
    public int SoundId;

    /// <summary>The file it plays (EventsApi file ids), 0 when the sound has none.</summary>
    public int FileId;

    /// <summary>The track's target actor, 0 when it plays from the controller.</summary>
    public int ActorId;

    /// <summary><see cref="BridgeSequenceFlags"/>.</summary>
    public int Flags;

    /// <summary>The AnimationController that plays the sequence.</summary>
    public int ControllerId;

    /// <summary>Where it sounds from now (the target actor, else the controller), Unreal units.</summary>
    public float X, Y, Z;

    /// <summary>How far into its file the segment is now, in the file's seconds: the segment's offset plus its time so far x its rate.</summary>
    public float Offset;

    /// <summary>The segment's playback rate (its voice's pitch).</summary>
    public float Rate;

    /// <summary>Its volume (the sound's and its file's gain), 0..1.</summary>
    public float Volume;

    /// <summary>The sound's radius (its minimum distance), 0 for the default.</summary>
    public float Radius;

    /// <summary>The controller's replicated sequence clock (PlaybackTime, as last published by the server), seconds.</summary>
    public float Clock;

    /// <summary>When the segment starts in the sequence and where in its file it starts, seconds (for the log).</summary>
    public float SegmentStart, SegmentFileOffset;
}

/// <summary>
/// The animation sequences' audio. Unreal asks for <see cref="SequenceAudioExports.GetApi"/> (type
/// "OpenScct.UnrealBridge.SequenceAudioExports, OpenScct.UnrealBridge", method "GetApi").
/// Source/OpenSCCT/Runtime/ScctSequenceAudioApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct SequenceAudioApi
{
    public int Version;

    /// <summary>Fills up to capacity segments sounding now. Returns the count (0 with no session or no sound library).</summary>
    public delegate* unmanaged[Cdecl]<BridgeSequenceSound*, int, int> GetSounds;

    /// <summary>
    /// For a check: fires the event named by the UTF-16 tag on the local match's server, as a trigger would, which
    /// starts the animation controllers of that slot. Returns 1 when queued, 0 when there is no local server.
    /// </summary>
    public delegate* unmanaged[Cdecl]<char*, int> TriggerForCheck;

    /// <summary>Distinct segment sounds the library did not know, since the session started.</summary>
    public delegate* unmanaged[Cdecl]<int> GetUnknownCount;
}

public static unsafe class SequenceAudioExports
{
    public const int ApiVersion = 1;

    [UnmanagedCallersOnly]
    public static int GetApi(SequenceAudioApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetSounds = &GetSounds;
        api->TriggerForCheck = &TriggerForCheck;
        api->GetUnknownCount = &GetUnknownCount;
        return ApiVersion;
    }

    /// <summary>The segments sounding now in <paramref name="session"/> (for the probes as for Unreal).</summary>
    public static int GetSounds(BridgeSession session, Span<BridgeSequenceSound> into) => EventsFeature.Of(session).GetSequenceSounds(into);

    /// <summary>Fires <paramref name="tag"/> on the local server before its next step. 1 when queued.</summary>
    public static int TriggerForCheck(BridgeSession session, string tag) =>
        WorldDebugExports.OnServer(session, host => host.World.Level.TriggerEvent(new Name(tag), null, null));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSounds(BridgeSequenceSound* sounds, int capacity)
    {
        if (sounds == null || capacity <= 0 || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            return GetSounds(session, new Span<BridgeSequenceSound>(sounds, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TriggerForCheck(char* tag)
    {
        if (tag == null)
        {
            return 0;
        }

        try
        {
            string text = new(tag);
            return text.Length == 0 || Exports.Current is not { } session ? 0 : TriggerForCheck(session, text);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetUnknownCount()
    {
        try
        {
            return Exports.Current is { } session ? EventsFeature.Of(session).SequenceUnknownSounds : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
