using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Net.Voice;
using OpenScct.Server;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Voice;

/// <summary>
/// The Unreal client's voice chat (docs/VOICE.md in OpenSCCT), ported from the Godot client's OnlineVoiceChat with the
/// audio and the overlay left to Unreal: one <see cref="VoiceClient"/> at 48 kHz and one <see cref="OnlineVoice"/> on
/// the online bridge's visit, made when the visit starts and disposed when it ends. OnlineVoice keeps the client
/// connected to each map's voice transport as the visit travels (lobby, rounds, lobby again), tells it the local
/// player's id, names the talkers (lobby players, then PlayerReplicationInfos) and keeps the player's volume and mute
/// for each other player by name, so they hold from map to map. Unreal hands the microphone in
/// (<see cref="SubmitCapture"/>), reads each talker's decoded frames (<see cref="ReadFrame"/>), the talker list and the
/// state, and sets the keys and the options.
/// <para>Without a visit the Voice page's microphone test runs instead (<see cref="StartEchoTest"/>): the same client
/// code against a relay in this process.</para>
/// <para>Single-threaded: everything is called on Unreal's game thread. The visit's voice is updated by
/// <see cref="AfterVisitTick"/>, which the online bridge calls once a frame after its visit tick; the echo test by
/// <see cref="Update"/>.</para>
/// </summary>
public sealed partial class VoiceBridge : IDisposable
{
    public const int ApiVersion = 4;

    /// <summary>The talker id the echo test comes back on (<see cref="ReadFrame"/>).</summary>
    public const int EchoTalker = VoiceEchoTest.Talker;

    private readonly VoiceSettings _settings = new();
    private VoiceVolumes _volumes = new();
    private readonly float[] _scratch = new float[VoiceK.FrameSamples];
    private readonly Dictionary<ushort, int> _teams = new();
    private OnlineClient? _visit;
    private OnlineVoice? _online;
    private VoiceEchoTest? _echo;
    private readonly VoiceSettings _meterSettings = new();
    private VoiceClient? _meter;
    private bool _meterWanted;
    private int _captureRate = VoiceK.SampleRate;
    private bool _talkKey;
    private bool _teamKey;
    private VoiceLinks.Own _own;

    private static VoiceBridge? s_instance;

    /// <summary>The one voice bridge of the process, made on first use.</summary>
    public static VoiceBridge Instance => s_instance ??= new VoiceBridge();

    /// <summary>The voice bridge if one was made, without making one.</summary>
    internal static VoiceBridge? Existing => s_instance;

    /// <summary>The player's voice options (one object for the visit's client and the echo test).</summary>
    public VoiceSettings Settings => _settings;

    /// <summary>The visit's voice, or null when not online.</summary>
    public OnlineVoice? Online => _online;

    /// <summary>
    /// The client the microphone goes to: the echo test's while it runs, else the visit's, else the Voice page's level
    /// meter while it is wanted, else none.
    /// </summary>
    public VoiceClient? Capturing => _echo is { Running: true } echo ? echo.Client : _online?.Voice ?? _meter;

    internal VoiceEchoTest? Echo => _echo;

    /// <summary>The last failure inside a voice entry point, as text, or null.</summary>
    public string? LastError { get; internal set; }

    // ---- the visit ----------------------------------------------------------------------------------------------

    /// <summary>
    /// Once a frame after the online bridge ran its visit ticks: the visit's voice follows the visit to its current
    /// map and updates (join refresh, settings, speaking indicators). A new visit gets a new voice client; the echo
    /// test stops, since the visit now owns the microphone. A failure here is kept in <see cref="LastError"/>; it never
    /// stops the visit.
    /// </summary>
    public void AfterVisitTick(OnlineClient client)
    {
        ArgumentNullException.ThrowIfNull(client);
        try
        {
            if (_online is null || !ReferenceEquals(_visit, client))
            {
                StartVisit(client);
            }

            _own = VoiceLinks.ReadOwn(VoiceLinks.LocalController(client));
            ApplyKeys();
            _online!.Update();
        }
        catch (Exception e)
        {
            LastError = e.ToString();
        }
    }

    /// <summary>The visit ended: its voice client lets go of the transport.</summary>
    public void EndVisit()
    {
        _online?.Dispose();
        _online = null;
        _visit = null;
        _own = default;
        _teams.Clear();
    }

    private void StartVisit(OnlineClient client)
    {
        EndVisit();
        StopEchoTest();
        // The clean-up (echo, noise, automatic gain) runs on the voice thread, as in the Godot client.
        var voice = new VoiceClient(_settings, UnrealBridge.Online.OnlineBridge.Instance.Clock) { UseVoiceThread = true };
        voice.SetCaptureRate(_captureRate);
        _online = new OnlineVoice(client, voice, _volumes);
        _visit = client;
        // The players muted earlier in this run (another visit, or before this one began) stay muted.
        foreach (string name in _mutedNames)
        {
            _online.SetMuted(name, true);
        }
    }

