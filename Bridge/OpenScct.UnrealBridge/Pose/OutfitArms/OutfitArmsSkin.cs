using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge.Pose.OutfitArms;

/// <summary>
/// The first-person arms in the map's outfit (spec/additions/map-outfits.md), a port of the Godot client's
/// OutfitSkins.Arms and PawnOutfits.FirstPersonArms: the arms material of a view model redrawn from the dressed body
/// (the outfit's own model, or the stock mesh with the outfit's body texture) through a <see cref="SurfaceTransfer"/>,
/// so the merc's sleeves and gloves, and the arms of a spy who wears a model of the map's own, match the body the
/// others see. The texture is made once per pair of meshes and written as a PNG under the temp folder; Unreal loads it
/// by path as it loads the camo textures.
/// </summary>
public static class OutfitArmsSkin
{
    // As the Godot client draws it.
    private const int Size = 256;

    /// <summary>The arms material of the merc's view models.</summary>
    public const string MercArms = "SWeaponTex.1DEF_01";

    /// <summary>The arms material of the spy's first-person arms.</summary>
    public const string SpyArms = "SPersoTextures.Shadw_agent";

    private static readonly string[] s_body = ["SPersoTextures.Shadw_agent", "SPersoTextures.DEF_01_A_Body"];
    private static readonly string[] s_head = ["SPersoTextures.Shadw_agent_2", "SPersoTextures.DEF_01_A_Face"];

    private static readonly Dictionary<string, string?> s_made = new(StringComparer.OrdinalIgnoreCase);
    private static readonly Dictionary<string, BindMesh> s_meshes = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// The PNG (full path) of the arms of the view model <paramref name="package"/>.<paramref name="mesh"/> for a team
    /// (0 spies, 1 mercs) as the map dresses it, or null when the arms keep their look: no outfit, a spy without a
    /// model of the map's own (the body texture fits those arms as it is), a merc outfit that leaves the body alone,
    /// the added rifles (their arms are a model of their own), or a mesh that cannot be read.
    /// </summary>
    public static string? For(PawnOutfits outfits, int kind, string package, string mesh)
    {
        ArgumentNullException.ThrowIfNull(outfits);
        if (outfits.Of(kind) is not { } team || string.IsNullOrEmpty(package) || string.IsNullOrEmpty(mesh) ||
            package.Equals("OpenScctRifles", StringComparison.OrdinalIgnoreCase) ||
            PawnOutfits.StockPath(package + "." + mesh, outfits.SharedRoot) is not { } viewModel)
        {
            return null;
        }

        bool ownModel = team.Model is not null;
        if (kind == 0 ? !ownModel : !ownModel && team.BodyTexture is null)
        {
            return null;
        }

        string body = team.Model?.Path ?? team.Stock.Path;
        var diffuse = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach ((string? texture, string[] surfaces) in new[] { (team.BodyTexture, s_body), (team.HeadTexture, s_head) })
        {
            if (texture is not null)
            {
                foreach (string surface in surfaces)
                {
                    diffuse[surface] = texture;
                }
            }
        }

        string arms = kind == 0 ? SpyArms : MercArms;
        string key = string.Join("|", Path.GetFullPath(viewModel), arms, Path.GetFullPath(body), string.Join(",", diffuse.Values));
        lock (s_made)
        {
            if (!s_made.TryGetValue(key, out string? made))
            {
                try
                {
                    made = Make(viewModel, arms, body, diffuse, key);
                }
                catch (Exception e) when (e is InvalidDataException or IOException or UnauthorizedAccessException)
                {
                    made = null;
                }

                s_made[key] = made;
            }

            return made;
        }
    }

