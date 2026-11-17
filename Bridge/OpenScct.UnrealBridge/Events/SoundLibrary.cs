using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Events;

/// <summary>One entry of a random, switch or sequence sound: the sound it names (null for a "Remove" entry) and its weight, surface or count.</summary>
internal readonly record struct SoundEntry(string? Sound, int Value);

/// <summary>A Sound of the converted content (svm.audio/1 and svm.mapaudio/1, OpenSCCT docs/AUDIO.md).</summary>
internal sealed class SoundDef
{
    public required string Path { get; init; }

    /// <summary>wave, stream, multichannel, random, switch or sequence.</summary>
    public required string Kind { get; init; }

    public int Flags { get; init; }

    /// <summary>The Sound's Gain (linear).</summary>
    public float Volume { get; init; } = 1f;

    /// <summary>The Sound's own Radius, 0 when it has none.</summary>
    public float Radius { get; init; }

    public bool Loop { get; init; }

    public bool Positional { get; init; } = true;

    /// <summary>Absolute path of the wave or Ogg file, null for list sounds and streams the map's bank lacks.</summary>
    public string? File { get; init; }

    public float Duration { get; init; }

    public int LoopStart { get; init; } = -1;

    public int LoopEnd { get; init; } = -1;

    public SoundEntry[] Entries { get; init; } = [];

    /// <summary>Name id of <see cref="Path"/>.</summary>
    public int PathId { get; init; }

    /// <summary>Name id of the file relative to the content root (forward slashes), 0 without a file.</summary>
    public int FileId { get; init; }

    public bool IsStream => File is not null && File.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase);
}

/// <summary>A sound actor of the map's audio.json (also the AmbientSound actors only the client has).</summary>
internal sealed record MapSoundActor(string Id, string Class, string Category, FVector Location, string? AmbientSound,
    byte SoundVolume, float SoundRadius, byte SoundPitch, int LightType, float LightBrightness, bool Loop, float RepeatInterval);

/// <summary>
/// The sounds a client can play: the shared library (<c>shared/audio</c>) with the map's own table
/// (<c>svmap/&lt;Map&gt;/audio</c>) on top, which supplies the map's streamed sounds. A port of the reading half of
/// the Godot client's AudioLibrary (game/Scripts/Audio/AudioLibrary.cs); the files themselves are played by Unreal.
/// </summary>
internal sealed class SoundLibrary
{
    private readonly Dictionary<string, SoundDef> _sounds = new(StringComparer.OrdinalIgnoreCase);
    private readonly string _contentRoot;

    private SoundLibrary(string contentRoot)
    {
        _contentRoot = contentRoot;
    }

    public int Count => _sounds.Count;

    /// <summary>The map's sound actors (empty without a map table).</summary>
    public List<MapSoundActor> Actors { get; } = [];

    /// <summary>Ids of the map's actors and zones whose ambient sound plays once, not as a loop.</summary>
    public HashSet<string> OneShots { get; } = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Repeat intervals of the map's actors and zones, by id.</summary>
    public Dictionary<string, float> RepeatIntervals { get; } = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>LevelInfo.Song: the music the engine starts when the map loads.</summary>
    public string? LevelSong { get; private set; }

    /// <summary>False when the map folder has no audio table: its ambience and streamed sounds are then silent.</summary>
    public bool HasMapTable { get; private set; }

    /// <summary>
    /// Loads the library of <paramref name="sharedRoot"/> (the folder holding <c>audio</c>) and the table of
    /// <paramref name="mapFolder"/>. File ids are relative to the folder above <paramref name="sharedRoot"/>.
    /// </summary>
    public static SoundLibrary Load(string sharedRoot, string? mapFolder)
    {
        string sharedDir = Path.Combine(sharedRoot, "audio");
        var library = new SoundLibrary(Path.GetFullPath(Path.Combine(sharedRoot, "..")));

        // The same files in the same order as the Godot client: later ones replace sounds of earlier ones.
        foreach (string name in new[] { "audio.json", "sidearms.json", "weapons.json", "sidearm-audio.json", "rifles.json", "user-gun-audio.json" })
        {
            string path = Path.Combine(sharedDir, name);
            if (File.Exists(path))
            {
                library.ReadShared(path, sharedDir);
            }
        }

        if (mapFolder is not null && Path.Combine(mapFolder, "audio") is { } mapDir &&
            File.Exists(Path.Combine(mapDir, "audio.json")))
        {
            library.ReadMap(Path.Combine(mapDir, "audio.json"), mapDir);
            library.HasMapTable = true;
        }

        return library;
    }

    public SoundDef? Find(string? path) => path is not null && _sounds.TryGetValue(path, out SoundDef? d) ? d : null;

