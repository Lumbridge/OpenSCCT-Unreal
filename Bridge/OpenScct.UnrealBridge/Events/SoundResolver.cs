using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// Turns the simulation's sound events into records Unreal can play as they are. It does what the Godot client does
/// between a <see cref="SimEvent"/> and a playing voice:
/// <list type="bullet">
/// <item>PlayAudioRouter (game/Scripts/Play/PlayAudioRouter.cs): the client's own copy of a call and the server's
/// copy (ClientHearSound) play once.</item>
/// <item>SoundSystem.Handle, PlaySound, Start, Resolve and NextInSequence (game/Scripts/Audio/SoundSystem.cs) with
/// SoundRules and AudioK: the hearing reach, random and switch lists down to a file, sequences, and the volume,
/// radius, minimum distance and pitch of the source.</item>
/// </list>
/// Left to Unreal: the voices themselves (the original mixes 32 and steals by slot and priority), the distance gain,
/// occlusion, Doppler and reverb.
/// </summary>
internal sealed class SoundResolver
{
    // The numbers below are the original's (game/Scripts/Audio/AudioK.cs has the evidence for each).

    /// <summary>Radius a hearer gets for a call with radius 0.</summary>
    private const float DefaultHearRadius = 80f;

    /// <summary>A sound's reach is this many times its radius.</summary>
    private const float RangeFactor = 100f;

    private const float MinVolume = 1e-5f;
    private const float MinPitch = 0.5f;
    private const float MaxPitch = 2f;

    /// <summary>Minimum distance of a source whose radius is 0, and the smallest one handed to the mixer.</summary>
    private const float ZeroRadiusMinDistance = 10f;
    private const float MinMinDistance = 0.001f;

    /// <summary>Random list weights are out of this.</summary>
    private const int RandomTotal = 10000;

    /// <summary>A switch sound with no pending SetSwitch traces from this far above its place to this far below.</summary>
    private const float SwitchTraceUp = 15f;
    private const float SwitchTraceDown = 150f;

    /// <summary>Sequence entry value bit: loop the entry for (value &amp; 0x1fff) times its duration.</summary>
    private const int SequenceTimedLoop = 0x2000;

    /// <summary>Music plays at this radius (it is 2D, so it only matters for priority).</summary>
    private const float MusicRadius = 1000f;

    private const int FlagLoop = 0x0002;

    /// <summary>Two copies of a sound on one actor within this many seconds are one call (PlayAudioRouter.DuplicateSeconds).</summary>
    private const float DuplicateSeconds = 0.2f;

    /// <summary>The most records one sequence call may give.</summary>
    private const int MaxSegments = 64;

    private readonly SoundLibrary _library;
    private readonly PlaySession _session;
    private readonly DrainQueue<BridgeSound> _out;
    private readonly Random _random = new(1);
    private readonly Dictionary<(int Actor, Name Sound), (long Tick, bool Heard)> _last = [];
    private readonly List<(int Actor, Name Sound)> _stale = [];
    private readonly List<LoopCall> _loops = [];

    // What may still be playing, by actor and sound (the call's and the leaf's), with the tick it ends: a StopSound
    // for anything else stops nothing and is not passed on.
    private readonly Dictionary<(int Actor, int Sound), long> _mayPlay = [];
    private readonly List<(int Actor, int Sound)> _ended = [];
    private long _prunedAt;
    private int _pendingSwitch = -1;
    private int _callId;

    public SoundResolver(SoundLibrary library, PlaySession session, DrainQueue<BridgeSound> output)
    {
        _library = library;
        _session = session;
        _out = output;
    }

    /// <summary>PlaySound calls that started at least one file.</summary>
    public long Started { get; private set; }

    /// <summary>Calls that played nothing: an empty random or switch entry, a "Remove" entry, a stream the bank lacks.</summary>
    public long Silent { get; private set; }

    /// <summary>Calls dropped as the second copy of a call that already played.</summary>
    public long Duplicates { get; private set; }

    /// <summary>Calls dropped because the viewed actor is beyond 100 x the radius.</summary>
    public long OutOfReach { get; private set; }

    /// <summary>Sounds the events named that the library does not have (the newest few).</summary>
    public List<string> Unknown { get; } = [];

    public long UnknownCount { get; private set; }

    /// <summary>StopSound events passed on as Stop records.</summary>
    public long Stops { get; private set; }