    private static string? Make(string viewModelGlb, string armsMaterial, string bodyGlb, Dictionary<string, string> diffuse, string key)
    {
        var transfer = new SurfaceTransfer(Mesh(viewModelGlb), Mesh(bodyGlb));
        int index = transfer.Target.MaterialIndex(armsMaterial);
        if (index < 0 || transfer.Map(index, Size) is not { IsEmpty: false } map)
        {
            return null;
        }

        string viewFolder = Path.GetDirectoryName(Path.GetFullPath(viewModelGlb)) ?? ".";
        string bodyFolder = Path.GetDirectoryName(Path.GetFullPath(bodyGlb)) ?? ".";
        TexelImage? own = transfer.Target.Images[index] is { } uri ? TexelImage.Load(Path.Combine(viewFolder, uri), Size) : null;
        var source = new TexelImage?[transfer.Source.Materials.Length];
        for (int m = 0; m < source.Length; m++)
        {
            string? path = diffuse.TryGetValue(transfer.Source.Materials[m], out string? over) ? over
                : transfer.Source.Images[m] is { } u ? Path.Combine(bodyFolder, u) : null;
            source[m] = path is null ? null : TexelImage.Load(path, 0);
        }

        byte[] texels = map.Bake((m, uv) => source[m]?.Sample(uv) ?? Rgba.Grey(0.5f), i => own?.Texel(i) ?? Rgba.Grey(0.5f));
        if (own is not null)
        {
            // The suit's colour on the view model's own alpha (its mask, where the material has one).
            for (int i = 0; i < Size * Size; i++)
            {
                texels[(i * 4) + 3] = own.Texel(i).A;
            }
        }

        string folder = Path.Combine(Path.GetTempPath(), "OpenSCCT", "outfit-arms");
        Directory.CreateDirectory(folder);
        string file = Path.Combine(folder, "arms_" + Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(key)), 0, 8) + ".png");
        File.WriteAllBytes(file, Png(texels, Size, Size));
        return file;
    }

    // The body's mesh serves every view model's arms: read each file once.
    private static BindMesh Mesh(string path)
    {
        string full = Path.GetFullPath(path);
        if (!s_meshes.TryGetValue(full, out BindMesh? mesh))
        {
            s_meshes[full] = mesh = BindMesh.Load(full);
        }

        return mesh;
    }

    // An RGBA8 PNG, rows unfiltered.
    private static byte[] Png(byte[] rgba, int width, int height)
    {
        using var raw = new MemoryStream();
        using (var z = new ZLibStream(raw, CompressionLevel.Fastest, leaveOpen: true))
        {
            for (int y = 0; y < height; y++)
            {
                z.WriteByte(0);
                z.Write(rgba, y * width * 4, width * 4);
            }
        }

        using var file = new MemoryStream();
        file.Write([0x89, (byte)'P', (byte)'N', (byte)'G', 0x0D, 0x0A, 0x1A, 0x0A]);
        Span<byte> header = stackalloc byte[13];
        header.Clear();
        BinaryPrimitives.WriteInt32BigEndian(header, width);
        BinaryPrimitives.WriteInt32BigEndian(header[4..], height);
        header[8] = 8;
        header[9] = 6;
        Chunk(file, "IHDR", header);
        Chunk(file, "IDAT", raw.ToArray());
        Chunk(file, "IEND", []);
        return file.ToArray();
    }

    private static void Chunk(Stream file, string type, ReadOnlySpan<byte> data)
    {
        Span<byte> word = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(word, data.Length);
        file.Write(word);
        Span<byte> name = stackalloc byte[4];
        Encoding.ASCII.GetBytes(type, name);
        file.Write(name);
        file.Write(data);
        uint crc = Crc(data, Crc(name, 0xFFFFFFFFu)) ^ 0xFFFFFFFFu;
        BinaryPrimitives.WriteUInt32BigEndian(word, crc);
        file.Write(word);
    }

    private static uint Crc(ReadOnlySpan<byte> data, uint crc)
    {
        foreach (byte b in data)
        {
            crc ^= b;
            for (int k = 0; k < 8; k++)
            {
                crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
            }
        }

        return crc;
    }

    /// <summary>A decoded image sampled in managed code (wrapping, bilinear).</summary>
    private sealed class TexelImage
    {
        private readonly int _width;
        private readonly int _height;
        private readonly byte[] _bgra;

        private TexelImage(int width, int height, byte[] bgra)
        {
            _width = width;
            _height = height;
            _bgra = bgra;
        }

        /// <summary>The PNG at <paramref name="path"/>, resized to <paramref name="size"/> squared unless 0; null when missing or unreadable.</summary>
        public static TexelImage? Load(string path, int size)
        {
            if (!File.Exists(path) || HudPng.Read(File.ReadAllBytes(path), out _) is not { } image || image.Width <= 0 || image.Height <= 0)
            {
                return null;
            }

            var loaded = new TexelImage(image.Width, image.Height, image.Bgra);
            if (size <= 0 || (image.Width == size && image.Height == size))
            {
                return loaded;
            }

            var resized = new byte[size * size * 4];
            for (int y = 0; y < size; y++)
            {
                for (int x = 0; x < size; x++)
                {
                    Rgba c = loaded.Sample(new Vector2((x + 0.5f) / size, (y + 0.5f) / size));
                    int o = ((y * size) + x) * 4;
                    resized[o] = c.B;
                    resized[o + 1] = c.G;
                    resized[o + 2] = c.R;
                    resized[o + 3] = c.A;
                }
            }

            return new TexelImage(size, size, resized);
        }

        /// <summary>Texel <paramref name="index"/> of the image in row order.</summary>
        public Rgba Texel(int index)
        {
            int o = index * 4;
            return o >= 0 && o + 3 < _bgra.Length ? new Rgba(_bgra[o + 2], _bgra[o + 1], _bgra[o], _bgra[o + 3]) : default;
        }

        public Rgba Sample(Vector2 uv)
        {
            float fx = (uv.X * _width) - 0.5f, fy = (uv.Y * _height) - 0.5f;
            int x0 = (int)MathF.Floor(fx), y0 = (int)MathF.Floor(fy);
            float tx = fx - x0, ty = fy - y0;
            Rgba a = Texel((Wrap(y0, _height) * _width) + Wrap(x0, _width));
            Rgba b = Texel((Wrap(y0, _height) * _width) + Wrap(x0 + 1, _width));
            Rgba c = Texel((Wrap(y0 + 1, _height) * _width) + Wrap(x0, _width));
            Rgba d = Texel((Wrap(y0 + 1, _height) * _width) + Wrap(x0 + 1, _width));
            return new Rgba(Mix(a.R, b.R, c.R, d.R, tx, ty), Mix(a.G, b.G, c.G, d.G, tx, ty), Mix(a.B, b.B, c.B, d.B, tx, ty),
                Mix(a.A, b.A, c.A, d.A, tx, ty));
        }

        private static byte Mix(byte a, byte b, byte c, byte d, float tx, float ty) =>
            (byte)Math.Clamp((int)MathF.Round(((a + ((b - a) * tx)) * (1f - ty)) + ((c + ((d - c) * tx)) * ty)), 0, 255);

        private static int Wrap(int i, int size) => ((i % size) + size) % size;
    }
}
