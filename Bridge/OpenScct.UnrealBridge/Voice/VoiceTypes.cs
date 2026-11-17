using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctVoiceApi.h mirrors them field for field;
// change both together and bump VoiceBridge.ApiVersion. Text fields are UTF-16, zero terminated, and cut to their
// capacity. Flags are 0 or 1.

/// <summary>
/// The player's voice options (OpenSCCT's VoiceSettings, the options' Voice page in the Godot client), as Unreal's
/// settings registry holds them. <c>ApplySettings</c> copies them into the voice client at once.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct VoiceSettingsBlock
{
    /// <summary>Voice chat at all: off, nothing is sent or received.</summary>
    public int Enabled;

    /// <summary>Transmit only while a talk key is held; off, the activation gate decides.</summary>
    public int PushToTalk;

    /// <summary>Self mute: nothing is sent, everyone else is still heard.</summary>
    public int MicMuted;

    /// <summary>Activation gate threshold on a frame's RMS, 0 (always open) to 1 (never).</summary>
    public float ActivationThreshold;

    /// <summary>How long the gate stays open after the voice drops, in milliseconds (0 to 5000).</summary>
    public int ActivationHoldMs;

    /// <summary>Microphone gain before the gate and the encoder, 0 to 8 (not used while AutoGain is on).</summary>
    public float InputGain;

    /// <summary>
    /// Volume of the other players' voices, 0 to 1. Kept for the state read only: <c>ReadFrame</c> does not apply it,
    /// the voice chat submix does (as Godot's SvmVoice bus does), so the echo canceller's reference can follow it.
    /// </summary>
    public float OutputVolume;

    /// <summary>Jitter buffer depth in 20 ms frames, 1 to 12.</summary>
    public int JitterFrames;

    public int NoiseSuppression;

    /// <summary>The suppression's most attenuation in dB: 15, 30, 45 or 60.</summary>
    public int NoiseSuppressionDecibels;

    public int AutoGain;

    /// <summary>Echo cancellation against what <c>SubmitPlayback</c> hands back (experimental, as in Godot).</summary>
    public int EchoCancellation;
}

/// <summary>Where the echo test is (<see cref="VoiceStatus.EchoPhase"/>).</summary>
public enum VoiceEchoPhase
{
    Off = 0,

    /// <summary>The microphone goes through the encoder, an in-process relay and a listening client; what it decodes is kept.</summary>
    Recording = 1,

    /// <summary>The recording comes back through <c>ReadFrame</c> (talker <see cref="Voice.VoiceEchoTest.Talker"/>).</summary>
    Playing = 2,

    /// <summary>Played back to the end.</summary>
    Done = 3,

    /// <summary>The recording time passed and nothing came through: no microphone samples arrived.</summary>
    NothingCaptured = 4,

    /// <summary>Live loopback: what the listener decodes comes straight back through <c>ReadFrame</c>.</summary>
    Live = 5,
}

/// <summary>The voice chat in one read (<c>GetState</c>), cheap enough for every frame.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct VoiceStatus
{
    /// <summary>The voice client has a server to send to (a visit's map, or the echo test's relay).</summary>
    public int Connected;

    /// <summary>The microphone is being sent now.</summary>
    public int Transmitting;

    /// <summary>The channel being sent on: 0 all, 1 team.</summary>
    public int Channel;

    public int MicMuted;

    /// <summary>The local player's talker id on the current map (0 until known).</summary>
    public int LocalTalker;

    /// <summary>Remote talkers heard so far on this visit (<c>GetTalkers</c>).</summary>
    public int TalkerCount;

    /// <summary><see cref="VoiceEchoPhase"/>.</summary>
    public int EchoPhase;

    public int Enabled;

    /// <summary>RMS of the last microphone frame after gain, 0 to 1 (the level meter).</summary>
    public float InputLevel;

    /// <summary>The automatic gain in use (1 while it is off).</summary>
    public float AutoGain;

    public long FramesCaptured;
    public long FramesSent;
    public long PacketsReceived;
}

/// <summary>One remote talker (<c>GetTalkers</c>): a player heard on this visit's current map.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct VoiceTalkerInfo
{
    public const int NameCapacity = 64;

    /// <summary>The talker id <c>ReadFrame</c>, <c>SetMuted</c> and <c>SetVolume</c> take.</summary>
    public int Talker;

    /// <summary>0 spies, 1 mercs, 255 spectators (lobby), -1 not known.</summary>
    public int Team;

    /// <summary>Being heard now (the speaking indicator).</summary>
    public int Speaking;

    /// <summary>The channel of the latest packet: 0 all, 1 team.</summary>
    public int Channel;

    /// <summary>Muted by the local player.</summary>
    public int Muted;

    /// <summary>The local player's volume for this talker, 0 to 2.</summary>
    public float Volume;

    /// <summary>RMS of the last frame read, after the volume.</summary>
    public float Level;

    /// <summary>Frames waiting in the jitter buffer.</summary>
    public int QueuedFrames;

    public long FramesDecoded;
    public long DecodeErrors;

    /// <summary>The player's name (lobby player or PlayerReplicationInfo), or "Player n" while unknown.</summary>
    public fixed char Name[NameCapacity];
}
