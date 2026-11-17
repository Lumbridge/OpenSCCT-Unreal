using System;
using System.Collections.Generic;
using System.Diagnostics;
using OpenScct.Core.Engine;
using OpenScct.Net;
using OpenScct.Net.Voice;

namespace OpenScct.UnrealBridge.Voice;

/// <summary>
/// The Voice page's "Test my microphone" without a server, ported from the Godot client's OptionsMenu.MicTest
/// (game/Scripts/Options/OptionsMenu.Voice.cs): the player's settings on a voice client that talks to a
/// <see cref="VoiceRelay"/> in this process over loopback transports, and a second client that listens, so what comes
/// back went through the same gate, Opus encoder, wire, relay, jitter buffer and decoder as a real match. Recorded
/// mode keeps what the listener decoded for a few seconds, then hands it back frame by frame; live mode hands each
/// decoded frame back as soon as it is there.
/// <para>Single-threaded, on the caller's thread (Unreal's game thread).</para>
/// </summary>
internal sealed class VoiceEchoTest : IDisposable
{
    /// <summary>The talking client's id; <c>ReadFrame</c> of this talker plays the test back.</summary>
    public const ushort Talker = 1;

    private const ushort ListenerTalker = 2;

    private readonly LoopbackNetwork _network = new();
    private readonly StopwatchClock _clock = new();
    private readonly VoiceTransport _server;
    private readonly VoiceTransport _talkerTransport;
    private readonly VoiceTransport _listenerTransport;
    private readonly VoiceSettings _talkerSettings = new();
    private readonly VoiceSettings _listenerSettings = new();
    private readonly List<float> _recorded = new();
    private readonly float[] _frame = new float[VoiceK.FrameSamples];
    private readonly long _recordMicros;
    private int _played;

    public VoiceEchoTest(VoiceSettings player, int captureRate, int recordSeconds)
    {
        _server = new VoiceTransport(_network.Bind("echo-relay"));
        var directory = new StaticVoiceDirectory { Rules = new VoiceRules(EVoiceChatMode.VCM_Open, false, true) };
        directory.Set(VoiceMember.Create(Talker, 0));
        directory.Set(VoiceMember.Create(ListenerTalker, 0));
        _ = new VoiceRelay(_server, directory, _clock);
        Follow(player);
        _talkerTransport = new VoiceTransport(_network.Bind("echo-talker"));
        Client = new VoiceClient(_talkerSettings, _clock) { LocalTalker = Talker };
        Client.Connect(_talkerTransport, _talkerTransport.Connect("echo-relay"));
        Client.SetCaptureRate(captureRate);
        _listenerTransport = new VoiceTransport(_network.Bind("echo-listener"));
        Listener = new VoiceClient(_listenerSettings, _clock) { LocalTalker = ListenerTalker };
        Listener.Connect(_listenerTransport, _listenerTransport.Connect("echo-relay"));
        _recordMicros = Math.Clamp(recordSeconds, 0, 10) * 1_000_000L;
        Phase = recordSeconds <= 0 ? VoiceEchoPhase.Live : VoiceEchoPhase.Recording;
    }

    /// <summary>The talking client: the microphone goes here while the test runs.</summary>
    public VoiceClient Client { get; }

    /// <summary>The listening client: its talker is the player's own voice as another player would hear it.</summary>
    public VoiceClient Listener { get; }

    public VoiceEchoPhase Phase { get; private set; }

    public bool Running => Phase is VoiceEchoPhase.Recording or VoiceEchoPhase.Playing or VoiceEchoPhase.Live;

    /// <summary>Samples the listener decoded so far (recorded mode).</summary>
    public int RecordedSamples => _recorded.Count;

    /// <summary>
    /// The player's settings as the test uses them (Godot's MicTest): always the activation gate, wide open when the
    /// player uses push to talk, so the test needs no key; never muted.
    /// </summary>
    public void Follow(VoiceSettings player)
    {
        VoiceBridge.Copy(player, _talkerSettings);
        _talkerSettings.Enabled = true;
        _talkerSettings.MicMuted = false;
        _talkerSettings.PushToTalk = false;
        _talkerSettings.ActivationThreshold = player.PushToTalk ? 0f : player.ActivationThreshold;
        _talkerSettings.EchoCancellation = false;
        _listenerSettings.JitterFrames = player.JitterFrames;
    }

    /// <summary>Moves the datagrams, updates both clients and, recording, keeps what the listener decoded.</summary>
    public void Update(VoiceSettings player)
    {
        if (!Running)
        {
            return;
        }

        Follow(player);
        _talkerTransport.DrainVoiceOnly();
        _server.DrainVoiceOnly();
        _listenerTransport.DrainVoiceOnly();
        Client.Update();
        Listener.Update();
        if (Phase != VoiceEchoPhase.Recording)
        {
            return;
        }

        foreach (VoiceTalker t in Listener.Talkers)
        {
            while (t.Buffer.Count > 0 && t.ReadFrame(_frame))
            {
                _recorded.AddRange(_frame);
            }
        }

        if (_clock.NowMicroseconds >= _recordMicros)
        {
            Phase = _recorded.Count == 0 ? VoiceEchoPhase.NothingCaptured : VoiceEchoPhase.Playing;
        }
    }

    /// <summary>The next 20 ms coming back; false when there is none (the destination is left alone).</summary>
    public bool ReadFrame(Span<float> destination)
    {
        if (Phase == VoiceEchoPhase.Live)
        {
            return Listener.FindTalker(Talker) is { } talker && talker.ReadFrame(destination);
        }

        if (Phase != VoiceEchoPhase.Playing)
        {
            return false;
        }

        int count = Math.Min(VoiceK.FrameSamples, _recorded.Count - _played);
        if (count <= 0)
        {
            Phase = VoiceEchoPhase.Done;
            return false;
        }

        destination[..VoiceK.FrameSamples].Clear();
        for (int i = 0; i < count; i++)
        {
            destination[i] = _recorded[_played + i];
        }

        _played += count;
        if (_played >= _recorded.Count)
        {
            Phase = VoiceEchoPhase.Done;
        }

        return true;
    }

    /// <summary>The test's one talker, as the listener hears it (its level and counters for the talker list).</summary>
    public VoiceTalker? Heard => Listener.FindTalker(Talker);

    public void Dispose()
    {
        if (Running)
        {
            Phase = VoiceEchoPhase.Off;
        }

        Client.Dispose();
        Listener.Dispose();
        _talkerTransport.Dispose();
        _listenerTransport.Dispose();
        _server.Dispose();
    }

    private sealed class StopwatchClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
