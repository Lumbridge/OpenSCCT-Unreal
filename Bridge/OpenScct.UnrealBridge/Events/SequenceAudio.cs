using System;
using System.Collections.Generic;
using OpenScct.Animation;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// The audio tracks of the client world's animation sequences: every audio segment an <see cref="AnimationController"/>
/// plays at this moment of its replicated clock, with the place it sounds from and how far into its file it is. A port
/// of what the Godot client's AnimationSequenceAudio reads each frame (game/Scripts/Audio/AnimationSequenceAudio.cs):
/// <see cref="AnimationController.Audio"/> gives each segment's id, sound, rate and offset, and the segment's target
/// actor (found through <see cref="AnimationWorld"/>) or the controller gives its place. The offset comes from the
/// controller's clock, which a client restores from the server's replicated PlaybackTime, so a player who joins in the
/// middle of a sequence gets segments already part-way through and starts them at the right point. Like the ambient
/// sounds this is state, read each frame, not drained; Unreal keeps one voice per segment key, starts it at the offset,
/// seeks it when it drifts and stops it when its segment is no longer listed (Play/ScctAudioSequences.cpp).
/// </summary>
internal sealed class SequenceAudio
{
    private readonly PlaySession _session;
    private readonly SoundLibrary _library;
    private readonly SoundResolver _resolver;

    /// <summary>A key per segment instance id (controller/generation/segment), never reused in a session.</summary>
    private readonly Dictionary<string, int> _keys = new(StringComparer.Ordinal);
    private readonly Dictionary<int, Resolved> _resolved = [];
    private readonly HashSet<string> _seen = new(StringComparer.Ordinal);
    private readonly List<string> _stale = [];
    private readonly HashSet<string> _unknown = new(StringComparer.Ordinal);
    private AnimationProject? _indexed;
    private readonly Dictionary<string, SequenceSegment> _segments = new(StringComparer.Ordinal);
    private int _nextKey = 1;

    public SequenceAudio(PlaySession session, SoundLibrary library, SoundResolver resolver)
    {
        _session = session;
        _library = library;
        _resolver = resolver;
    }

    /// <summary>Distinct segment sounds the library does not know (their segments are listed with no file).</summary>
    public int UnknownCount => _unknown.Count;

    /// <summary>Writes the audio segments sounding now. Returns the count written.</summary>
    public int Write(Span<BridgeSequenceSound> into)
    {
        Level level = _session.Client.World.Level;
        _seen.Clear();
        int count = 0;
        if (level.GetService<AnimationWorld>() is { } world)
        {
            Index(world.Project);
            foreach (AnimationController controller in level.AllActors<AnimationController>())
            {
                if (controller.bDeleteMe)
                {
                    continue;
                }

                foreach ((string id, string sound, float offset, float rate, string target) in controller.Audio())
                {
                    if (count >= into.Length)
                    {
                        break;
                    }

                    _seen.Add(id);
                    if (!_keys.TryGetValue(id, out int key))
                    {
                        key = _nextKey++;
                        _keys.Add(id, key);
                    }

                    if (!_resolved.TryGetValue(key, out Resolved r))
                    {
                        // A random list picks once per segment, as the Godot client makes one player per segment id.
                        r = Resolve(sound);
                        _resolved[key] = r;
                    }

                    Actor? actor = world.Actor(target);
                    FVector at = actor?.Location ?? controller.Location;
                    _segments.TryGetValue(SegmentId(id), out SequenceSegment? segment);
                    into[count++] = new BridgeSequenceSound
                    {
                        Key = key,
                        NameId = EventNames.Id(id),
                        SoundId = r.Def?.PathId ?? EventNames.Id(sound),
                        FileId = r.Leaf?.FileId ?? 0,
                        ActorId = actor?.ActorId ?? 0,
                        Flags = (r.Def is { Positional: true } ? BridgeSequenceFlags.Positional : 0) |
                            (actor is null ? BridgeSequenceFlags.AtController : 0) |
                            (r.Def is null ? BridgeSequenceFlags.Unknown : 0),
                        ControllerId = controller.ActorId,
                        X = at.X,
                        Y = at.Y,
                        Z = at.Z,
                        Offset = Math.Max(offset, 0f),
                        Rate = Math.Max(rate, .01f),
                        Volume = SoundResolver.SourceVolume(r.Gain * (r.Leaf?.Volume ?? 1f)),
                        Radius = r.Def?.Radius ?? 0f,
                        Clock = controller.PlaybackTime,
                        SegmentStart = segment?.Start ?? 0f,
                        SegmentFileOffset = segment?.Offset ?? 0f,
                    };
                }
            }
        }

        // Segments that ended or whose controller restarted (a new generation) are dropped with their pick.
        _stale.Clear();
        foreach (string id in _keys.Keys)
        {
            if (!_seen.Contains(id))
            {
                _stale.Add(id);
            }
        }

        foreach (string id in _stale)
        {
            _resolved.Remove(_keys[id]);
            _keys.Remove(id);
        }

        return count;
    }

    /// <summary>The segment's own id: the last part of controller/generation/segment.</summary>
    private static string SegmentId(string id)
    {
        int slash = id.LastIndexOf('/');
        return slash >= 0 ? id[(slash + 1)..] : id;
    }

    private void Index(AnimationProject project)
    {
        if (ReferenceEquals(project, _indexed))
        {
            return;
        }

        _indexed = project;
        _segments.Clear();
        foreach (AnimationSequence sequence in project.Sequences)
        {
            foreach (SequenceTrack track in sequence.Tracks)
            {
                if (track.Kind != SequenceTrackKind.Audio)
                {
                    continue;
                }

                foreach (SequenceSegment segment in track.Segments)
                {
                    _segments.TryAdd(segment.Id, segment);
                }
            }
        }
    }

    private Resolved Resolve(string sound)
    {
        if (_library.Find(sound) is not { } def)
        {
            _unknown.Add(sound);
            return new Resolved(null, null, 1f);
        }

        float gain = 1f;
        SoundDef? leaf = _resolver.Resolve(def, default, 0, ref gain);
        if (leaf?.Kind == "sequence")
        {
            // A sound sequence on an audio track: its first entry with a file stands for it (as for an ambient sound).
            gain *= leaf.Volume;
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

        return new Resolved(def, leaf, gain);
    }

    private readonly record struct Resolved(SoundDef? Def, SoundDef? Leaf, float Gain);
}