    /// <summary>
    /// StopSound events for a sound no call of this session can still be playing on that actor. The network and sticky
    /// cameras stop their turn and zoom sounds every tick while idle (about 1,200 events a second on AquaE).
    /// </summary>
    public long IdleStops { get; private set; }

    private static long WindowTicks => (long)Math.Ceiling(DuplicateSeconds * Sim.Hz);

    /// <summary>The level's Song, which the engine starts with no fade when the map loads (SoundSystem.Bind).</summary>
    public void PlayLevelSong(long tick)
    {
        if (_library.LevelSong is { } song)
        {
            PlayMusic(tick, song, 0f);
        }
    }

    /// <summary>
    /// Handles one sound event presented with <paramref name="tick"/>. Returns false for a PlaySound that was
    /// dropped (a duplicate copy, or out of reach).
    /// </summary>
    public bool Handle(long tick, in SimEvent e)
    {
        switch (e.Kind)
        {
            case SimEventKind.PlaySound:
                if (!Admit(tick, e))
                {
                    Duplicates++;
                    return false;
                }

                if (!InHearingReach(e))
                {
                    OutOfReach++;
                    return false;
                }

                PlaySound(tick, e);
                return true;
            case SimEventKind.StopSound:
                return StopSound(tick, e);
            case SimEventKind.PlayMusic:
                PlayMusic(tick, e.What.ToString(), e.Radius);
                return true;
            case SimEventKind.StopMusic:
                _out.Add(new BridgeSound { Op = BridgeSoundOp.StopMusic, Tick = (int)tick, FadeSeconds = e.Radius, Flags = BridgeSoundFlags.Music });
                return true;
            case SimEventKind.SetSwitch:
                _pendingSwitch = e.Slot;
                return true;
            default:
                return true;
        }
    }

    /// <summary>
    /// After each tick: a looping call whose actor is gone is stopped (SoundSystem.UpdateVoice stops a looping voice
    /// that can no longer find the actor it follows), and the duplicate table is pruned.
    /// </summary>
    public void AfterTick(long tick, Level level)
    {
        for (int i = _loops.Count - 1; i >= 0; i--)
        {
            LoopCall loop = _loops[i];
            if (level.FindActor(loop.ActorId) is { bDeleteMe: false })
            {
                continue;
            }

            _out.Add(new BridgeSound { Op = BridgeSoundOp.StopCall, Tick = (int)tick, CallId = loop.CallId, ActorId = loop.ActorId, SoundId = loop.SoundId });
            _loops.RemoveAt(i);
        }

        if (tick - _prunedAt < Sim.Hz)
        {
            return;
        }

        _prunedAt = tick;
        _stale.Clear();
        foreach (KeyValuePair<(int Actor, Name Sound), (long Tick, bool Heard)> kv in _last)
        {
            if (tick - kv.Value.Tick > WindowTicks)
            {
                _stale.Add(kv.Key);
            }
        }

        foreach ((int Actor, Name Sound) k in _stale)
        {
            _last.Remove(k);
        }

        _ended.Clear();
        foreach (KeyValuePair<(int Actor, int Sound), long> kv in _mayPlay)
        {
            if (kv.Value < tick)
            {
                _ended.Add(kv.Key);
            }
        }

        foreach ((int Actor, int Sound) k in _ended)
        {
            _mayPlay.Remove(k);
        }
    }

    /// <summary>PlayAudioRouter.Admit: false when <paramref name="e"/> is the other machine's copy of a call that already played.</summary>
    private bool Admit(long tick, in SimEvent e)
    {
        if (e.ActorId == 0 || e.What.IsNone)
        {
            return true;
        }

        (int ActorId, Name What) key = (e.ActorId, e.What);
        if (_last.TryGetValue(key, out (long Tick, bool Heard) last) && last.Heard != e.Heard && tick - last.Tick <= WindowTicks)
        {
            // The pair is complete: the next call of either side plays again.
            _last.Remove(key);
            return false;
        }

        _last[key] = (tick, e.Heard);
        return true;
    }

    /// <summary>
    /// SoundSystem.InHearingReach (APlayerController::HearSound): an attenuated call this machine's own simulation
    /// made plays only when the actor the player views is nearer than 100 x its radius. The server tested its own
    /// calls before sending them. Not ported: the radius shrinking by 0.8 ^ (ZoneDist ^ 2) when something is in the
    /// way (it needs the occlusion trace and the map's zone distance table).
    /// </summary>
    private bool InHearingReach(in SimEvent e)
    {
        if (e.Heard || e.NoAttenuate || e.ActorId == 0 || e.Radius == 0f ||
            _session.LocalController is not { } pc || (pc.ViewTarget ?? pc.Pawn) is not { bDeleteMe: false } viewed)
        {
            return true;
        }

        float reach = RangeFactor * e.Radius;
        return FVector.DistSquared(viewed.Location, e.Location) < reach * reach;
    }

