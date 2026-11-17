using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// The ambient sounds of the client world: every actor whose AmbientSound plays now. Ambient sounds are state, not
/// events, so they are read each frame instead of drained. A port of what the Godot client collects before its
/// ambient rules run (LevelAudioWorld.CollectAmbient in game/Scripts/Audio/AudioWorld.cs and
/// SoundSystem.AddStaticAmbient): the simulation's actors (zones, ambient triggers, movers, running conveyors, armed
/// gadgets) and the actors only the map's audio table has (AmbientSound actors, lights and meshes with a sound).
/// Which of them is audible and how loud (rules 19 to 22: range, zone, the loudest of several) is left to Unreal;
/// the values each rule needs are in the record.
/// </summary>
internal sealed class AmbientSounds
{
    private const float VolumeUnit = 255f;
    private const float PitchUnit = 64f;

    private readonly PlaySession _session;
    private readonly SoundLibrary _library;
    private readonly SoundResolver _resolver;
    private readonly Dictionary<int, Resolved> _resolved = [];
    private readonly HashSet<int> _seen = [];
    private readonly List<int> _stale = [];
    private Dictionary<(int, int, int), MapSoundActor>? _placedRows;

    public AmbientSounds(PlaySession session, SoundLibrary library, SoundResolver resolver)
    {
        _session = session;
        _library = library;
        _resolver = resolver;
    }

    /// <summary>Writes the ambient sources of this frame. Returns the count written.</summary>
    public int Write(Span<BridgeAmbientSound> into)
    {
        Level level = _session.Client.World.Level;
        FVector listener = _session.View.Valid ? _session.View.Location : default;
        ZoneInfo? listenerZone = _session.View.Valid ? GadgetWorld.Collision(level.Info)?.GetZoneInfo(listener) : null;
        int count = 0;
        _seen.Clear();

        // Indexed, not foreach over the interface (a boxed enumerator every frame).
        IReadOnlyList<Actor> actors = level.Actors;
        for (int i = 0; i < actors.Count && count < into.Length; i++)
        {
            Actor a = actors[i];
            if (a is null || a.bDeleteMe || a.AmbientSound.IsNone)
            {
                continue;
            }

            string sound = a.AmbientSound.Path!;
            FVector at = a.Location;
            byte volume = a.SoundVolume, pitch = a.SoundPitch;
            float radius = a.SoundRadius;
            string? mapId = null;
            if (a is Conveyor conveyor)
            {
                // A belt hums while it runs, from the point of its path nearest the listener.
                if (conveyor.SurfaceSpeed == 0f || !conveyor.Hums)
                {
                    continue;
                }

                at = conveyor.NearestOnPath(listener);
            }
            else if (PlacedRow(a) is { } row)
            {
                // The map's row says whether the sound loops. A trigger's channel carries its AmbientSound and nothing
                // else, so the client's copy has the class's volume, radius and pitch; the row has the placed ones.
                mapId = row.Id;
                if (a is SAmbientSoundTrigger)
                {
                    (volume, radius, pitch) = (row.SoundVolume, row.SoundRadius, row.SoundPitch);
                }
            }
            else if (a is SMines mine)
            {
                pitch = (byte)MathF.Round(a.SoundPitch * mine.MineSoundPitch);
            }

            float light = a.LightType != 0 ? a.LightBrightness / VolumeUnit : 1f;
            bool zone = a is ZoneInfo;
            if (Record(a.ActorId, sound, at, volume / VolumeUnit * light, radius, pitch, zone,
                    zone && ReferenceEquals(a, listenerZone), mapId, out BridgeAmbientSound record))
            {
                into[count++] = record;
            }
        }

        // The actors the simulation leaves out (the map table's "render" rows); negative keys keep them apart.
        List<MapSoundActor> placed = _library.Actors;
        for (int i = 0; i < placed.Count && count < into.Length; i++)
        {
            MapSoundActor a = placed[i];
            if (a.AmbientSound is null || a.Category != "render")
            {
                continue;
            }

            float light = a.LightType != 0 ? a.LightBrightness / VolumeUnit : 1f;
            bool zone = a.Class.EndsWith("ZoneInfo", StringComparison.Ordinal) || a.Class.EndsWith("LevelInfo", StringComparison.Ordinal);
            if (Record(-(i + 1), a.AmbientSound, a.Location, a.SoundVolume / VolumeUnit * light, a.SoundRadius, a.SoundPitch,
                    zone, false, a.Id, out BridgeAmbientSound record))
            {
                into[count++] = record;
            }
        }

        // The picks of sources that are gone are dropped, so a source that comes back picks again.
        _stale.Clear();
        foreach (int key in _resolved.Keys)
        {
            if (!_seen.Contains(key))
            {
                _stale.Add(key);
            }
        }

        foreach (int key in _stale)
        {
            _resolved.Remove(key);
        }

        return count;
    }