    private void ReadShared(string path, string baseDir)
    {
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path));
        JsonElement root = doc.RootElement;
        if (!root.TryGetProperty("format", out JsonElement f) || f.GetString() != "svm.audio/1")
        {
            throw new InvalidDataException($"{path}: not an svm.audio/1 library");
        }

        foreach (JsonProperty p in root.GetProperty("sounds").EnumerateObject())
        {
            _sounds[p.Name] = Read(p.Name, p.Value, baseDir);
        }
    }

    private void ReadMap(string path, string mapDir)
    {
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path));
        JsonElement root = doc.RootElement;
        if (!root.TryGetProperty("format", out JsonElement f) || f.GetString() != "svm.mapaudio/1")
        {
            throw new InvalidDataException($"{path}: not an svm.mapaudio/1 file");
        }

        foreach (JsonProperty p in root.GetProperty("sounds").EnumerateObject())
        {
            JsonElement v = p.Value;
            bool ofMap = v.TryGetProperty("root", out JsonElement own) && own.GetString() == "map";

            // A complete definition: a bank stream (or a sound stored in the map package). Its file is in the map folder.
            if (ofMap && v.TryGetProperty("flags", out _))
            {
                _sounds[p.Name] = Read(p.Name, v, mapDir);
                continue;
            }

            // A streamed sound of this map's bank named without its definition: the shared entry with the map's file.
            if (ofMap && v.TryGetProperty("file", out JsonElement file) && _sounds.TryGetValue(p.Name, out SoundDef? shared))
            {
                string full = Path.GetFullPath(Path.Combine(mapDir, file.GetString()!));
                _sounds[p.Name] = new SoundDef
                {
                    Path = shared.Path, Kind = shared.Kind, Flags = shared.Flags, Volume = shared.Volume, Radius = shared.Radius,
                    Loop = shared.Loop, Positional = shared.Positional, Entries = shared.Entries, File = full,
                    Duration = v.TryGetProperty("duration", out JsonElement d) ? d.GetSingle() : 0f,
                    PathId = shared.PathId, FileId = FileId(full),
                };
            }
        }

        JsonElement actors = root.GetProperty("actors");
        foreach (JsonElement a in actors.EnumerateArray())
        {
            string cls = a.GetProperty("class").GetString()!;
            if (cls.EndsWith("LevelInfo", StringComparison.Ordinal) && a.TryGetProperty("sounds", out JsonElement levelSounds) &&
                levelSounds.TryGetProperty("Song", out JsonElement song))
            {
                LevelSong = song.GetString();
            }

            JsonElement loc = a.GetProperty("location");
            var actor = new MapSoundActor(
                a.GetProperty("id").GetString()!, cls, a.GetProperty("category").GetString()!,
                new FVector(loc.GetProperty("X").GetSingle(), loc.GetProperty("Y").GetSingle(), loc.GetProperty("Z").GetSingle()),
                a.TryGetProperty("ambientSound", out JsonElement s) ? s.GetString() : null,
                (byte)Number(a, "soundVolume", 128), Number(a, "soundRadius", 64f), (byte)Number(a, "soundPitch", 64),
                (int)Number(a, "lightType", 0), Number(a, "lightBrightness", 64f),
                !a.TryGetProperty("loop", out JsonElement loop) || loop.ValueKind != JsonValueKind.False, Interval(a));
            Actors.Add(actor);
            Note(actor.Id, actor.Loop, actor.RepeatInterval);
        }

        foreach (JsonElement z in root.GetProperty("zones").EnumerateArray())
        {
            Note(z.GetProperty("id").GetString()!,
                !z.TryGetProperty("loop", out JsonElement loop) || loop.ValueKind != JsonValueKind.False, Interval(z));
        }
    }

    private void Note(string id, bool loop, float interval)
    {
        if (!loop)
        {
            OneShots.Add(id);
        }

        if (interval > 0f)
        {
            RepeatIntervals[id] = interval;
        }
    }

    private SoundDef Read(string path, JsonElement v, string baseDir)
    {
        var entries = new List<SoundEntry>();
        if (v.TryGetProperty("entries", out JsonElement es))
        {
            foreach (JsonElement e in es.EnumerateArray())
            {
                string? s = e.TryGetProperty("sound", out JsonElement sv) && sv.ValueKind == JsonValueKind.String ? sv.GetString() : null;
                entries.Add(new SoundEntry(s, e.GetProperty("value").GetInt32()));
            }
        }

        string? file = v.TryGetProperty("file", out JsonElement fe) ? Path.GetFullPath(Path.Combine(baseDir, fe.GetString()!)) : null;
        return new SoundDef
        {
            Path = path,
            Kind = v.GetProperty("kind").GetString()!,
            Flags = v.GetProperty("flags").GetInt32(),
            Volume = v.GetProperty("volume").GetSingle(),
            Radius = v.GetProperty("radius").GetSingle(),
            Loop = v.GetProperty("loop").GetBoolean(),
            Positional = v.GetProperty("positional").GetBoolean(),
            File = file,
            Duration = v.TryGetProperty("duration", out JsonElement du) ? du.GetSingle() : 0f,
            LoopStart = v.TryGetProperty("loopStart", out JsonElement ls) ? ls.GetInt32() : -1,
            LoopEnd = v.TryGetProperty("loopEnd", out JsonElement le) ? le.GetInt32() : -1,
            Entries = entries.ToArray(),
            PathId = EventNames.Id(path),
            FileId = file is null ? 0 : FileId(file),
        };
    }

    /// <summary>The name id of a file as C++ opens it: relative to the content root, or the full path when it lies outside.</summary>
    private int FileId(string file)
    {
        string relative = Path.GetRelativePath(_contentRoot, file);
        return EventNames.Id((relative.StartsWith("..", StringComparison.Ordinal) ? file : relative).Replace('\\', '/'));
    }

    private static float Number(JsonElement e, string name, float fallback) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetSingle() : fallback;

    private static float Interval(JsonElement element)
    {
        float seconds = Number(element, "repeatInterval", 0f);
        return float.IsFinite(seconds) ? Math.Clamp(seconds, 0f, 86400f) : 0f;
    }
}
