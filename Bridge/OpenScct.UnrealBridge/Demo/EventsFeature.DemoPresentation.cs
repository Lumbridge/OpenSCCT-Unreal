using System;
using System.Collections.Generic;
using System.IO;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Server;
using OpenScct.UnrealBridge.Demo;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge.Events;

public sealed partial class EventsFeature
{
    // Voices are resolved by the usual sound router with its switches, sequence delays and source ownership.
    // Keep them silent until the target tick. An explicit seek owns a fresh session and a fresh timeline.
    private sealed class SeekVoice(BridgeSound sound)
    {
        public BridgeSound Sound = sound;
        public double FadeStart, FadeEnd;
    }

    private readonly List<SeekVoice> _seekVoices = [];
    private readonly List<(string Id, long Tick)> _seekTriggers = [];
    private bool _seekInProgress, _readSeekTick;
    private bool ReadingCheckpointBootstrap => !_readSeekTick && DemoBridge.Player is { UsingCheckpoint: true };

    private void AdvanceSeekTick(long tick)
    {
        _seekInProgress = true;
        if (ReadingCheckpointBootstrap && DemoBridge.Player is { } player)
        {
            var client = _session.Client;
            if (!client.HasDemoCheckpointPresentation)
                throw new InvalidDataException("Checkpoint has no presentation history.");
            var history = new List<DemoPresentationSample>();
            client.TakeDemoCheckpointPresentation(history);

            // The recorder includes the original map song at its start tick.
            Span<BridgeSound> discard = stackalloc BridgeSound[16];
            while (_sounds.Drain(discard) == discard.Length) { }
            foreach (var sample in history)
            {
                long age = (long)Math.Round(((long)player.CurrentTick - sample.Tick) * (double)Sim.Hz / player.Header.TickRate);
                if (sample.IsLiveEmitter) _effects.RestoreFirstTick(sample.Event.ActorId, tick - age);
                else TakeSeekEvent(tick - age, sample.Event, sample.MissingSource);
            }
        }

        _readSeekTick = true;
        ConsumeSounds(false);
        PruneVoices(tick);
        _effects.CaptureSeekTick();
        Span<BridgeShot> shots = stackalloc BridgeShot[32];
        while (_shots.Drain(shots) == shots.Length) { }
    }

    private void TakeSeekEvent(long tick, in SimEvent e, bool missingSource)
    {
        if (e.Kind == SimEventKind.SpawnEmitter)
        {
            _effects.RestoreLooseEmitter(tick, e);
            return;
        }
        if (e.Kind == SimEventKind.Notify && e.What == Emitter.NAME_ReplicateTrigger)
        {
            if (_session.Client.World.Level.FindActor(e.ActorId) is Emitter actor &&
                _session.Client.Map?.Loaded?.RecordOf(actor)?.Id is { } id)
                _seekTriggers.Add((id, tick));
            return;
        }
        if (e.Kind is not (SimEventKind.PlaySound or SimEventKind.StopSound or SimEventKind.SetSwitch or
            SimEventKind.PlayMusic or SimEventKind.StopMusic)) return;
        _resolver?.Handle(tick, e);
        ConsumeSounds(missingSource);
    }

    private static double Age(BridgeSound s, long tick) => (tick - (long)s.Tick) * (double)Sim.DeltaTime - s.Delay;

    private void PruneVoices(long tick)
    {
        for (int i = _seekVoices.Count - 1; i >= 0; i--)
        {
            SeekVoice voice = _seekVoices[i];
            var s = voice.Sound;
            double age = Age(s, tick);
            if ((s.SeekFadeOut > 0 && tick >= voice.FadeEnd) || (s.PlaySeconds > 0 && age >= s.PlaySeconds) ||
                ((s.Flags & BridgeSoundFlags.Loop) == 0 && s.Duration > 0 && age * s.Pitch >= s.Duration))
                _seekVoices.RemoveAt(i);
        }
    }