    /// <summary>SoundSystem.PlaySound and Start (rules 9 to 14) for one call.</summary>
    private void PlaySound(long tick, in SimEvent e)
    {
        string sound = e.What.ToString();
        if (_library.Find(sound) is not { } def)
        {
            NoteUnknown(sound);
            return;
        }

        float chain = 1f;
        SoundDef? leaf = Resolve(def, e.Location, 0, ref chain);
        if (leaf is null)
        {
            Silent++;
            return;
        }

        // Native PlaySound replaces a pawn this machine controls with the viewport controller, clears the bone and
        // starts the sound at the listener, where it stays. A spectated pawn is not locally controlled.
        SPlayerController? local = _session.LocalController;
        bool atListener = e.ActorId != 0 && e.ActorId == (local?.Pawn?.ActorId ?? 0);
        FVector location = atListener && _session.View.Valid ? _session.View.Location : e.Location;

        var call = new Call
        {
            Tick = (int)tick,
            CallId = ++_callId,
            ActorId = e.ActorId,
            FollowActorId = atListener ? 0 : e.ActorId,
            FollowBoneId = atListener ? 0 : EventNames.Id(e.Bone),
            SoundId = def.PathId,
            Slot = Math.Clamp(e.Slot, 0, 7),
            Location = location,
            Radius = e.Radius == 0f ? DefaultHearRadius : e.Radius,
            Pitch = Math.Clamp(e.Pitch, MinPitch, MaxPitch),
            Flags = (atListener ? BridgeSoundFlags.AtListener : 0) | (e.NoOverride ? BridgeSoundFlags.NoOverride : 0) |
                (e.Heard ? BridgeSoundFlags.Heard : 0) | (IsObjectiveJingle(def.Path) ? BridgeSoundFlags.Objective : 0),
            Flat = e.Flat,
        };

        int segments;
        if (leaf.Kind == "sequence")
        {
            // The sequence's own gain applies to every entry (rule 10 runs before the list dispatch).
            segments = PlaySequence(ref call, leaf, e.Volume * chain * leaf.Volume);
        }
        else
        {
            bool loop = (leaf.Flags & FlagLoop) != 0 || leaf.Loop;
            segments = Emit(ref call, leaf, e.Volume * chain, loop, 0f, 0f) ? 1 : 0;
            if (segments > 0 && loop && call.FollowActorId != 0 && _loops.Count < 256)
            {
                _loops.Add(new LoopCall(call.CallId, call.ActorId, call.SoundId, leaf.PathId));
            }
        }

        if (segments > 0)
        {
            Started++;
        }
        else
        {
            Silent++;
        }
    }

    /// <summary>
    /// Rule 14, SoundSystem.NextInSequence laid out in time: the entries play one after another on one voice, an
    /// entry <c>value</c> times, or looping for (value &amp; 0x1fff) times its length when bit 0x2000 is set. Each
    /// play of an entry picks its random and switch lists again. Returns the records written.
    /// </summary>
    private int PlaySequence(ref Call call, SoundDef sequence, float baseVolume)
    {
        int written = 0;
        float delay = 0f;
        foreach (SoundEntry entry in sequence.Entries)
        {
            bool timed = (entry.Value & SequenceTimedLoop) != 0;
            int plays = timed ? 1 : Math.Max(entry.Value, 1);
            for (int i = 0; i < plays && written < MaxSegments; i++)
            {
                float chain = 1f;
                SoundDef? leaf = _library.Find(entry.Sound) is { } def ? Resolve(def, call.Location, 1, ref chain) : null;

                // A missing entry, silence or a nested sequence ends this entry (the original moves to the next one).
                if (leaf is null || leaf.Kind == "sequence" || leaf.FileId == 0)
                {
                    break;
                }

                float length = leaf.Duration / call.Pitch;
                float seconds = timed ? length * (entry.Value & (SequenceTimedLoop - 1)) : 0f;
                call.Segment = written;
                if (Emit(ref call, leaf, baseVolume * chain, timed, delay, seconds))
                {
                    written++;
                }

                delay += timed ? seconds : length;
            }
        }

        return written;
    }