    // ---- the microphone and the keys ----------------------------------------------------------------------------

    /// <summary>The rate of the samples <see cref="SubmitCapture"/> receives (Unreal's capture device rate).</summary>
    public void SetCaptureRate(int sampleRate)
    {
        _captureRate = Math.Clamp(sampleRate, 8000, 192000);
        _online?.Voice.SetCaptureRate(_captureRate);
        _echo?.Client.SetCaptureRate(_captureRate);
        _meter?.SetCaptureRate(_captureRate);
    }

    /// <summary>Microphone samples, mono, -1 to 1, at the capture rate. Dropped when nothing listens.</summary>
    public void SubmitCapture(ReadOnlySpan<float> mono) => Capturing?.SubmitCapture(mono);

    /// <summary>What the voice output played (48 kHz mono, the mix of every talker): the echo canceller's reference.</summary>
    public void SubmitPlayback(ReadOnlySpan<float> mono) => _online?.Voice.SubmitPlayback(mono);

    /// <summary>The push to talk keys: <paramref name="talk"/> the all channel, <paramref name="team"/> the team channel.</summary>
    public void SetKeys(bool talk, bool team)
    {
        _talkKey = talk;
        _teamKey = team;
        ApplyKeys();
    }

    /// <summary>
    /// The keys onto the visit's client. Answering the HUD's Speak prompt (the Hack Comm key next to a downed enemy, or
    /// on the merc a spy holds) talks on the all channel, as a held all-channel key would: downed chat and grab talk go
    /// only on that channel, so push to talk or a held team key would otherwise leave the prompt saying nothing.
    /// </summary>
    private void ApplyKeys()
    {
        if (_online is null)
        {
            return;
        }

        bool speak = _own.TalkLink != VoiceLink.All;
        _online.Voice.TalkKey = _talkKey || speak;
        _online.Voice.TeamKey = _teamKey && !speak;
    }

    public void ApplySettings(in VoiceSettingsBlock s)
    {
        _settings.Enabled = s.Enabled != 0;
        _settings.PushToTalk = s.PushToTalk != 0;
        _settings.MicMuted = s.MicMuted != 0;
        _settings.ActivationThreshold = float.IsFinite(s.ActivationThreshold) ? s.ActivationThreshold : VoiceK.ActivationThreshold;
        _settings.ActivationHoldMs = s.ActivationHoldMs;
        _settings.InputGain = float.IsFinite(s.InputGain) ? s.InputGain : 1f;
        _settings.OutputVolume = float.IsFinite(s.OutputVolume) ? s.OutputVolume : 1f;
        _settings.JitterFrames = s.JitterFrames;
        _settings.NoiseSuppression = s.NoiseSuppression != 0;
        _settings.NoiseSuppressionDecibels = s.NoiseSuppressionDecibels;
        _settings.AutoGain = s.AutoGain != 0;
        _settings.EchoCancellation = s.EchoCancellation != 0;
    }

    public VoiceSettingsBlock ReadSettings() => new()
    {
        Enabled = _settings.Enabled ? 1 : 0,
        PushToTalk = _settings.PushToTalk ? 1 : 0,
        MicMuted = _settings.MicMuted ? 1 : 0,
        ActivationThreshold = _settings.ActivationThreshold,
        ActivationHoldMs = _settings.ActivationHoldMs,
        InputGain = _settings.InputGain,
        OutputVolume = _settings.OutputVolume,
        JitterFrames = _settings.JitterFrames,
        NoiseSuppression = _settings.NoiseSuppression ? 1 : 0,
        NoiseSuppressionDecibels = _settings.NoiseSuppressionDecibels,
        AutoGain = _settings.AutoGain ? 1 : 0,
        EchoCancellation = _settings.EchoCancellation ? 1 : 0,
    };

    /// <summary>Copies every voice option from one settings object to another.</summary>
    internal static void Copy(VoiceSettings from, VoiceSettings into)
    {
        into.Enabled = from.Enabled;
        into.PushToTalk = from.PushToTalk;
        into.MicMuted = from.MicMuted;
        into.ActivationThreshold = from.ActivationThreshold;
        into.ActivationHoldMs = from.ActivationHoldMs;
        into.InputGain = from.InputGain;
        into.OutputVolume = from.OutputVolume;
        into.JitterFrames = from.JitterFrames;
        into.NoiseSuppression = from.NoiseSuppression;
        into.NoiseSuppressionDecibels = from.NoiseSuppressionDecibels;
        into.AutoGain = from.AutoGain;
        into.EchoCancellation = from.EchoCancellation;
    }

    // ---- what Unreal reads --------------------------------------------------------------------------------------

