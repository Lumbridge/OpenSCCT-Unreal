using System;
using System.Buffers.Binary;
using System.IO;
using System.IO.Compression;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// Reads the converted interface textures (<c>shared/ui/textures/&lt;package&gt;/&lt;name&gt;.png</c>, written by
/// <c>python -m svmbuild ui</c>) into BGRA8 rows, the order an Unreal B8G8R8A8 texture takes them. The Godot client
/// loads the same files with Godot's own image loader; the bridge has no image library, so this reads the PNGs the
/// converter writes: 8 bits a channel, grey, grey and alpha, RGB, RGBA or a palette (with its tRNS alpha), not
/// interlaced. Anything else returns null with the reason.
/// </summary>
public static class HudPng
{
    private static ReadOnlySpan<byte> Signature => [0x89, (byte)'P', (byte)'N', (byte)'G', 0x0D, 0x0A, 0x1A, 0x0A];

    /// <summary>A decoded image: Width x Height texels, 4 bytes each (blue, green, red, alpha), top row first.</summary>
    public sealed record Image(int Width, int Height, byte[] Bgra);

    public static Image? Read(ReadOnlySpan<byte> png, out string? error)
    {
        error = null;
        if (png.Length < 8 || !png[..8].SequenceEqual(Signature))
        {
            error = "not a PNG";
            return null;
        }

        int width = 0, height = 0, depth = 0, colour = 0, interlace = 0;
        byte[]? palette = null, transparency = null;
        using var idat = new MemoryStream();
        int at = 8;
        while (at + 8 <= png.Length)
        {
            int length = BinaryPrimitives.ReadInt32BigEndian(png[at..]);
            ReadOnlySpan<byte> type = png.Slice(at + 4, 4);
            if (length < 0 || at + 12 + length > png.Length)
            {
                error = "a chunk runs past the end of the file";
                return null;
            }

            ReadOnlySpan<byte> data = png.Slice(at + 8, length);
            if (type.SequenceEqual("IHDR"u8))
            {
                width = BinaryPrimitives.ReadInt32BigEndian(data);
                height = BinaryPrimitives.ReadInt32BigEndian(data[4..]);
                depth = data[8];
                colour = data[9];
                interlace = data[12];
            }
            else if (type.SequenceEqual("PLTE"u8))
            {
                palette = data.ToArray();
            }
            else if (type.SequenceEqual("tRNS"u8))
            {
                transparency = data.ToArray();
            }
            else if (type.SequenceEqual("IDAT"u8))
            {
                idat.Write(data);
            }
            else if (type.SequenceEqual("IEND"u8))
            {
                break;
            }

            at += 12 + length;
        }

        int channels = colour switch { 0 => 1, 2 => 3, 3 => 1, 4 => 2, 6 => 4, _ => 0 };
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
        {
            error = $"size {width} x {height} is not usable";
            return null;
        }

        if (depth != 8 || channels == 0 || interlace != 0 || (colour == 3 && palette is null))
        {
            error = $"bit depth {depth}, colour type {colour}, interlace {interlace} is not a kind the converter writes";
            return null;
        }

        int stride = width * channels;
        byte[] raw = new byte[(stride + 1) * height];
        idat.Position = 0;
        using (var z = new ZLibStream(idat, CompressionMode.Decompress))
        {
            int read = 0;
            while (read < raw.Length)
            {
                int n = z.Read(raw, read, raw.Length - read);
                if (n == 0)
                {
                    error = "the image data ends early";
                    return null;
                }

                read += n;
            }
        }

        // Undo the row filters in place (PNG spec 9.2), each row against the one above it.
        for (int y = 0; y < height; y++)
        {
            int row = y * (stride + 1);
            byte filter = raw[row];
            for (int x = 0; x < stride; x++)
            {
                int i = row + 1 + x;
                int left = x >= channels ? raw[i - channels] : 0;
                int up = y > 0 ? raw[i - stride - 1] : 0;
                int upLeft = y > 0 && x >= channels ? raw[i - stride - 1 - channels] : 0;
                raw[i] = filter switch
                {
                    1 => (byte)(raw[i] + left),
                    2 => (byte)(raw[i] + up),
                    3 => (byte)(raw[i] + ((left + up) >> 1)),
                    4 => (byte)(raw[i] + Paeth(left, up, upLeft)),
                    _ => raw[i],
                };
            }
        }

        byte[] bgra = new byte[width * height * 4];
        for (int y = 0; y < height; y++)
        {
            int row = y * (stride + 1) + 1;
            for (int x = 0; x < width; x++)
            {
                int s = row + x * channels, d = (y * width + x) * 4;
                byte r, g, b, a = 255;
                switch (colour)
                {
                    case 0:
                        r = g = b = raw[s];
                        break;
                    case 4:
                        r = g = b = raw[s];
                        a = raw[s + 1];
                        break;
                    case 2:
                        (r, g, b) = (raw[s], raw[s + 1], raw[s + 2]);
                        break;
                    case 3:
                        int p = raw[s] * 3;
                        (r, g, b) = p + 2 < palette!.Length ? (palette[p], palette[p + 1], palette[p + 2]) : ((byte)0, (byte)0, (byte)0);
                        a = transparency is not null && raw[s] < transparency.Length ? transparency[raw[s]] : (byte)255;
                        break;
                    default:
                        (r, g, b, a) = (raw[s], raw[s + 1], raw[s + 2], raw[s + 3]);
                        break;
                }

                bgra[d] = b;
                bgra[d + 1] = g;
                bgra[d + 2] = r;
                bgra[d + 3] = a;
            }
        }

        return new Image(width, height, bgra);
    }

    private static int Paeth(int a, int b, int c)
    {
        int p = a + b - c;
        int pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    }
}
