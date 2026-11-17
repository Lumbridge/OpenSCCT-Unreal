using System;
using System.Collections.Generic;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>
/// The names the equipment structs carry as ids (classes, meshes, bones, skins, file paths). A name gets its id on
/// first use and keeps it for the life of the process, so Unreal can cache what it resolved; per frame only ids
/// cross, and looking up a name that is already known allocates nothing. Id 0 is "no name".
/// </summary>
public static class EquipmentNames
{
    private static readonly Dictionary<string, int> s_ids = new(StringComparer.Ordinal) { [""] = 0 };
    private static readonly List<string> s_names = [""];
    private static readonly Dictionary<Name, int> s_ofName = new();
    private static readonly Dictionary<Type, int> s_ofType = new();
    private static readonly object s_gate = new();

    /// <summary>The id of <paramref name="text"/> (0 for null or empty).</summary>
    public static int Id(string? text)
    {
        if (string.IsNullOrEmpty(text))
        {
            return 0;
        }

        lock (s_gate)
        {
            if (!s_ids.TryGetValue(text, out int id))
            {
                id = s_names.Count;
                s_names.Add(text);
                s_ids[text] = id;
            }

            return id;
        }
    }

    /// <summary>The id of a simulation name (a bone, a sequence), without making its string again.</summary>
    public static int Id(Name name)
    {
        if (name.IsNone)
        {
            return 0;
        }

        lock (s_gate)
        {
            if (s_ofName.TryGetValue(name, out int known))
            {
                return known;
            }
        }

        int id = Id(name.ToString());
        lock (s_gate)
        {
            s_ofName[name] = id;
        }

        return id;
    }

    /// <summary>The id of the class of <paramref name="instance"/> as the simulation names it (the C# type's name: SWeaponDefense, SGadStickyCam...).</summary>
    public static int ClassId(object? instance)
    {
        if (instance is null)
        {
            return 0;
        }

        Type type = instance.GetType();
        lock (s_gate)
        {
            if (s_ofType.TryGetValue(type, out int known))
            {
                return known;
            }
        }

        int id = Id(type.Name);
        lock (s_gate)
        {
            s_ofType[type] = id;
        }

        return id;
    }

    /// <summary>The text of an id, or "" for an id never given out.</summary>
    public static string Text(int id)
    {
        lock (s_gate)
        {
            return id > 0 && id < s_names.Count ? s_names[id] : "";
        }
    }
}
