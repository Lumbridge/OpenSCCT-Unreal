using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json;
using System.Runtime.CompilerServices;
using Image = OpenScct.UnrealBridge.Pose.OutfitArms.OutfitArmsSkin.TexelImage;

namespace OpenScct.UnrealBridge.Pose.OutfitArms;

/// <summary>Stock cool-form heat and EMF in RG, baked into the drawn mesh's UV layout.</summary>
internal sealed class OutfitVisionMasks
{
    private const int Size = 256;
    private static readonly ConditionalWeakTable<PawnOutfits, OutfitVisionMasks> Packs = new();
    private readonly Dictionary<string, Maps> _skins = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, Image?> _images = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<(int, int), TexelMap> _maps = new();
    private readonly Dictionary<int, SurfaceTransfer> _pairs = new();
    private readonly Dictionary<(int, bool, string, bool, bool), string?> _files = new();
    private readonly string _root;
    private sealed record Maps(string? Heat, int Type, string? Emf, bool Invert);
    private static readonly Rgba Flat = new(64, 255, 0, 255);

    private OutfitVisionMasks(PawnOutfits outfits)
    {
        _root = Path.Combine(outfits.SharedRoot ?? "", "render");
        string path = Path.Combine(_root, "vision.json");
        if (!File.Exists(path)) return;
        using var doc = JsonDocument.Parse(File.ReadAllBytes(path));
        foreach (string group in new[] { "bases", "modifiers" })
        {
            if (!doc.RootElement.TryGetProperty(group, out var entries)) continue;
            foreach (var entry in entries.EnumerateObject())
            {
                var heat = Settings(entry.Value, "heat");
                var emf = Settings(entry.Value, "emf");
                var maps = new Maps(heat is { } h && (!h.TryGetProperty("useTexture", out var use) || use.GetBoolean())
                        ? Texture(h) : null, heat is { } ht && ht.TryGetProperty("type", out var type) ? type.GetInt32() : 0,
                    emf is { } e ? Texture(e) : null, emf is { } em && em.TryGetProperty("invert", out var inv) && inv.GetBoolean());
                _skins[entry.Name] = maps;
                int first = entry.Name.IndexOf('.');
                if (first >= 0 && group == "bases") _skins.TryAdd(entry.Name[..first] + entry.Name[entry.Name.LastIndexOf('.')..], maps);
            }
        }
    }

    private static JsonElement? Settings(JsonElement entry, string name) =>
        entry.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Object ? value : null;
    private static string? Texture(JsonElement value) => value.TryGetProperty("texture", out var t) ? t.GetString() : null;
    private static string Key(string name) => string.Concat(name.Where(char.IsLetterOrDigit)).ToLowerInvariant();

    public static string? For(PawnOutfits outfits, int kind, bool wearsModel, string slot, bool wet, bool localSpy)
    {
        var pack = Packs.GetValue(outfits, static o => new OutfitVisionMasks(o));
        var key = (kind, wearsModel, slot, wet, localSpy);
        if (!pack._files.TryGetValue(key, out string? path))
            pack._files[key] = path = pack.Bake(outfits, kind, wearsModel, slot, wet, localSpy);
        return path;
    }

    private Image? Load(string? uri)
    {
        if (uri is null) return null;
        if (!_images.TryGetValue(uri, out var image)) _images[uri] = image = Image.Load(Path.Combine(_root, uri), 0);
        return image;
    }

    private string? Bake(PawnOutfits outfits, int kind, bool wearsModel, string slot, bool wet, bool localSpy)
    {
        if (_skins.Count == 0 || outfits.StockCharacter(kind) is not { } stock) return null;
        var source = OutfitArmsSkin.Mesh(stock.Path);
        var other = wearsModel ? outfits.Of(kind)?.Model : null;
        var target = other is null ? source : OutfitArmsSkin.Mesh(other.Path);
        string key = Key(slot.StartsWith("MI_", StringComparison.OrdinalIgnoreCase) ? slot[3..] : slot);
        int index = Array.FindIndex(target.Materials, m => Key(m) == key);
        if (index < 0) return null;

        var inputs = source.Materials.Select(name =>
        {
            string skin = wet ? name.ToLowerInvariant() switch
            {
                "spersotextures.shadw_agent" => localSpy ? "SPersoTextures.alpha_ATT02.wet_lit_ATT_SI" : "SPersoTextures.alpha_ATT02.wet_lit_ATT",
                "spersotextures.shadw_agent_2" => "SPersoTextures.alpha_ATT02.wet_lit_ATT_2",
                "spersotextures.def_01_a_body" => "SPersoTextures.alpha_DEF01.lit_def_fx",
                _ => name
            } : name;
            var maps = _skins.GetValueOrDefault(skin);
            return (Heat: Load(maps?.Heat), Type: maps?.Type ?? 0, Emf: Load(maps?.Emf), Invert: maps?.Invert == true);
        }).ToArray();
        Rgba Sample(int material, Vector2 uv)
        {
            var maps = inputs[material];
            static float Lum(Rgba c) => (.3f*c.R + .59f*c.G + .11f*c.B) / 255f;
            float heat = .25f, emf = 1f;
            if (maps.Heat is { } h) { heat = Lum(h.Sample(uv)); if (maps.Type != 0) heat = (heat + 1f) * .25f; }
            if (maps.Emf is { } e) { emf = Lum(e.Sample(uv)); if (maps.Invert) emf = 1f - emf; }
            return new Rgba(Rgba.ToByte(heat), Rgba.ToByte(emf), 0, 255);
        }

        byte[] pixels;
        if (other is not null)
        {
            if (!_pairs.TryGetValue(kind, out var transfer)) _pairs[kind] = transfer = new SurfaceTransfer(target, source);
            if (!_maps.TryGetValue((kind,index), out var map)) _maps[(kind,index)] = map = transfer.Map(index, Size);
            if (map.IsEmpty) return null;
            pixels = map.Bake(Sample, _ => Flat);
        }
        else
        {
            if (inputs[index].Heat is null && inputs[index].Emf is null) return null;
            pixels = new byte[Size * Size * 4];
            for (int i = 0; i < Size * Size; i++)
            {
                Rgba c = Sample(index, new Vector2((i % Size + .5f) / Size, (i / Size + .5f) / Size));
                pixels[i*4] = c.R; pixels[i*4+1] = c.G; pixels[i*4+3] = 255;
            }
        }
        byte[] png = OutfitArmsSkin.Png(pixels, Size, Size);
        string folder = Path.Combine(Path.GetTempPath(), "OpenSCCT", "outfit-vision");
        Directory.CreateDirectory(folder);
        string file = Path.Combine(folder, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(png)) + ".png");
        if (!File.Exists(file))
        {
            string tmp = file + "." + Guid.NewGuid().ToString("N");
            try { File.WriteAllBytes(tmp, png); File.Move(tmp, file, overwrite: true); }
            finally { File.Delete(tmp); }
        }
        return file;
    }
}
