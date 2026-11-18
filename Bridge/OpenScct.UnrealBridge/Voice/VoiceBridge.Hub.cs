using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using OpenScct.Core;
using OpenScct.Net.Voice;
using OpenScct.Server.Hub;

namespace OpenScct.UnrealBridge.Voice;

/// <summary>
/// Voice chat in the hub (the Godot client's game/Scripts/Hub/HubVoiceChat.cs): while the hub bridge has a visit, one
/// <see cref="VoiceClient"/> on the visit's voice transport and OpenSCCT's <see cref="HubVoice"/>, which plays each
/// talker by the distance from the listener's ear to his pawn, muffled through walls. The proximity gain and the
/// low-pass are already in the samples <see cref="ReadFrame"/> hands out (VoiceTalker.Spatial), so Unreal plays hub
/// talkers exactly as it plays those of an online visit. The microphone, the keys, the options, the talker list and the
/// state go through the same entries; an online visit takes over when one starts (the hub's connection is closed for a
/// pad's match anyway).
/// <para>The per-player volume and mute of the hub's player list are Unreal's (UScctHubSubsystem) and are kept apart
/// from the online visit's saved volumes: they arrive through <see cref="SetVolume"/> and <see cref="SetMuted"/> by
/// talker id.</para>
/// </summary>
public sealed partial class VoiceBridge
{
    private HubClient? _hubClient;
    private HubVoice? _hub;
    private VoiceVolumes _hubVolumes = new();
    private FVector? _hubEar;
    private long _hubEarStamp;
    private long _hubStamp;

    /// <summary>The hub visit's voice, or null when not in a hub.</summary>
    public HubVoice? Hub => _hub;

    /// <summary>The listener's ear in the hub (the camera, UE units); the local pawn's view point when none came lately.</summary>
    public void SetHubEar(FVector ear)
    {
        _hubEar = ear;
        _hubEarStamp = Stopwatch.GetTimestamp();
    }

    /// <summary>
    /// Once a frame after the hub bridge ticked its visit: the hub's voice connects once the session knows the server,
    /// follows the names and updates every talker's proximity mix. A new visit gets a new voice client. A failure is
    /// kept in <see cref="LastError"/>; it never stops the visit.
    /// </summary>
    public void AfterHubTick(HubClient client)
    {
        ArgumentNullException.ThrowIfNull(client);
        try
        {
            if (_online is not null || client.Voice is null)
            {
                EndHub();
                return;
            }

            long now = Stopwatch.GetTimestamp();
            if (_hub is null || !ReferenceEquals(_hubClient, client))
            {
                EndHub();
                StopEchoTest();
                var voice = new VoiceClient(_settings, UnrealBridge.Online.OnlineBridge.Instance.Clock) { UseVoiceThread = true };
                voice.SetCaptureRate(_captureRate);
                _hub = new HubVoice(client, voice, volumes: _hubVolumes);
                _hubClient = client;
                _hubStamp = now;
            }

            float dt = (float)Math.Clamp((now - _hubStamp) / (double)Stopwatch.Frequency, 0.0, 0.25);
            _hubStamp = now;
            _hub.Voice.TalkKey = _talkKey;
            _hub.Voice.TeamKey = _teamKey;
            // An ear older than half a second is stale (the hub's HUD stopped asking): the pawn's view point then.
            FVector? ear = _hubEar is { } e && (now - _hubEarStamp) < Stopwatch.Frequency / 2 ? e : null;
            _hub.Update(ear, dt);
        }
        catch (Exception e)
        {
            LastError = e.ToString();
        }
    }

    /// <summary>The hub visit ended (or an online visit took over): its voice client lets go of the transport.</summary>
    public void EndHub()
    {
        _hub?.Dispose();
        _hub = null;
        _hubClient = null;
        _hubEar = null;
    }

    private unsafe int ReadHubTalkers(HubVoice hub, Span<VoiceTalkerInfo> into)
    {
        IReadOnlyList<VoiceTalker> talkers = hub.Voice.Talkers;
        for (int i = 0; i < talkers.Count && i < into.Length; i++)
        {
            VoiceTalker t = talkers[i];
            Write(ref into[i], t, hub.NameOf(t.Id) ?? "Player " + t.Id.ToString(CultureInfo.InvariantCulture), -1);
        }

        return talkers.Count;
    }

    private bool SetHubMuted(int talker, bool muted)
    {
        if (_hub is not { } hub)
        {
            return false;
        }

        if (hub.NameOf((ushort)talker) is { } name)
        {
            if (hub.IsMuted(name) != muted)
            {
                hub.SetMuted(name, muted);
            }
        }
        else
        {
            hub.Voice.SetMuted((ushort)talker, muted);
        }

        return true;
    }

    private bool SetHubVolume(int talker, float volume)
    {
        if (_hub is not { } hub)
        {
            return false;
        }

        if (hub.NameOf((ushort)talker) is { } name)
        {
            if (Math.Abs(hub.VolumeOf(name) - volume) > 0.001f)
            {
                hub.SetVolume(name, volume);
            }
        }
        else
        {
            hub.Voice.SetVolume((ushort)talker, volume);
        }

        return true;
    }
}
