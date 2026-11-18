using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;

namespace OpenScct.ReleaseDelta;

/// <summary>Match the already-compressed blocks in a .ucas by their bytes, even when the cook reorders them.</summary>
internal static class IoStoreDelta
{
    private readonly record struct Block(long Offset, int Size);

    // FIoStoreTocHeader and FIoStoreTocCompressedBlockEntry, as written by UE 5.8.
    // Unknown layouts and partitioned containers can use the release's full-file download.
    private static Block[]? ReadBlocks(string container, long length)
    {
        string toc = Path.ChangeExtension(container, ".utoc");
        if (!File.Exists(toc)) return null;
        using var file = File.OpenRead(toc);
        Span<byte> h = stackalloc byte[144];
        if (file.ReadAtLeast(h, h.Length, throwOnEndOfStream: false) < h.Length ||
            !h[..16].SequenceEqual("-==--==--==--==-"u8) || h[16] != 8 ||
            U32(h, 20) != h.Length || U32(h, 32) != 12 || U32(h, 52) != 1)
        {
            return null;
        }

        // Chunk IDs (12 bytes each), offset/length pairs (10), then the two perfect-hash arrays.
        long start = h.Length + 22L * U32(h, 24) + 4L * (U32(h, 84) + (long)U32(h, 96));
        uint count = U32(h, 28);
        if (count > Array.MaxLength || start + 12L * count > file.Length) return null;
        file.Position = start;
        var blocks = new Block[count];
        Span<byte> entry = stackalloc byte[12];
        for (int i = 0; i < blocks.Length; i++)
        {
            file.ReadExactly(entry);
            long offset = U32(entry, 0) | ((long)entry[4] << 32);
            int size = (int)(U32(entry, 5) & 0x00ffffff);
            if (size == 0 || offset + size > length) return null;
            blocks[i] = new Block(offset, size);
        }
        Array.Sort(blocks, (a, b) => a.Offset.CompareTo(b.Offset));
        for (int i = 1; i < blocks.Length; i++)
        {
            if (blocks[i].Offset < blocks[i - 1].Offset + blocks[i - 1].Size) return null;
        }
        return blocks;
    }

    private static uint U32(ReadOnlySpan<byte> bytes, int start) =>
        BinaryPrimitives.ReadUInt32LittleEndian(bytes[start..]);

    public static bool TryDiff(string oldPath, string newPath, string patchPath)
    {
        using var old = File.OpenRead(oldPath);
        using var next = File.OpenRead(newPath);
        var from = ReadBlocks(oldPath, old.Length);
        var to = ReadBlocks(newPath, next.Length);
        if (from is null || to is null) return false;

        byte[] buffer = new byte[65536];
        var positions = new Dictionary<string, long>(from.Length, StringComparer.Ordinal);
        foreach (Block b in from)
        {
            if (buffer.Length < b.Size) Array.Resize(ref buffer, b.Size);
            old.Position = b.Offset;
            old.ReadExactly(buffer, 0, b.Size);
            positions.TryAdd(Convert.ToHexString(SHA256.HashData(buffer.AsSpan(0, b.Size))), b.Offset);
        }

        // The extra stream can be large for an unrelated cook. Spool it to disk, keeping RAM bounded.
        using var extra = new FileStream(Path.GetTempFileName(), FileMode.Truncate, FileAccess.ReadWrite,
            FileShare.None, 65536, FileOptions.DeleteOnClose);
        using var control = new MemoryStream();
        using var diff = new MemoryStream();
        long matched = 0;
        using (var controls = new ZLibStream(control, CompressionLevel.Optimal, leaveOpen: true))
        using (var literals = new ZLibStream(extra, CompressionLevel.Optimal, leaveOpen: true))
        {
            long oldCursor = 0, add = 0, copy = 0;
            void Flush(long nextOld)
            {
                Span<byte> triple = stackalloc byte[24];
                BinaryPrimitives.WriteInt64LittleEndian(triple, add);
                BinaryPrimitives.WriteInt64LittleEndian(triple[8..], copy);
                BinaryPrimitives.WriteInt64LittleEndian(triple[16..], nextOld - (oldCursor + add));
                controls.Write(triple);
                oldCursor = nextOld;
                add = copy = 0;
            }
            void CopyUntil(long end)
            {
                while (next.Position < end)
                {
                    int take = (int)Math.Min(buffer.Length, end - next.Position);
                    next.ReadExactly(buffer, 0, take);
                    literals.Write(buffer, 0, take);
                    copy += take;
                }
            }

            foreach (Block b in to)
            {
                CopyUntil(b.Offset); // padding and any unindexed bytes must round-trip too
                if (buffer.Length < b.Size) Array.Resize(ref buffer, b.Size);
                next.ReadExactly(buffer, 0, b.Size);
                var bytes = buffer.AsSpan(0, b.Size);
                if (positions.TryGetValue(Convert.ToHexString(SHA256.HashData(bytes)), out long offset))
                {
                    if (copy != 0 || oldCursor + add != offset) Flush(offset);
                    add += b.Size;
                    matched += b.Size;
                }
                else
                {
                    literals.Write(bytes);
                    copy += b.Size;
                }
            }
            CopyUntil(next.Length);
            if (add != 0 || copy != 0) Flush(oldCursor + add);
        }

        // bsdiff adds these zeros to the reused bytes. The released launcher already supports this encoding.
        Array.Clear(buffer);
        using (var zeros = new ZLibStream(diff, CompressionLevel.Optimal, leaveOpen: true))
        {
            for (long remaining = matched; remaining > 0;)
            {
                int take = (int)Math.Min(buffer.Length, remaining);
                zeros.Write(buffer, 0, take);
                remaining -= take;
            }
        }
        old.Position = next.Position = 0;
        Delta.WritePatch(patchPath, old.Length, next.Length, SHA256.HashData(old), SHA256.HashData(next),
            control, diff, extra);
        return true;
    }
}
