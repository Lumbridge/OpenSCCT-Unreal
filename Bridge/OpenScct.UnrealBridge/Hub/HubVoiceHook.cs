using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Net.Voice;
using OpenScct.Net.Voice.Proximity;
using OpenScct.Server;
using OpenScct.Server.Hub;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// The proximity half of the hub's voice chat without its transport (an OpenSCCT addition; the playback side of
/// OpenSCCT's src/OpenScct.Server/Hub/HubVoice.cs and the Godot client's game/Scripts/Hub/HubVoiceChat.cs): for every
/// other player with a pawn in the drawn hub, the mix his voice would be played with as heard from the listener's ear
/// (the camera): the distance gain (full to 4 m, nothing past 30 m), muffled to 45 % with a 1200 Hz low-pass when the
/// world's collision is between his mouth and the ear (a line trace through the simulation's world, as HubVoice does:
/// the imported level carries no collision), smoothed so a talker walking behind a corner does not click. The
/// listener's own per-player volume and mute are Unreal's (UScctHubSubsystem) and are applied there.
/// <para>Unreal has no voice transport yet (no microphone capture, no Opus, no voice relay): nothing is played. The
/// mixes are what a transport would apply to each talker's samples.</para>
/// </summary>
public sealed class HubVoiceHook
{
    private readonly HubBridge _hub;
    private readonly ProximityListener _listener = new();
    private readonly List<(int PlayerId, ushort Talker, string Name, FVector Mouth)> _talkers = new();
    private readonly HashSet<ushort> _heard = new();
    private PhysicsLineOfSight? _lineOfSight;
    private MapInfo? _lineOfSightMap;
    private HubClient? _client;

    public HubVoiceHook(HubBridge hub) => _hub = hub ?? throw new ArgumentNullException(nameof(hub));

    /// <summary>The settings of the mixes (distances, occlusion gain and cut-off).</summary>
    public ProximityVoiceSettings Settings => _listener.Settings;

    /// <summary>
    /// Updates every talker's mix as heard from <paramref name="ear"/> over <paramref name="dt"/> seconds and writes them
    /// (nearest first). Returns how many talkers there are.
    /// </summary>
    public int Mixes(FVector ear, float dt, Span<HubVoiceMixRow> target)
    {
        HubClient? client = _hub.Client;
        if (!ReferenceEquals(client, _client))
        {
            _client = client;
            foreach (ushort talker in _heard)
            {
                _listener.Forget(talker);
            }

            _heard.Clear();
        }

        if (client is null || _hub.Round is null)
        {
            return 0;
        }

        _talkers.Clear();
        int me = client.LocalPlayer?.PlayerReplicationInfo?.PlayerID ?? -1;
        foreach (Pawn pawn in client.Session.World.Level.DynamicActors<Pawn>())
        {
            if (pawn.bDeleteMe || pawn.Health <= 0 || pawn.PlayerReplicationInfo is not { } pri || pri.PlayerID == me)
            {
                continue;
            }

            ushort talker = VoiceK.TalkerOf(pri.PlayerID);
            if (talker != 0)
            {
                _talkers.Add((pri.PlayerID, talker, pri.PlayerName, HubVoicePositions.EyeOf(pawn)));
            }
        }

        _talkers.Sort((a, b) => (a.Mouth - ear).SizeSquared().CompareTo((b.Mouth - ear).SizeSquared()));
        Func<FVector, FVector, bool> blocked = Blocked;
        for (int i = 0; i < _talkers.Count; i++)
        {
            (int playerId, ushort talker, string name, FVector mouth) = _talkers[i];
            _heard.Add(talker);
            float distance = (mouth - ear).Size();
            bool occluded = distance < Settings.HearingDistance && Blocked(ear, mouth);
            VoiceMix mix = _listener.Update(talker, ear, mouth, blocked, dt);
            if (i >= target.Length)
            {
                continue;
            }

            ref HubVoiceMixRow row = ref target[i];
            row.PlayerId = playerId;
            row.Flags = (occluded ? HubVoiceMixRow.Occluded : 0) | (mix.Audible ? HubVoiceMixRow.Audible : 0);
            row.Distance = distance;
            row.Gain = mix.Gain;
            // Out of hearing the mix eases towards VoiceMix.Silent (0 Hz): say the cut-off the wall would give instead,
            // so a muffle read from it means "behind a wall", not "out of reach".
            row.LowPassHz = mix.Gain >= 0.01f ? mix.LowPassHz : occluded ? Settings.OccludedLowPassHz : Settings.OpenLowPassHz;
            row.X = mouth.X;
            row.Y = mouth.Y;
            row.Z = mouth.Z;
            unsafe
            {
                fixed (HubVoiceMixRow* r = &row)
                {
                    OnlineTextCopy.Fixed(r->Name, HubVoiceMixRow.NameCapacity, name);
                }
            }
        }

        return _talkers.Count;
    }

    /// <summary>The world's collision is between the two points (HubVoice.Blocked).</summary>
    private bool Blocked(FVector from, FVector to)
    {
        MapInfo? map = _hub.Client?.Session.Map;
        if (!ReferenceEquals(map, _lineOfSightMap))
        {
            _lineOfSightMap = map;
            _lineOfSight = map?.Physics is { } physics ? new PhysicsLineOfSight(physics) : null;
        }

        return _lineOfSight is not null && _lineOfSight.IsBlockedByWorld(from, to);
    }
}