    private bool Record(int key, string sound, FVector at, float volume, float actorRadius, byte soundPitch, bool zone,
        bool listenerInZone, string? mapId, out BridgeAmbientSound record)
    {
        record = default;
        _seen.Add(key);
        if (!_resolved.TryGetValue(key, out Resolved r) || r.Sound != sound)
        {
            r = Resolve(sound);
            _resolved[key] = r;
        }

        if (r.Def is not { } def)
        {
            return false;
        }

        SoundDef? leaf = r.Leaf;
        bool loop = mapId is null || !_library.OneShots.Contains(mapId);
        float interval = mapId is not null && _library.RepeatIntervals.TryGetValue(mapId, out float seconds) ? seconds : 0f;
        record = new BridgeAmbientSound
        {
            Key = key,
            SoundId = def.PathId,
            FileId = leaf?.FileId ?? 0,
            // Rules 20 to 22: an ambient loop is 3D or 2D by its own Sound (the one the actor names), whatever a list resolves to.
            Flags = (def.Positional ? BridgeAmbientFlags.Positional : 0) | (zone ? BridgeAmbientFlags.Zone : 0) |
                (listenerInZone ? BridgeAmbientFlags.ListenerInZone : 0) | (loop && interval <= 0f ? BridgeAmbientFlags.Loop : 0) |
                (leaf is { IsStream: true } ? BridgeAmbientFlags.Stream : 0) | (r.Sequence ? BridgeAmbientFlags.Sequence : 0),
            X = at.X,
            Y = at.Y,
            Z = at.Z,
            Volume = SoundResolver.SourceVolume(volume * r.Gain * (leaf?.Volume ?? 1f)),
            Radius = def.Radius > 0f ? def.Radius : actorRadius,
            Pitch = Math.Clamp(soundPitch / PitchUnit, 0.5f, 2f),
            RepeatInterval = interval,
        };
        return true;
    }

    /// <summary>The file an ambient sound plays, picked once while its source lasts (a random list picks one entry).</summary>
    private Resolved Resolve(string sound)
    {
        if (_library.Find(sound) is not { } def)
        {
            return new Resolved(sound, null, null, 1f, false);
        }

        float gain = 1f;
        SoundDef? leaf = _resolver.Resolve(def, default, 0, ref gain);
        bool sequence = leaf?.Kind == "sequence";
        if (sequence)
        {
            // A sequence as an ambient sound: its first entry stands for it here.
            gain *= leaf!.Volume;
            SoundDef? first = null;
            foreach (SoundEntry entry in leaf.Entries)
            {
                if (_library.Find(entry.Sound) is { } next && _resolver.Resolve(next, default, 1, ref gain) is { FileId: not 0 } found)
                {
                    first = found;
                    break;
                }
            }

            leaf = first;
        }

        return new Resolved(sound, def, leaf, gain, sequence);
    }

    /// <summary>
    /// The map's own row of an actor it placed, found by where it stands (LevelAudioWorld.PlacedTrigger does this for
    /// the sound triggers; here zones are found the same way, for their loop and repeat settings).
    /// </summary>
    private MapSoundActor? PlacedRow(Actor a)
    {
        if (a is not (SAmbientSoundTrigger or ZoneInfo) || _library.Actors.Count == 0)
        {
            return null;
        }

        if (_placedRows is null)
        {
            _placedRows = [];
            foreach (MapSoundActor row in _library.Actors)
            {
                if (row.AmbientSound is not null && row.Category != "render")
                {
                    _placedRows.TryAdd(Whole(row.Location), row);
                }
            }
        }

        return _placedRows.TryGetValue(Whole(a.Location), out MapSoundActor? found) &&
            found.Class.EndsWith(a is ZoneInfo ? "ZoneInfo" : ".SAmbientSoundTrigger", StringComparison.Ordinal) ? found : null;
    }

    private static (int, int, int) Whole(FVector p) =>
        ((int)MathF.Floor(p.X + 0.5f), (int)MathF.Floor(p.Y + 0.5f), (int)MathF.Floor(p.Z + 0.5f));

    private readonly record struct Resolved(string Sound, SoundDef? Def, SoundDef? Leaf, float Gain, bool Sequence);
}