    /// <summary>SoundSystem.PlayLeaf: one file with the source values of rules 10 and 11.</summary>
    private bool Emit(ref Call call, SoundDef leaf, float volume, bool loop, float delay, float playSeconds)
    {
        if (leaf.FileId == 0)
        {
            return false;
        }

        float radius = leaf.Radius > 0f ? leaf.Radius : call.Radius;
        bool positional = leaf.Positional && !call.Flat;
        _out.Add(new BridgeSound
        {
            Op = BridgeSoundOp.Play,
            Tick = call.Tick,
            CallId = call.CallId,
            Segment = call.Segment,
            ActorId = call.ActorId,
            FollowActorId = call.FollowActorId,
            FollowBoneId = call.FollowBoneId,
            SoundId = call.SoundId,
            LeafId = leaf.PathId,
            FileId = leaf.FileId,
            Slot = call.Slot,
            Flags = call.Flags | (positional ? BridgeSoundFlags.Positional : 0) | (loop ? BridgeSoundFlags.Loop : 0) |
                (leaf.IsStream ? BridgeSoundFlags.Stream : 0),
            X = call.Location.X,
            Y = call.Location.Y,
            Z = call.Location.Z,
            Volume = SourceVolume(volume * leaf.Volume),
            Radius = radius,
            MinDistance = radius == 0f ? ZeroRadiusMinDistance : Math.Max(radius, MinMinDistance),
            Pitch = call.Pitch,
            Delay = delay,
            PlaySeconds = playSeconds,
            Duration = leaf.Duration,
            LoopStart = loop ? leaf.LoopStart : -1,
            LoopEnd = loop ? leaf.LoopEnd : -1,
        });

        // Until when a StopSound can find this voice: for good when it loops, else its length (a second to spare; a
        // file of unknown length counts as a minute).
        if (call.ActorId != 0)
        {
            float seconds = playSeconds > 0f ? playSeconds : (leaf.Duration > 0f ? leaf.Duration : 60f) / call.Pitch;
            long ends = loop && playSeconds <= 0f ? long.MaxValue : call.Tick + (long)((delay + seconds + 1f) * Sim.Hz);
            Keep((call.ActorId, call.SoundId), ends);
            Keep((call.ActorId, leaf.PathId), ends);
        }

        return true;
    }

    /// <summary>
    /// Rules 12 and 13 (SoundSystem.Resolve): random and switch sounds down to a wave, stream or sequence; null for
    /// silence. Each list on the way multiplies <paramref name="gain"/> by its own Gain.
    /// </summary>
    internal SoundDef? Resolve(SoundDef def, FVector location, int depth, ref float gain)
    {
        if (depth > 8)
        {
            return null;
        }

        int pick;
        switch (def.Kind)
        {
            case "random":
            {
                // The weights are out of 10,000; a roll past their sum is silence.
                int roll = _random.Next(RandomTotal);
                int sum = 0;
                pick = -1;
                for (int i = 0; i < def.Entries.Length; i++)
                {
                    sum += def.Entries[i].Value;
                    if (roll < sum)
                    {
                        pick = i;
                        break;
                    }
                }

                break;
            }
            case "switch":
            {
                // The surface the last SetSwitch gave (a footstep sends its floor first), else a trace under the sound.
                int surface = _pendingSwitch;
                _pendingSwitch = -1;
                if (surface < 0)
                {
                    surface = SurfaceUnder(location);
                }

                surface = Math.Max(surface, 0);
                pick = -1;
                for (int i = 0; i < def.Entries.Length && pick < 0; i++)
                {
                    pick = def.Entries[i].Value == surface ? i : -1;
                }

                // No entry for the surface: the entry with value 0.
                for (int i = 0; i < def.Entries.Length && pick < 0; i++)
                {
                    pick = def.Entries[i].Value == 0 ? i : -1;
                }

                break;
            }
            default:
                return def;
        }

        gain *= def.Volume;
        return pick >= 0 && _library.Find(def.Entries[pick].Sound) is { } next ? Resolve(next, location, depth + 1, ref gain) : null;
    }

    /// <summary>LevelAudioWorld.SurfaceOf: the surface type of what lies under a sound, -1 for nothing.</summary>
    private int SurfaceUnder(FVector location)
    {
        Level level = _session.Client.World.Level;
        if (GadgetWorld.Collision(level.Info) is not { } collision)
        {
            return -1;
        }

        return collision.LineCheck(out FCheckResult hit, null, location - new FVector(0f, 0f, SwitchTraceDown),
            location + new FVector(0f, 0f, SwitchTraceUp), default, ETraceFlags.TRACE_World)
            ? hit.SurfaceType
            : -1;
    }

