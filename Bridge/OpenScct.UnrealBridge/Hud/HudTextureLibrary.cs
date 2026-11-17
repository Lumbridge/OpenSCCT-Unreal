using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Server;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// The converted interface content the HUD's pictures come from, read as the Godot client's HudAssets reads it
/// (game/Scripts/Hud/HudAssets.cs): <c>shared/ui/manifest.json</c> lists each package's textures with their size, the
/// PNGs sit in <c>textures/&lt;package&gt;/</c>, and a Magma package (<c>magma/Hud.json</c>) names the region of a
/// texture each of its materials shows. Textures are decoded on first use and kept; a missing one is remembered as
/// missing. The content folder is the one the bridge was configured with (ContentPaths.Shared).
/// </summary>
public sealed class HudTextureLibrary
{
    public const string UiFormat = "svm.ui/1";

    private static HudTextureLibrary? _current;
    private static string? _currentRoot;
    private static readonly object Gate = new();

    private readonly Dictionary<(string, string), string> _files = new();
    private readonly Dictionary<(string, string), HudPng.Image?> _images = new();
    private readonly Dictionary<string, string> _packageFiles = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, JsonDocument?> _packages = new(StringComparer.OrdinalIgnoreCase);

    private HudTextureLibrary(string root)
    {
        Root = root;
    }

    /// <summary>The ui folder this library reads.</summary>
    public string Root { get; }

    /// <summary>Why the last lookup found nothing, for the log.</summary>
    public string? LastError { get; private set; }

    /// <summary>The library of the configured content, or null with the reason when there is no UI content.</summary>
    public static HudTextureLibrary? Current(out string? error)
    {
        string? root = ContentPaths.Shared is { } shared ? Path.Combine(shared, "ui") : null;
        lock (Gate)
        {
            if (_current is not null && string.Equals(_currentRoot, root, StringComparison.OrdinalIgnoreCase))
            {
                error = null;
                return _current;
            }

            error = "no content folder is configured";
            _current = root is null ? null : TryLoad(root, out error);
            _currentRoot = root;
            return _current;
        }
    }

    /// <summary>Loads the manifest under <paramref name="root"/>, or null with the reason.</summary>
    public static HudTextureLibrary? TryLoad(string root, out string? error)
    {
        string manifest = Path.Combine(root, "manifest.json");
        if (!File.Exists(manifest))
        {
            error = $"no UI content at {root} (run: python -m svmbuild ui)";
            return null;
        }

        try
        {
            var library = new HudTextureLibrary(root);
            using FileStream stream = File.OpenRead(manifest);
            using JsonDocument doc = JsonDocument.Parse(stream);
            JsonElement top = doc.RootElement;
            if (top.GetProperty("format").GetString() != UiFormat)
            {
                error = $"{manifest}: not a {UiFormat} manifest";
                return null;
            }

            foreach (JsonProperty group in top.GetProperty("textures").EnumerateObject())
            {
                foreach (JsonElement t in group.Value.EnumerateArray())
                {
                    string name = t.GetProperty("name").GetString() ?? "";
                    library._files[Key(group.Name, name)] = Path.Combine(root, "textures", group.Name, name + ".png");
                }
            }

            foreach (JsonProperty p in top.GetProperty("packages").EnumerateObject())
            {
                library._packageFiles[p.Name] = Path.Combine(root, p.Value.GetProperty("file").GetString() ?? "");
            }

            error = null;
            return library;
        }
        catch (Exception e) when (e is IOException or JsonException or KeyNotFoundException or InvalidOperationException or UnauthorizedAccessException)
        {
            error = $"UI content at {root} could not be read: {e.Message}";
            return null;
        }
    }

    private static (string, string) Key(string package, string name) => (package.ToUpperInvariant(), name.ToUpperInvariant());

    /// <summary>The texture <paramref name="package"/>.<paramref name="name"/>, decoded, or null (LastError says why).</summary>
    public HudPng.Image? Texture(string package, string name)
    {
        lock (Gate)
        {
            (string, string) key = Key(package, name);
            if (_images.TryGetValue(key, out HudPng.Image? kept))
            {
                LastError = kept is null ? $"{package}.{name} could not be read before" : null;
                return kept;
            }

            HudPng.Image? image = null;
            if (!_files.TryGetValue(key, out string? file))
            {
                LastError = $"the content has no texture {package}.{name}";
            }
            else
            {
                try
                {
                    image = HudPng.Read(File.ReadAllBytes(file), out string? error);
                    LastError = image is null ? $"{file}: {error}" : null;
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException)
                {
                    LastError = $"{file}: {e.Message}";
                }
            }

            _images[key] = image;
            return image;
        }
    }

    /// <summary>A Magma package's document (magma/&lt;name&gt;.json), parsed on first use, or null.</summary>
    public JsonDocument? Package(string name)
    {
        lock (Gate)
        {
            if (_packages.TryGetValue(name, out JsonDocument? kept))
            {
                return kept;
            }

            JsonDocument? doc = null;
            if (_packageFiles.TryGetValue(name, out string? file) && File.Exists(file))
            {
                try
                {
                    doc = JsonDocument.Parse(File.ReadAllBytes(file));
                }
                catch (JsonException)
                {
                    doc = null;
                }
            }

            _packages[name] = doc;
            return doc;
        }
    }

    /// <summary>
    /// The texture and texel region a material of a Magma package shows (its "texture" and "region"), as the Godot
    /// client's widgets draw it: the radar's Heart, the coop icons' poses. False when the package has no such material.
    /// </summary>
    public bool Material(string package, string material, out string texture, out int u0, out int v0, out int u1, out int v1)
    {
        texture = "";
        u0 = v0 = u1 = v1 = 0;
        if (Package(package) is not { } doc || !doc.RootElement.TryGetProperty("materials", out JsonElement materials))
        {
            return false;
        }

        foreach (JsonElement m in materials.EnumerateArray())
        {
            if (m.TryGetProperty("name", out JsonElement n) && string.Equals(n.GetString(), material, StringComparison.OrdinalIgnoreCase))
            {
                return MaterialAt(m, out texture, out u0, out v0, out u1, out v1);
            }
        }

        return false;
    }

    /// <summary>The texture and region of one material element.</summary>
    public static bool MaterialAt(JsonElement m, out string texture, out int u0, out int v0, out int u1, out int v1)
    {
        texture = m.TryGetProperty("texture", out JsonElement t) ? t.GetString() ?? "" : "";
        u0 = v0 = u1 = v1 = 0;
        if (texture.Length == 0 || !m.TryGetProperty("region", out JsonElement r) || r.GetArrayLength() < 4)
        {
            return false;
        }

        u0 = r[0].GetInt32();
        v0 = r[1].GetInt32();
        u1 = r[2].GetInt32();
        v1 = r[3].GetInt32();
        return true;
    }
}
