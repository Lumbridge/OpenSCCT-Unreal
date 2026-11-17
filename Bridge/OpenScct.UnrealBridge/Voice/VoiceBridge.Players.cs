using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Net.Voice;
using OpenScct.Server;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Voice;

/// <summary>
/// The local player's choices for each other player, by name, as the Godot lobby and speaker overlay make them
/// (OnlineLobby.VoiceVolume / SetVoiceMuted, OnlineVoiceChat.HandleSpeakerInput): a volume from 0 to 2 kept in a file
/// across runs (PlayerVoiceVolumeStore), and a mute kept for the rest of the run, across visits. Also what the lobby
/// screen reads per player (speaking, level) and the host's voice ban.
/// </summary>
public sealed partial class VoiceBridge
{
    private readonly HashSet<string> _mutedNames = new(StringComparer.OrdinalIgnoreCase);
    private string? _volumeFile;

    /// <summary>The file the per-player volumes are kept in, or null while they are not saved.</summary>
    public string? VolumeFile => _volumeFile;

    /// <summary>
    /// Keeps the per-player volumes in <paramref name="path"/> (JSON of name to volume, as Godot's voice-volumes.json):
    /// loads what it holds now and writes it again whenever a volume changes. A file that cannot be read starts every
    /// player at full volume (the failure is kept in <see cref="LastError"/>). Called before the first visit; a visit
    /// already up goes on with the volumes it had until the next.
    /// </summary>
    public void SetVolumeFile(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        VoiceVolumes volumes;
        try
        {
            volumes = VoiceVolumes.Load(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            LastError = $"Could not load {path}: {e.Message}";
            volumes = new VoiceVolumes();
        }

        volumes.Changed += () =>
        {
            try
            {
                volumes.Save(path);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                LastError = $"Could not save {path}: {e.Message}";
            }
        };
        _volumes = volumes;
        _volumeFile = path;
    }

    /// <summary>The local player's volume for <paramref name="name"/>'s voice, 0 to 2 (1 unless set).</summary>
    public float PlayerVolume(string name) => _volumes.VolumeOf(name);

    /// <summary>
    /// Sets the local player's volume for <paramref name="name"/>, 0 to 2, at once on the current map when he is on it,
    /// and in the saved volumes either way.
    /// </summary>
    public void SetPlayerVolume(string name, float volume)
    {
        if (string.IsNullOrWhiteSpace(name) || !float.IsFinite(volume))
        {
            return;
        }

        if (_online is { } online)
        {
            online.SetVolume(name, volume);
        }
        else
        {
            _volumes.SetVolume(name, volume);
        }
    }

    public bool IsPlayerMuted(string name) => _mutedNames.Contains(name ?? string.Empty);

    /// <summary>Mutes or unmutes <paramref name="name"/> for the local player; the server stops sending him while muted.</summary>
    public void SetPlayerMuted(string name, bool muted)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        if (muted)
        {
            _mutedNames.Add(name);
        }
        else
        {
            _mutedNames.Remove(name);
        }

        _online?.SetMuted(name, muted);
    }

    /// <summary>
    /// What the lobby shows for one player (Godot's LobbyRoomScreen row): speaking (for the local player, transmitting),
    /// the level of what is heard from him, and the local player's volume and mute for him.
    /// </summary>
    public VoicePlayerInfo ReadPlayer(string name)
    {
        var info = new VoicePlayerInfo
        {
            Visit = _online is null ? 0 : 1,
            Volume = PlayerVolume(name),
            Muted = IsPlayerMuted(name) ? 1 : 0,
        };
        if (_online is not { } online || string.IsNullOrEmpty(name))
        {
            return info;
        }

        if (online.TalkerOf(name) is { } talker)
        {
            info.Talker = talker;
            if (talker == online.Voice.LocalTalker)
            {
                info.Local = 1;
                info.Speaking = online.Voice.Transmitting ? 1 : 0;
                info.Level = online.Voice.Transmitting ? online.Voice.InputLevel : 0f;
            }
            else if (online.Voice.FindTalker(talker) is { } heard)
            {
                info.Speaking = heard.Speaking ? 1 : 0;
                info.Level = heard.Level;
            }
        }

        return info;
    }

    /// <summary>Where the visit is, for the overlay: a round or the lobby, and the local player's team.</summary>
    public VoiceContext ReadContext()
    {
        var context = new VoiceContext { LocalTeam = -1 };
        if (_visit is not { } client)
        {
            return context;
        }

        context.Visit = 1;
        context.LocalTalker = _online?.Voice.LocalTalker ?? 0;
        bool lobby = client.CurrentMap is null || MapCatalog.IsMenuMap(client.CurrentMap);
        context.InRound = lobby ? 0 : 1;
        if (lobby)
        {
            context.LocalTeam = client.Lobby.Local?.Team ?? -1;
        }
        else if (client.Session?.LocalPlayer?.PlayerReplicationInfo is { } own)
        {
            context.LocalTeam = own.Team?.TeamIndex ?? -1;
        }

        return context;
    }

    /// <summary>
    /// The host's Voice Ban (OnlineClient.SetVoiceBanned): the server stops relaying the player's voice and shows him
    /// as voice banned. The server refuses it from anyone but the host or an admin. False when there is no visit.
    /// </summary>
    public bool SetVoiceBanned(int playerId, bool banned)
    {
        if (_visit is not { } client || playerId <= 0)
        {
            return false;
        }

        client.SetVoiceBanned(playerId, banned);
        return true;
    }
}
