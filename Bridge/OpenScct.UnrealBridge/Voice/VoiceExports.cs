using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net.Voice;
using OpenScct.UnrealBridge.Voice;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The voice chat's entry points (<see cref="VoiceBridge"/>). Unreal asks for <see cref="VoiceExports.GetApi"/>
/// (type "OpenScct.UnrealBridge.VoiceExports, OpenScct.UnrealBridge", method "GetApi").
/// Source/OpenSCCT/Runtime/ScctVoiceApi.h mirrors it. Every entry is for Unreal's game thread only: microphone
/// samples that arrive on an audio thread are queued there and handed in from the game thread.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct VoiceApi
{
    public int Version;

    /// <summary>Size of <see cref="VoiceTalkerInfo"/> in bytes, so C++ can check its mirror.</summary>
    public int TalkerInfoSize;

    /// <summary>
    /// Once a frame: runs the echo test while it is on. The visit's voice needs no call here; the online table's
    /// Update ticks it after the visit.
    /// </summary>
    public delegate* unmanaged[Cdecl]<void> Update;

    /// <summary>The rate of the microphone samples SubmitCapture receives (8000 to 192000).</summary>
    public delegate* unmanaged[Cdecl]<int, void> SetCaptureRate;

    /// <summary>Microphone samples: mono floats, -1 to 1, at the capture rate.</summary>
    public delegate* unmanaged[Cdecl]<float*, int, void> SubmitCapture;

    /// <summary>What the voice output played (48 kHz mono, all talkers mixed at the output volume): the echo reference.</summary>
    public delegate* unmanaged[Cdecl]<float*, int, void> SubmitPlayback;

    /// <summary>The push to talk keys held: talk (all channel), team (team channel), 0 or 1.</summary>
    public delegate* unmanaged[Cdecl]<int, int, void> SetKeys;

    public delegate* unmanaged[Cdecl]<VoiceSettingsBlock*, void> ApplySettings;

    public delegate* unmanaged[Cdecl]<VoiceSettingsBlock*, void> GetSettings;

    public delegate* unmanaged[Cdecl]<VoiceStatus*, void> GetState;

    /// <summary>Fills up to capacity rows; returns how many talkers there are (may be more than the capacity).</summary>
    public delegate* unmanaged[Cdecl]<VoiceTalkerInfo*, int, int> GetTalkers;

    /// <summary>The talker's next 20 ms into 960 floats at 48 kHz; 1 when a frame was written, 0 when none is due.</summary>
    public delegate* unmanaged[Cdecl]<int, float*, int> ReadFrame;

    public delegate* unmanaged[Cdecl]<int, int, void> SetMuted;

    public delegate* unmanaged[Cdecl]<int, float, void> SetVolume;

    /// <summary>Starts the microphone test (seconds recorded, 0 live); 1 started, 0 refused while online.</summary>
    public delegate* unmanaged[Cdecl]<int, int> StartEchoTest;

    public delegate* unmanaged[Cdecl]<void> StopEchoTest;

    /// <summary>The last failure inside a voice entry point into the buffer; returns its full length (0: none).</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> GetError;
}

public static unsafe class VoiceExports
{
    [UnmanagedCallersOnly]
    public static int GetApi(VoiceApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = VoiceBridge.ApiVersion;
        api->TalkerInfoSize = sizeof(VoiceTalkerInfo);
        api->Update = &Update;
        api->SetCaptureRate = &SetCaptureRate;
        api->SubmitCapture = &SubmitCapture;
        api->SubmitPlayback = &SubmitPlayback;
        api->SetKeys = &SetKeys;
        api->ApplySettings = &ApplySettings;
        api->GetSettings = &GetSettings;
        api->GetState = &GetState;
        api->GetTalkers = &GetTalkers;
        api->ReadFrame = &ReadFrame;
        api->SetMuted = &SetMuted;
        api->SetVolume = &SetVolume;
        api->StartEchoTest = &StartEchoTest;
        api->StopEchoTest = &StopEchoTest;
        api->GetError = &GetError;
        return VoiceBridge.ApiVersion;
    }

    private static VoiceBridge Voice => VoiceBridge.Instance;

    private static void Fail(Exception e) => Voice.LastError = e.ToString();

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Update()
    {
        try
        {
            Voice.Update();
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetCaptureRate(int rate)
    {
        try
        {
            Voice.SetCaptureRate(rate);
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SubmitCapture(float* samples, int count)
    {
        if (samples == null || count <= 0)
        {
            return;
        }

        try
        {
            Voice.SubmitCapture(new ReadOnlySpan<float>(samples, count));
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SubmitPlayback(float* samples, int count)
    {
        if (samples == null || count <= 0)
        {
            return;
        }

        try
        {
            Voice.SubmitPlayback(new ReadOnlySpan<float>(samples, count));
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetKeys(int talk, int team) => Voice.SetKeys(talk != 0, team != 0);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ApplySettings(VoiceSettingsBlock* settings)
    {
        if (settings != null)
        {
            Voice.ApplySettings(in *settings);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetSettings(VoiceSettingsBlock* settings)
    {
        if (settings != null)
        {
            *settings = Voice.ReadSettings();
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void GetState(VoiceStatus* status)
    {
        if (status == null)
        {
            return;
        }

        try
        {
            *status = Voice.ReadState();
        }
        catch (Exception e)
        {
            *status = default;
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetTalkers(VoiceTalkerInfo* rows, int capacity)
    {
        try
        {
            return Voice.ReadTalkers(rows == null || capacity <= 0 ? default : new Span<VoiceTalkerInfo>(rows, capacity));
        }
        catch (Exception e)
        {
            Fail(e);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ReadFrame(int talker, float* frame)
    {
        if (frame == null)
        {
            return 0;
        }

        try
        {
            return Voice.ReadFrame(talker, new Span<float>(frame, VoiceK.FrameSamples)) ? 1 : 0;
        }
        catch (Exception e)
        {
            Fail(e);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetMuted(int talker, int muted)
    {
        try
        {
            Voice.SetMuted(talker, muted != 0);
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetVolume(int talker, float volume)
    {
        try
        {
            Voice.SetVolume(talker, volume);
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartEchoTest(int recordSeconds)
    {
        try
        {
            return Voice.StartEchoTest(recordSeconds) ? 1 : 0;
        }
        catch (Exception e)
        {
            Fail(e);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void StopEchoTest()
    {
        try
        {
            Voice.StopEchoTest();
        }
        catch (Exception e)
        {
            Fail(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetError(char* buffer, int capacity) => OnlineTextCopy.Buffer(buffer, capacity, Voice.LastError);
}
