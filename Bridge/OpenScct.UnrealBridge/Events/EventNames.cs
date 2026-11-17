using System;
using System.Collections.Generic;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// The name table of the events feature: sound paths, class paths, bone names, notify names and file paths cross to
/// C++ as small integers, and C++ asks for the text of an id once (EventsApi.GetName) and keeps it. Ids are never
/// reused and stay valid for the life of the process, across sessions. 0 is "none". Names compare without case, as
/// the simulation's own names do. Only used on Unreal's game thread.
/// </summary>
public static class EventNames
{
    private static readonly Dictionary<string, int> s_ids = new(StringComparer.OrdinalIgnoreCase);
    private static readonly List<string> s_names = [""];

    // The simulation's names are already interned integers: their ids are found without making a string.
    private static readonly Dictionary<int, int> s_ofSimName = [];

    public static int Count => s_names.Count;

    /// <summary>The id of <paramref name="text"/>, 0 for null, empty or "None".</summary>
    public static int Id(string? text)
    {
        if (string.IsNullOrEmpty(text) || string.Equals(text, "None", StringComparison.OrdinalIgnoreCase))
        {
            return 0;
        }

        if (!s_ids.TryGetValue(text, out int id))
        {
            id = s_names.Count;
            s_names.Add(text);
            s_ids[text] = id;
        }

        return id;
    }

    /// <summary>The id of a simulation name.</summary>
    public static int Id(Name name)
    {
        if (name.IsNone)
        {
            return 0;
        }

        if (!s_ofSimName.TryGetValue(name.Index, out int id))
        {
            id = Id(name.ToString());
            s_ofSimName[name.Index] = id;
        }

        return id;
    }

    /// <summary>The text of an id, "" for 0 or an id never given out.</summary>
    public static string Text(int id) => id > 0 && id < s_names.Count ? s_names[id] : "";
}

/// <summary>
/// Records waiting for Unreal to take them: filled as the simulation ticks, emptied by a drain call each frame.
/// When Unreal does not drain (a paused editor), the oldest records are dropped beyond <see cref="Capacity"/>.
/// </summary>
internal sealed class DrainQueue<T>(int capacity)
    where T : unmanaged
{
    private readonly List<T> _items = new(256);
    private int _read;

    public int Capacity { get; } = capacity;

    /// <summary>Records waiting.</summary>
    public int Count => _items.Count - _read;

    /// <summary>Records dropped because nothing drained them.</summary>
    public long Dropped { get; private set; }

    /// <summary>Records added since the session started.</summary>
    public long Total { get; private set; }

    public void Add(in T item)
    {
        if (Count >= Capacity)
        {
            _read++;
            Dropped++;
        }

        // The read part is cut off before the list grows, so a drained queue never reallocates.
        if (_read > 0 && _items.Count == _items.Capacity)
        {
            _items.RemoveRange(0, _read);
            _read = 0;
        }

        _items.Add(item);
        Total++;
    }

    /// <summary>Moves the oldest records into <paramref name="into"/>. Returns how many; the rest wait for the next call.</summary>
    public int Drain(Span<T> into)
    {
        int count = Math.Min(into.Length, Count);
        for (int i = 0; i < count; i++)
        {
            into[i] = _items[_read + i];
        }

        _read += count;
        if (_read == _items.Count)
        {
            _items.Clear();
            _read = 0;
        }

        return count;
    }
}