    public VoiceStatus ReadState()
    {
        // A finished echo test still shows its counters until it is stopped or a visit starts.
        VoiceClient? client = Capturing ?? (_online is null ? _echo?.Client : null);
        var status = new VoiceStatus
        {
            EchoPhase = (int)(_echo?.Phase ?? VoiceEchoPhase.Off),
            Enabled = _settings.Enabled ? 1 : 0,
            MicMuted = _settings.MicMuted ? 1 : 0,
            AutoGain = 1f,
        };
        if (client is null)
        {
            return status;
        }

        status.Connected = client.IsConnected ? 1 : 0;
        status.Transmitting = client.Transmitting ? 1 : 0;
        status.Channel = (int)client.TransmitChannel;
        status.LocalTalker = client.LocalTalker;
        status.TalkerCount = ListenerOf()?.Talkers.Count ?? 0;
        status.InputLevel = client.InputLevel;
        status.AutoGain = client.AutoGain;
        status.FramesCaptured = client.Stats.FramesCaptured;
        status.FramesSent = client.Stats.FramesSent;
        status.PacketsReceived = ListenerOf()?.Stats.PacketsReceived ?? 0;
        return status;
    }

    /// <summary>Fills <paramref name="into"/> with the remote talkers heard so far; returns how many there are.</summary>
    public unsafe int ReadTalkers(Span<VoiceTalkerInfo> into)
    {
        if (_echo is { } echo && (echo.Running || _online is null))
        {
            if (echo.Heard is not { } heard)
            {
                return 0;
            }

            if (into.Length > 0)
            {
                Write(ref into[0], heard, "You (microphone test)", -1);
            }

            return 1;
        }

        if (_online is not { } online)
        {
            return 0;
        }

        FillTeams();
        SPlayerController? local = VoiceLinks.LocalController(_visit);
        IReadOnlyList<VoiceTalker> talkers = online.Voice.Talkers;
        for (int i = 0; i < talkers.Count && i < into.Length; i++)
        {
            VoiceTalker t = talkers[i];
            string name = online.NameOf(t.Id) ?? "Player " + t.Id.ToString(CultureInfo.InvariantCulture);
            int team = _teams.GetValueOrDefault(t.Id, -1);
            Write(ref into[i], t, name, team);
            into[i].Link = (int)VoiceLinks.Of(local, t.Id, t.Channel, team);
        }

        return talkers.Count;
    }

    private static unsafe void Write(ref VoiceTalkerInfo row, VoiceTalker t, string name, int team)
    {
        row.Talker = t.Id;
        row.Team = team;
        row.Speaking = t.Speaking ? 1 : 0;
        row.Channel = (int)t.Channel;
        row.Muted = t.Muted ? 1 : 0;
        row.Volume = t.Volume;
        row.Level = t.Level;
        row.QueuedFrames = t.Buffer.Count;
        row.FramesDecoded = t.FramesDecoded;
        row.DecodeErrors = t.DecodeErrors;
        row.Link = (int)(t.Channel == EVoiceChannel.Team ? VoiceLink.Team : VoiceLink.All);
        fixed (char* text = row.Name)
        {
            OnlineTextCopy.Fixed(text, VoiceTalkerInfo.NameCapacity, name);
        }
    }

    /// <summary>The team of each talker on the current map: lobby players in the lobby, PlayerReplicationInfos in a round.</summary>
    private void FillTeams()
    {
        _teams.Clear();
        if (_visit is not { } client)
        {
            return;
        }

        if (client.CurrentMap is null || MapCatalog.IsMenuMap(client.CurrentMap))
        {
            foreach (LobbyPlayer p in client.Lobby.Players)
            {
                if (p.Id is > 0 and <= ushort.MaxValue)
                {
                    _teams[(ushort)p.Id] = p.Team;
                }
            }

            return;
        }

        if (client.Session is not { } session)
        {
            return;
        }

        foreach (PlayerReplicationInfo pri in session.World.Level.AllActors<PlayerReplicationInfo>())
        {
            if (!pri.bDeleteMe && VoiceK.TalkerOf(pri.PlayerID) is > 0 and var talker)
            {
                _teams[talker] = pri.Team?.TeamIndex ?? -1;
            }
        }
    }

    /// <summary>
    /// The next 20 ms of <paramref name="talker"/> at 48 kHz (960 samples) at the player's volume for him; the output
    /// volume is the voice submix's. False when there is nothing to play (the destination is left alone). While the
    /// echo test runs, talker <see cref="VoiceEchoTest.Talker"/> is the test coming back.
    /// </summary>
    public bool ReadFrame(int talker, Span<float> destination)
    {
        if (destination.Length < VoiceK.FrameSamples || talker is <= 0 or > ushort.MaxValue)
        {
            return false;
        }

        if (_echo is { } echo && (echo.Running || _online is null))
        {
            return talker == VoiceEchoTest.Talker && echo.ReadFrame(destination);
        }

        return _online?.Voice.FindTalker((ushort)talker) is { } t && t.ReadFrame(destination);
    }