    private void Keep((int Actor, int Sound) key, long ends)
    {
        if (!_mayPlay.TryGetValue(key, out long known) || known < ends)
        {
            _mayPlay[key] = ends;
        }
    }

    /// <summary>
    /// SoundSystem.StopSound: the voices of an actor that play a sound; None and actor 0 are wildcards. Returns false
    /// (and passes nothing on) when the stop names one sound on one actor and no call can still be playing it.
    /// </summary>
    private bool StopSound(long tick, in SimEvent e)
    {
        int soundId = EventNames.Id(e.What);
        int localController = _session.LocalController?.ActorId ?? 0;

        // The local controller also owns its pawn's voices, which were started under the pawn's id: always passed on.
        if (e.ActorId != 0 && soundId != 0 && e.ActorId != localController)
        {
            if (!_mayPlay.Remove((e.ActorId, soundId), out long ends) || ends < tick)
            {
                IdleStops++;
                return false;
            }
        }

        Stops++;
        _out.Add(new BridgeSound
        {
            Op = BridgeSoundOp.Stop,
            Tick = (int)tick,
            ActorId = e.ActorId,
            SoundId = soundId,
            Flags = (soundId == 0 ? BridgeSoundFlags.AllSounds : 0) |
                (e.ActorId != 0 && e.ActorId == localController ? BridgeSoundFlags.LocalPawnVoices : 0),
        });

        for (int i = _loops.Count - 1; i >= 0; i--)
        {
            LoopCall loop = _loops[i];
            if ((e.ActorId == 0 || loop.ActorId == e.ActorId) && (soundId == 0 || loop.SoundId == soundId || loop.LeafId == soundId))
            {
                _loops.RemoveAt(i);
            }
        }

        return true;
    }

    /// <summary>Rule 24 (SoundSystem.PlayMusic): the one music track, 2D, at full volume without the Sound's gain.</summary>
    private void PlayMusic(long tick, string song, float fadeIn)
    {
        if (_library.Find(song) is not { } def)
        {
            NoteUnknown(song);
            return;
        }

        float chain = 1f;
        if (Resolve(def, default, 0, ref chain) is not { FileId: not 0 } leaf)
        {
            Silent++;
            return;
        }

        _out.Add(new BridgeSound
        {
            Op = BridgeSoundOp.PlayMusic,
            Tick = (int)tick,
            SoundId = def.PathId,
            LeafId = leaf.PathId,
            FileId = leaf.FileId,
            Flags = BridgeSoundFlags.Music | (def.Loop ? BridgeSoundFlags.Loop : 0) | (leaf.IsStream ? BridgeSoundFlags.Stream : 0),
            Volume = 1f,
            Radius = MusicRadius,
            MinDistance = MusicRadius,
            Pitch = 1f,
            Duration = leaf.Duration,
            FadeSeconds = fadeIn,
            LoopStart = -1,
            LoopEnd = -1,
        });
    }

    private void NoteUnknown(string sound)
    {
        UnknownCount++;
        if (Unknown.Count < 64 && !Unknown.Contains(sound))
        {
            Unknown.Add(sound);
        }
    }

    /// <summary>Rule 10: the source volume, clamped to the mixer's range.</summary>
    internal static float SourceVolume(float volume) => Math.Clamp(volume, MinVolume, 1f);

    /// <summary>SoundRules.IsObjectiveJingle: a Jingl sound of an Interface package's Objectifs group.</summary>
    private static bool IsObjectiveJingle(string path) =>
        path.StartsWith("Interface", StringComparison.OrdinalIgnoreCase) &&
        path.Contains(".Objectifs.", StringComparison.OrdinalIgnoreCase) &&
        path.Contains("Jingl", StringComparison.OrdinalIgnoreCase);

    private readonly record struct LoopCall(int CallId, int ActorId, int SoundId, int LeafId);

    /// <summary>What the records of one PlaySound call share.</summary>
    private struct Call
    {
        public int Tick, CallId, Segment, ActorId, FollowActorId, FollowBoneId, SoundId, Slot, Flags;
        public FVector Location;
        public float Radius, Pitch;
        public bool Flat;
    }
}
