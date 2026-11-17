using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// The session's HUD texts by id. The HUD structs carry ids instead of strings (they stay blittable and nothing is
/// allocated per frame); Unreal reads a text once with <c>GetText</c> and keeps it. A text keeps its id for the whole
/// session. Id 0 is the empty text.
/// <para>
/// Only names and content texts go in here (weapons, gadgets, players, objectives, prompts, the lines the original
/// stacks), never numbers that change every second, so the table stays small.
/// </para>
/// </summary>
public sealed class HudTextTable
{
    private readonly Dictionary<string, int> _ids = new(StringComparer.Ordinal) { [""] = 0 };
    private readonly List<string> _texts = [""];

    public int Count => _texts.Count;

    /// <summary>The id of <paramref name="text"/>, added on first sight. Null and empty are 0.</summary>
    public int Id(string? text)
    {
        if (string.IsNullOrEmpty(text))
        {
            return 0;
        }

        if (!_ids.TryGetValue(text, out int id))
        {
            id = _texts.Count;
            _texts.Add(text);
            _ids[text] = id;
        }

        return id;
    }

    /// <summary>The text of <paramref name="id"/>, or empty for an id never given out.</summary>
    public string Text(int id) => id >= 0 && id < _texts.Count ? _texts[id] : "";
}

/// <summary>
/// The game's interface strings (the <c>.int</c> files of the player's own install, converted to
/// <c>shared/ui/strings.json</c>): file, then section, then key. The HUD reads its few labels that are not class
/// defaults here (the Speak and Hack prompts, "Inf."), as the Godot client's HudAssets.Localize does, and Unreal can
/// ask for any other label of the original through <c>Localize</c>. Missing content gives the fallback.
/// </summary>
public sealed class HudStrings
{
    private readonly Dictionary<string, Dictionary<string, Dictionary<string, string>>> _files = new(StringComparer.OrdinalIgnoreCase);

    public bool Loaded => _files.Count > 0;

    /// <summary>Reads <c>ui/strings.json</c> under the shared content folder; a missing folder or file leaves it empty.</summary>
    public static HudStrings Load(string? sharedRoot)
    {
        var strings = new HudStrings();
        string? path = sharedRoot is null ? null : Path.Combine(sharedRoot, "ui", "strings.json");
        if (path is null || !File.Exists(path))
        {
            return strings;
        }

        // A damaged file must not stop the match: the HUD then runs without these few labels.
        JsonDocument doc;
        try
        {
            doc = JsonDocument.Parse(File.ReadAllBytes(path));
        }
        catch (Exception e) when (e is JsonException or IOException or UnauthorizedAccessException)
        {
            return strings;
        }

        using JsonDocument owned = doc;
        if (doc.RootElement.ValueKind != JsonValueKind.Object)
        {
            return strings;
        }

        foreach (JsonProperty file in doc.RootElement.EnumerateObject())
        {
            if (file.Value.ValueKind != JsonValueKind.Object)
            {
                continue;
            }

            var sections = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
            foreach (JsonProperty section in file.Value.EnumerateObject())
            {
                if (section.Value.ValueKind != JsonValueKind.Object)
                {
                    continue;
                }

                var keys = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                foreach (JsonProperty key in section.Value.EnumerateObject())
                {
                    keys[key.Name] = key.Value.ValueKind == JsonValueKind.String ? key.Value.GetString() ?? "" : "";
                }

                sections[section.Name] = keys;
            }

            strings._files[file.Name] = sections;
        }

        return strings;
    }

    /// <summary>A string of <c>System/&lt;file&gt;.int</c>, or <paramref name="fallback"/>.</summary>
    public string Get(string file, string section, string key, string fallback = "") =>
        _files.TryGetValue(file, out Dictionary<string, Dictionary<string, string>>? sections) &&
        sections.TryGetValue(section, out Dictionary<string, string>? keys) && keys.TryGetValue(key, out string? value)
            ? value
            : fallback;
}