    private void ConsumeSounds(bool missingSource)
    {
        Span<BridgeSound> records = stackalloc BridgeSound[16];
        int count;
        while ((count = _sounds.Drain(records)) > 0)
        {
            foreach (var record in records[..count])
            {
                PruneVoices(record.Tick);
                bool music = record.Op == BridgeSoundOp.PlayMusic;
                if (music && _seekVoices.Exists(v => v.Sound.Op == BridgeSoundOp.PlayMusic &&
                    v.Sound.SoundId == record.SoundId && v.Sound.SeekFadeOut == 0)) continue;
                if (record.Op == BridgeSoundOp.Play || music)
                {
                    if (missingSource && (record.Flags & BridgeSoundFlags.Loop) != 0) continue;
                    if (music) FadeMusic(record.Tick, Math.Max(2f, record.FadeSeconds));
                    _seekVoices.Add(new(record));
                }
                else if (record.Op == BridgeSoundOp.StopMusic) FadeMusic(record.Tick, record.FadeSeconds);
                else if (record.Op == BridgeSoundOp.StopCall) _seekVoices.RemoveAll(v => v.Sound.CallId == record.CallId);
                else if (record.Op == BridgeSoundOp.Stop)
                {
                    bool all = (record.Flags & BridgeSoundFlags.AllSounds) != 0;
                    bool local = (record.Flags & BridgeSoundFlags.LocalPawnVoices) != 0;
                    _seekVoices.RemoveAll(v =>
                        (record.ActorId == 0 ? !all || v.Sound.Op != BridgeSoundOp.PlayMusic :
                            record.ActorId == v.Sound.ActorId || local && (v.Sound.Flags & BridgeSoundFlags.AtListener) != 0) &&
                        (all || record.SoundId == v.Sound.SoundId || record.SoundId == v.Sound.LeafId));
                }
            }
        }
    }

    private void FadeMusic(long tick, float seconds)
    {
        for (int i = _seekVoices.Count - 1; i >= 0; i--)
        {
            var voice = _seekVoices[i];
            if (voice.Sound.Op != BridgeSoundOp.PlayMusic || voice.Sound.SeekFadeOut != 0) continue;
            if (seconds <= 0) _seekVoices.RemoveAt(i);
            else
            {
                voice.FadeStart = tick;
                voice.FadeEnd = tick + seconds * Sim.Hz;
                voice.Sound.SeekFadeOut = seconds;
            }
        }
    }

    internal void FinishSeek()
    {
        long now = _session.Client.World.Level.TickCount;
        PruneVoices(now);
        foreach (SeekVoice voice in _seekVoices)
        {
            var s = voice.Sound;
            float age = (float)Age(s, now);
            s.Delay = Math.Max(0, -age);
            s.SeekOffset = Math.Max(0, age) * s.Pitch;
            if (s.PlaySeconds > 0) s.PlaySeconds -= Math.Max(0, age);
            s.FadeSeconds = Math.Max(0, s.FadeSeconds - Math.Max(0, age));
            if (s.SeekFadeOut > 0)
            {
                s.Volume *= (float)((voice.FadeEnd - now) / (voice.FadeEnd - voice.FadeStart));
                s.SeekFadeOut = (float)((voice.FadeEnd - now) * Sim.DeltaTime);
            }
            _sounds.Add(s);
        }
        _seekVoices.Clear();

        if (DemoBridge.Player is { } demo && _session.Client.Map?.Loaded is { } loaded)
        {
            var world = WorldState.Of(_bridge);
            var meshes = PlacedMeshParticles.Of(_bridge);
            var triggers = new Dictionary<string, List<long>>(StringComparer.OrdinalIgnoreCase);
            foreach (var (id, tick) in _seekTriggers)
            {
                if (!triggers.TryGetValue(id, out var times)) triggers[id] = times = [];
                times.Add(tick);
            }
            // Older toggles still determine what is enabled at the start of the particle lifetime window.
            double elapsed = ((long)demo.CurrentTick - demo.Recording.Index.FirstTick) / (double)demo.Header.TickRate;
            long start = now - (long)Math.Round(Math.Clamp(elapsed, 0, 20) * Sim.Hz);
            foreach (var placement in loaded.Gameplay.Actors)
            {
                if (loaded.ActorOf(placement) is not Emitter) continue;
                string id = placement.Id;
                long cursor = start;
                int owner = world.Intern(id);
                meshes.ResetDemoEmitter(owner);
                world.RestoreEmitterTrigger(id, reset: true, trigger: false, seconds: 0);
                if (triggers.TryGetValue(id, out var times))
                {
                    foreach (long t in times)
                    {
                        long next = Math.Clamp(t, cursor, now);
                        float seconds = (next - cursor) * Sim.DeltaTime;
                        meshes.AdvanceDemoEmitter(owner, seconds, trigger: true);
                        world.RestoreEmitterTrigger(id, reset: false, trigger: true, seconds);
                        cursor = next;
                    }
                }
                float tail = (now - cursor) * Sim.DeltaTime;
                meshes.AdvanceDemoEmitter(owner, tail, trigger: false);
                world.RestoreEmitterTrigger(id, reset: false, trigger: false, tail);
            }
            _seekTriggers.Clear();
        }
        _seekInProgress = false;
    }
}