    /// <summary>
    /// Mutes or unmutes a talker for the local player. A named talker is muted by name (it holds on the next maps, as
    /// the Godot lobby and overlay do); one not named yet only on this map.
    /// </summary>
    public void SetMuted(int talker, bool muted)
    {
        if (_online is not { } online || talker is <= 0 or > ushort.MaxValue)
        {
            return;
        }

        if (online.NameOf((ushort)talker) is { } name)
        {
            SetPlayerMuted(name, muted);
        }
        else
        {
            online.Voice.SetMuted((ushort)talker, muted);
        }
    }

    /// <summary>Sets the local player's volume for a talker, 0 to 2 (by name when he is named, as for the mute).</summary>
    public void SetVolume(int talker, float volume)
    {
        if (_online is not { } online || talker is <= 0 or > ushort.MaxValue || !float.IsFinite(volume))
        {
            return;
        }

        if (online.NameOf((ushort)talker) is { } name)
        {
            online.SetVolume(name, volume);
        }
        else
        {
            online.Voice.SetVolume((ushort)talker, volume);
        }
    }

    /// <summary>The client whose talkers Unreal hears: the echo test's listener, else the visit's; none for the meter.</summary>
    private VoiceClient? ListenerOf() => _online is null && _echo is { } echo ? echo.Listener : _online?.Voice;

    // ---- the lobby's "Restrict Mic to Team" ---------------------------------------------------------------------

    /// <summary>
    /// Sends the lobby's "Restrict Mic to Team" (SPlayerController.ServerSetTeamVoiceOnly through the session's
    /// "teamvoice" line); the server keeps it across the maps. False when there is no visit to send it on.
    /// </summary>
    public bool SetTeamVoiceOnly(bool only)
    {
        if (_visit is not { } client)
        {
            return false;
        }

        client.SetTeamVoiceOnly(only);
        return true;
    }

    /// <summary>The local player's "Restrict Mic to Team" as the server last sent it: -1 when not known (no visit, or no
    /// lobby row for him yet), else 0 or 1.</summary>
    public int TeamVoiceOnly => _visit?.Lobby.Local is { } local ? (local.TeamVoiceOnly ? 1 : 0) : -1;

    // ---- the Voice page's level meter ---------------------------------------------------------------------------

    /// <summary>
    /// The Voice page's microphone level outside a visit (Godot's OptionsMenu meter): while wanted, and neither a visit
    /// nor the echo test has the microphone, an unconnected voice client takes the microphone through the player's
    /// clean-up and gain, so <c>GetState</c>'s input level is what the gate would see. It sends nothing.
    /// </summary>
    public void SetMeter(bool on)
    {
        _meterWanted = on;
        if (!on)
        {
            StopMeter();
        }
    }

    private void UpdateMeter()
    {
        if (!_meterWanted || _online is not null || _echo is { Running: true })
        {
            StopMeter();
            return;
        }

        // The player's clean-up and gain; never muted or gated off, and no voices to cancel.
        Copy(_settings, _meterSettings);
        _meterSettings.Enabled = true;
        _meterSettings.MicMuted = false;
        _meterSettings.PushToTalk = false;
        _meterSettings.EchoCancellation = false;
        if (_meter is null)
        {
            _meter = new VoiceClient(_meterSettings, UnrealBridge.Online.OnlineBridge.Instance.Clock);
            _meter.SetCaptureRate(_captureRate);
        }

        _meter.Update();
    }

    private void StopMeter()
    {
        _meter?.Dispose();
        _meter = null;
    }

    // ---- the echo test ------------------------------------------------------------------------------------------

    /// <summary>
    /// Starts the microphone test: <paramref name="recordSeconds"/> (1 to 10) recorded, then played back through
    /// <see cref="ReadFrame"/>; 0 for live loopback. False while a visit is up (the visit owns the microphone; Godot
    /// says "Leave the session to test the microphone").
    /// </summary>
    public bool StartEchoTest(int recordSeconds)
    {
        if (_online is not null)
        {
            return false;
        }

        StopEchoTest();
        _echo = new VoiceEchoTest(_settings, _captureRate, recordSeconds);
        return true;
    }

    public void StopEchoTest()
    {
        _echo?.Dispose();
        _echo = null;
    }

    /// <summary>Once a frame: the echo test moves its datagrams and records what came back; the level meter runs.</summary>
    public void Update()
    {
        _echo?.Update(_settings);
        UpdateMeter();
    }

    public void Dispose()
    {
        StopEchoTest();
        StopMeter();
        EndVisit();
        if (ReferenceEquals(s_instance, this))
        {
            s_instance = null;
        }
    }
}
