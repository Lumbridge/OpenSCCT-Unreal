using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;

namespace OpenScct.ReleaseDelta;

/// <summary>
/// ReleaseDelta diff &lt;old&gt; &lt;new&gt; &lt;patch&gt;   writes a patch that turns old into new
/// ReleaseDelta apply &lt;old&gt; &lt;patch&gt; &lt;new&gt;  applies it (checks both files' SHA-256)
///
/// The patch format (OSCTDIF1) is bsdiff's: a list of controls (add, copy, seek), the bytes to add to the old file's
/// bytes, and the bytes that are new, in three zlib streams. bsdiff's matching finds code that only moved or had its
/// addresses changed, which is what a recompiled exe looks like, and blocks of cooked content that moved inside a
/// container. Everything is little-endian.
///
///   "OSCTDIF1"            8 bytes
///   old size, new size    int64 each
///   old SHA-256           32 bytes
///   new SHA-256           32 bytes
///   control, diff, extra  int64 each: the length of each zlib stream
///   the three zlib streams, in that order
///
/// A control is three int64: add (bytes read from the diff stream, each added to the old file's byte at the old
/// position, both positions then advance), copy (bytes read from the extra stream, the new position advances) and seek
/// (moves the old position, may be negative). The controls run until the new file is complete.
/// </summary>
public static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            if (args.Length == 4 && args[0] == "diff-iostore")
            {
                return IoStoreDelta.TryDiff(args[1], args[2], args[3]) ? 0 : 3;
            }
            if (args.Length == 4 && args[0] == "diff")
            {
                Delta.Diff(args[1], args[2], args[3]);
                return 0;
            }
            if (args.Length == 4 && args[0] == "apply")
            {
                Delta.Apply(args[1], args[2], args[3]);
                return 0;
            }
            if (args.Length == 1 && args[0] == "selftest")
            {
                return SelfTest.Run();
            }
            Console.Error.WriteLine("usage: ReleaseDelta diff|diff-iostore <old> <new> <patch> | apply <old> <patch> <new> | selftest");
            return 2;
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException)
        {
            Console.Error.WriteLine("ReleaseDelta: " + ex.Message);
            return 1;
        }
    }
}

public static class Delta
{
    private static readonly byte[] Magic = "OSCTDIF1"u8.ToArray();
    private const int HeaderSize = 8 + 8 + 8 + 32 + 32 + 8 + 8 + 8;

    public static void Diff(string oldPath, string newPath, string patchPath)
    {
        byte[] oldData = File.ReadAllBytes(oldPath);
        byte[] newData = File.ReadAllBytes(newPath);
        // The matcher's positions and scores are int: both files together stay under 2 GB.
        if (oldData.LongLength + newData.LongLength >= int.MaxValue)
        {
            throw new ArgumentException("the two files together are 2 GB or more; they cannot be diffed");
        }
        int[] suffixes = SuffixArray.Build(oldData);

        var control = new MemoryStream();
        var diff = new MemoryStream();
        var extra = new MemoryStream();
        using (var controlZ = new ZLibStream(control, CompressionLevel.SmallestSize, leaveOpen: true))
        using (var diffZ = new ZLibStream(diff, CompressionLevel.SmallestSize, leaveOpen: true))
        using (var extraZ = new ZLibStream(extra, CompressionLevel.SmallestSize, leaveOpen: true))
        {
            BsDiff.Run(oldData, newData, suffixes, controlZ, diffZ, extraZ);
        }

        WritePatch(patchPath, oldData.LongLength, newData.LongLength, SHA256.HashData(oldData),
            SHA256.HashData(newData), control, diff, extra);
    }

    internal static void WritePatch(string patchPath, long oldLength, long newLength, byte[] oldHash, byte[] newHash,
        Stream control, Stream diff, Stream extra)
    {
        using var output = File.Create(patchPath);
        Span<byte> header = stackalloc byte[HeaderSize];
        Magic.CopyTo(header);
        BitConverter.TryWriteBytes(header[8..], oldLength);
        BitConverter.TryWriteBytes(header[16..], newLength);
        oldHash.CopyTo(header[24..56]);
        newHash.CopyTo(header[56..88]);
        BitConverter.TryWriteBytes(header[88..], control.Length);
        BitConverter.TryWriteBytes(header[96..], diff.Length);
        BitConverter.TryWriteBytes(header[104..], extra.Length);
        output.Write(header);
        control.Position = 0;
        control.CopyTo(output);
        diff.Position = 0;
        diff.CopyTo(output);
        extra.Position = 0;
        extra.CopyTo(output);
    }

    /// <summary>The reference for the launcher: reads the old file at random positions, writes the new one in order.</summary>
    public static void Apply(string oldPath, string patchPath, string newPath)
    {
        using var patch = File.OpenRead(patchPath);
        Span<byte> header = stackalloc byte[HeaderSize];
        patch.ReadExactly(header);
        if (!header[..8].SequenceEqual(Magic))
        {
            throw new InvalidDataException("not an OSCTDIF1 patch");
        }
        long oldSize = BitConverter.ToInt64(header[8..]);
        long newSize = BitConverter.ToInt64(header[16..]);
        byte[] oldHash = header[24..56].ToArray();
        byte[] newHash = header[56..88].ToArray();
        long controlLength = BitConverter.ToInt64(header[88..]);
        long diffLength = BitConverter.ToInt64(header[96..]);
        long extraLength = BitConverter.ToInt64(header[104..]);
        if (oldSize < 0 || newSize < 0 || controlLength < 0 || diffLength < 0 || extraLength < 0
            || HeaderSize + controlLength + diffLength + extraLength != patch.Length)
        {
            throw new InvalidDataException("the patch is damaged");
        }

        string temporary = newPath + ".partial";
        byte[] madeHash;
        try
        {
            // Every file is closed before the move, so the new file may replace the old one (an update in place).
            using var old = File.OpenRead(oldPath);
            if (old.Length != oldSize || !SHA256.HashData(old).AsSpan().SequenceEqual(oldHash))
            {
                throw new InvalidDataException("the old file is not the one the patch was made from");
            }

            using var controlZ = new ZLibStream(Slice(patchPath, HeaderSize, controlLength), CompressionMode.Decompress);
            using var diffZ = new ZLibStream(Slice(patchPath, HeaderSize + controlLength, diffLength), CompressionMode.Decompress);
            using var extraZ = new ZLibStream(Slice(patchPath, HeaderSize + controlLength + diffLength, extraLength), CompressionMode.Decompress);
            using var sha = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            using var output = File.Create(temporary);
            var buffer = new byte[1 << 20];
            var oldBuffer = new byte[1 << 20];
            Span<byte> triple = stackalloc byte[24];
            long newPos = 0, oldPos = 0;
            while (newPos < newSize)
            {
                controlZ.ReadExactly(triple);
                long add = BitConverter.ToInt64(triple);
                long copy = BitConverter.ToInt64(triple[8..]);
                long seek = BitConverter.ToInt64(triple[16..]);
                if (add < 0 || copy < 0 || newPos + add + copy > newSize)
                {
                    throw new InvalidDataException("the patch is damaged (control out of range)");
                }

                for (long done = 0; done < add;)
                {
                    int n = (int)Math.Min(buffer.Length, add - done);
                    diffZ.ReadExactly(buffer, 0, n);
                    // Old bytes outside the old file count as zero (bsdiff's rule).
                    Array.Clear(oldBuffer, 0, n);
                    long from = oldPos + done;
                    long start = Math.Max(from, 0), end = Math.Min(from + n, oldSize);
                    if (start < end)
                    {
                        old.Position = start;
                        old.ReadExactly(oldBuffer, (int)(start - from), (int)(end - start));
                    }
                    for (int i = 0; i < n; i++)
                    {
                        buffer[i] += oldBuffer[i];
                    }
                    output.Write(buffer, 0, n);
                    sha.AppendData(buffer, 0, n);
                    done += n;
                }
                newPos += add;
                oldPos += add;

                for (long done = 0; done < copy;)
                {
                    int n = (int)Math.Min(buffer.Length, copy - done);
                    extraZ.ReadExactly(buffer, 0, n);
                    output.Write(buffer, 0, n);
                    sha.AppendData(buffer, 0, n);
                    done += n;
                }
                newPos += copy;
                oldPos += seek;
            }
            madeHash = sha.GetHashAndReset();
        }
        catch
        {
            TryDelete(temporary);
            throw;
        }
        if (!madeHash.AsSpan().SequenceEqual(newHash))
        {
            TryDelete(temporary);
            throw new InvalidDataException("the patched file does not have the expected SHA-256");
        }
        File.Move(temporary, newPath, overwrite: true);
    }

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private static Stream Slice(string path, long offset, long length)
    {
        var stream = File.OpenRead(path);
        stream.Position = offset;
        return new BoundedStream(stream, length);
    }

    private sealed class BoundedStream(Stream inner, long length) : Stream
    {
        private long remaining = length;
        public override bool CanRead => true;
        public override bool CanSeek => false;
        public override bool CanWrite => false;
        public override long Length => throw new NotSupportedException();
        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count)
        {
            if (remaining <= 0)
            {
                return 0;
            }
            int n = inner.Read(buffer, offset, (int)Math.Min(count, remaining));
            remaining -= n;
            return n;
        }
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                inner.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}

/// <summary>Checks the suffix array against a plain sort and diff/apply round trips on made-up files.</summary>
internal static class SelfTest
{
    public static int Run()
    {
        var random = new Random(20261117);
        for (int round = 0; round < 3000; round++)
        {
            int length = random.Next(0, 64);
            int alphabet = random.Next(1, 5);
            var data = new byte[length];
            for (int i = 0; i < length; i++)
            {
                data[i] = (byte)random.Next(alphabet);
            }
            int[] expected = new int[length + 1];
            for (int i = 0; i <= length; i++)
            {
                expected[i] = i;
            }
            Array.Sort(expected, (a, b) => data.AsSpan(a).SequenceCompareTo(data.AsSpan(b)));
            if (!SuffixArray.Build(data).AsSpan().SequenceEqual(expected))
            {
                Console.Error.WriteLine($"suffix array wrong for [{string.Join(",", data)}]");
                return 1;
            }
        }

        string folder = Path.Combine(Path.GetTempPath(), "ReleaseDelta-selftest-" + Environment.ProcessId);
        Directory.CreateDirectory(folder);
        try
        {
            CheckIoStoreRoundTrip(folder, random);
            for (int round = 0; round < 40; round++)
            {
                byte[] old = new byte[random.Next(0, 200_000)];
                random.NextBytes(old);
                // The new file: old with blocks moved, changed, inserted and removed, sometimes nothing alike.
                var next = new MemoryStream();
                int pos = 0;
                while (pos < old.Length && round % 10 != 9)
                {
                    int take = Math.Min(random.Next(1, 20_000), old.Length - pos);
                    byte[] piece = old.AsSpan(pos, take).ToArray();
                    if (random.Next(4) == 0)
                    {
                        for (int i = 0; i < piece.Length; i += random.Next(1, 64))
                        {
                            piece[i] ^= (byte)random.Next(1, 256);
                        }
                    }
                    next.Write(piece);
                    if (random.Next(3) == 0)
                    {
                        byte[] inserted = new byte[random.Next(0, 3000)];
                        random.NextBytes(inserted);
                        next.Write(inserted);
                    }
                    pos += take + (random.Next(5) == 0 ? random.Next(0, 3000) : 0);
                }
                if (round % 10 == 9)
                {
                    byte[] other = new byte[random.Next(0, 50_000)];
                    random.NextBytes(other);
                    next.Write(other);
                }
                string a = Path.Combine(folder, "a"), b = Path.Combine(folder, "b"), p = Path.Combine(folder, "p"), c = Path.Combine(folder, "c");
                File.WriteAllBytes(a, old);
                File.WriteAllBytes(b, next.ToArray());
                Delta.Diff(a, b, p);
                Delta.Apply(a, p, c);
                if (!File.ReadAllBytes(c).AsSpan().SequenceEqual(next.ToArray()))
                {
                    Console.Error.WriteLine($"round trip {round} gave a different file");
                    return 1;
                }
                // In place: the new file replaces the old one.
                Delta.Apply(a, p, a);
                if (!File.ReadAllBytes(a).AsSpan().SequenceEqual(next.ToArray()) || File.Exists(a + ".partial"))
                {
                    Console.Error.WriteLine($"in-place round trip {round} failed");
                    return 1;
                }
            }
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
        Console.WriteLine("selftest passed");
        return 0;
    }

    private static void CheckIoStoreRoundTrip(string folder, Random random)
    {
        byte[][] blocks = new byte[4][];
        for (int i = 0; i < blocks.Length; i++)
        {
            blocks[i] = new byte[4096 + i];
            random.NextBytes(blocks[i]);
        }

        string MakeContainer(string name, int[] order)
        {
            string path = Path.Combine(folder, name + ".ucas");
            using var data = File.Create(path);
            using var toc = new BinaryWriter(File.Create(Path.ChangeExtension(path, ".utoc")));
            var header = new byte[144];
            "-==--==--==--==-"u8.CopyTo(header);
            header[16] = 8;
            BitConverter.GetBytes(144).CopyTo(header, 20);
            BitConverter.GetBytes(order.Length).CopyTo(header, 28);
            BitConverter.GetBytes(12).CopyTo(header, 32);
            BitConverter.GetBytes(1).CopyTo(header, 52);
            toc.Write(header);
            foreach (int i in order)
            {
                data.WriteByte((byte)(data.Position % 251)); // padding belongs to the target
                toc.Write((ulong)data.Position | ((ulong)blocks[i].Length << 40));
                toc.Write((uint)blocks[i].Length);
                data.Write(blocks[i]);
            }
            data.WriteByte(0xfe);
            return path;
        }

        string old = MakeContainer("base", [0, 1, 2]);
        string next = MakeContainer("target", [2, 3, 0, 2, 1]);
        string patch = Path.Combine(folder, "blocks.osdiff"), made = Path.Combine(folder, "result.ucas");
        if (!IoStoreDelta.TryDiff(old, next, patch) || new FileInfo(patch).Length >= new FileInfo(next).Length / 2)
            throw new InvalidDataException("IoStore delta did not reuse the moved and repeated blocks");
        Delta.Apply(old, patch, made);
        if (!File.ReadAllBytes(made).AsSpan().SequenceEqual(File.ReadAllBytes(next)))
            throw new InvalidDataException("IoStore delta did not reconstruct the target");
        File.Delete(Path.ChangeExtension(old, ".utoc"));
        if (IoStoreDelta.TryDiff(old, next, patch))
            throw new InvalidDataException("IoStore delta accepted a missing block table");
    }
}

/// <summary>bsdiff 4.3's matcher (Colin Percival, BSD licence), writing its three streams as they are made.</summary>
internal static class BsDiff
{
    public static void Run(byte[] old, byte[] @new, int[] suffixes, Stream control, Stream diff, Stream extra)
    {
        int oldSize = old.Length, newSize = @new.Length;
        int scan = 0, len = 0, pos = 0, lastScan = 0, lastPos = 0, lastOffset = 0;
        var buffer = new byte[1 << 16];
        Span<byte> triple = stackalloc byte[24];

        while (scan < newSize)
        {
            int oldScore = 0;
            int scsc;
            for (scsc = scan += len; scan < newSize; scan++)
            {
                len = Search(suffixes, old, @new.AsSpan(scan), 0, oldSize, out pos);
                for (; scsc < scan + len; scsc++)
                {
                    if (scsc + lastOffset < oldSize && old[scsc + lastOffset] == @new[scsc])
                    {
                        oldScore++;
                    }
                }
                if ((len == oldScore && len != 0) || len > oldScore + 8)
                {
                    break;
                }
                if (scan + lastOffset < oldSize && old[scan + lastOffset] == @new[scan])
                {
                    oldScore--;
                }
            }

            if (len != oldScore || scan == newSize)
            {
                int s = 0, sf = 0, lenF = 0;
                for (int i = 0; lastScan + i < scan && lastPos + i < oldSize;)
                {
                    if (old[lastPos + i] == @new[lastScan + i])
                    {
                        s++;
                    }
                    i++;
                    if (s * 2 - i > sf * 2 - lenF)
                    {
                        sf = s;
                        lenF = i;
                    }
                }

                int lenB = 0;
                if (scan < newSize)
                {
                    s = 0;
                    int sb = 0;
                    for (int i = 1; scan >= lastScan + i && pos >= i; i++)
                    {
                        if (old[pos - i] == @new[scan - i])
                        {
                            s++;
                        }
                        if (s * 2 - i > sb * 2 - lenB)
                        {
                            sb = s;
                            lenB = i;
                        }
                    }
                }

                if (lastScan + lenF > scan - lenB)
                {
                    int overlap = lastScan + lenF - (scan - lenB);
                    s = 0;
                    int ss = 0, lenS = 0;
                    for (int i = 0; i < overlap; i++)
                    {
                        if (@new[lastScan + lenF - overlap + i] == old[lastPos + lenF - overlap + i])
                        {
                            s++;
                        }
                        if (@new[scan - lenB + i] == old[pos - lenB + i])
                        {
                            s--;
                        }
                        if (s > ss)
                        {
                            ss = s;
                            lenS = i + 1;
                        }
                    }
                    lenF += lenS - overlap;
                    lenB -= lenS;
                }

                for (int done = 0; done < lenF;)
                {
                    int n = Math.Min(buffer.Length, lenF - done);
                    for (int i = 0; i < n; i++)
                    {
                        buffer[i] = (byte)(@new[lastScan + done + i] - old[lastPos + done + i]);
                    }
                    diff.Write(buffer, 0, n);
                    done += n;
                }
                int extraLength = scan - lenB - (lastScan + lenF);
                extra.Write(@new, lastScan + lenF, extraLength);

                BitConverter.TryWriteBytes(triple, (long)lenF);
                BitConverter.TryWriteBytes(triple[8..], (long)extraLength);
                BitConverter.TryWriteBytes(triple[16..], (long)(pos - lenB - (lastPos + lenF)));
                control.Write(triple);

                lastScan = scan - lenB;
                lastPos = pos - lenB;
                lastOffset = pos - scan;
            }
        }
    }

    /// <summary>The longest match of target's start among the old file's suffixes from st to en (binary search).</summary>
    private static int Search(int[] suffixes, byte[] old, ReadOnlySpan<byte> target, int st, int en, out int pos)
    {
        while (en - st >= 2)
        {
            int x = st + (en - st) / 2;
            ReadOnlySpan<byte> suffix = old.AsSpan(suffixes[x]);
            int n = Math.Min(suffix.Length, target.Length);
            if (suffix[..n].SequenceCompareTo(target[..n]) < 0)
            {
                st = x;
            }
            else
            {
                en = x;
            }
        }
        int a = old.AsSpan(suffixes[st]).CommonPrefixLength(target);
        int b = old.AsSpan(suffixes[en]).CommonPrefixLength(target);
        if (a > b)
        {
            pos = suffixes[st];
            return a;
        }
        pos = suffixes[en];
        return b;
    }
}

/// <summary>
/// The suffix array of a byte string by induced sorting (SA-IS, Nong, Zhang and Chan 2009), with an end marker
/// smaller than every byte. The result has length n + 1 and starts with n, the empty suffix: the layout bsdiff's
/// search expects.
/// </summary>
internal static class SuffixArray
{
    public static int[] Build(byte[] data)
    {
        if (data.LongLength >= int.MaxValue - 1)
        {
            throw new ArgumentException("a file of 2 GB or more cannot be diffed");
        }
        int n = data.Length + 1;
        if (n == 1)
        {
            return [0];
        }
        var s = new int[n];
        for (int i = 0; i < data.Length; i++)
        {
            s[i] = data[i] + 1;
        }
        s[n - 1] = 0;
        var sa = new int[n];
        Sais(s, 0, sa, n, 256);
        return sa;
    }

    // s[sOff .. sOff + n) is the string, its last symbol the unique smallest (0); symbols are 0..k. sa is written in
    // sa[0 .. n). In the recursion the reduced string lives at the end of sa itself, past the n1 entries being sorted.
    private static void Sais(int[] s, int sOff, int[] sa, int n, int k)
    {
        var t = new bool[n]; // true: S-type
        t[n - 1] = true;
        if (n >= 2)
        {
            t[n - 2] = false;
        }
        for (int i = n - 3; i >= 0; i--)
        {
            int a = s[sOff + i], b = s[sOff + i + 1];
            t[i] = a < b || (a == b && t[i + 1]);
        }
        bool IsLms(int i) => i > 0 && t[i] && !t[i - 1];

        var bucket = new int[k + 1];
        Buckets(s, sOff, bucket, n, k, end: true);
        Array.Fill(sa, -1, 0, n);
        for (int i = 1; i < n; i++)
        {
            if (IsLms(i))
            {
                sa[--bucket[s[sOff + i]]] = i;
            }
        }
        InduceL(t, sa, s, sOff, bucket, n, k);
        InduceS(t, sa, s, sOff, bucket, n, k);

        // The sorted LMS substrings, compacted to the front, then named.
        int n1 = 0;
        for (int i = 0; i < n; i++)
        {
            if (IsLms(sa[i]))
            {
                sa[n1++] = sa[i];
            }
        }
        Array.Fill(sa, -1, n1, n - n1);
        int name = 0, prev = -1;
        for (int i = 0; i < n1; i++)
        {
            int p = sa[i];
            bool different = false;
            for (int d = 0; d < n; d++)
            {
                if (prev == -1 || s[sOff + p + d] != s[sOff + prev + d] || t[p + d] != t[prev + d])
                {
                    different = true;
                    break;
                }
                if (d > 0 && (IsLms(p + d) || IsLms(prev + d)))
                {
                    break;
                }
            }
            if (different)
            {
                name++;
                prev = p;
            }
            sa[n1 + p / 2] = name - 1;
        }
        for (int i = n - 1, j = n - 1; i >= n1; i--)
        {
            if (sa[i] >= 0)
            {
                sa[j--] = sa[i];
            }
        }

        // The reduced string s1 is sa[n - n1 .. n): sort it, recursively when names repeat.
        int s1 = n - n1;
        if (name < n1)
        {
            Sais(sa, s1, sa, n1, name - 1);
        }
        else
        {
            for (int i = 0; i < n1; i++)
            {
                sa[sa[s1 + i]] = i;
            }
        }

        // The LMS suffixes in order, put at their bucket ends, then everything induced from them.
        Buckets(s, sOff, bucket, n, k, end: true);
        for (int i = 1, j = 0; i < n; i++)
        {
            if (IsLms(i))
            {
                sa[s1 + j++] = i;
            }
        }
        for (int i = 0; i < n1; i++)
        {
            sa[i] = sa[s1 + sa[i]];
        }
        Array.Fill(sa, -1, n1, n - n1);
        for (int i = n1 - 1; i >= 0; i--)
        {
            int j = sa[i];
            sa[i] = -1;
            sa[--bucket[s[sOff + j]]] = j;
        }
        InduceL(t, sa, s, sOff, bucket, n, k);
        InduceS(t, sa, s, sOff, bucket, n, k);
    }

    private static void Buckets(int[] s, int sOff, int[] bucket, int n, int k, bool end)
    {
        Array.Clear(bucket);
        for (int i = 0; i < n; i++)
        {
            bucket[s[sOff + i]]++;
        }
        int sum = 0;
        for (int i = 0; i <= k; i++)
        {
            sum += bucket[i];
            bucket[i] = end ? sum : sum - bucket[i];
        }
    }

    private static void InduceL(bool[] t, int[] sa, int[] s, int sOff, int[] bucket, int n, int k)
    {
        Buckets(s, sOff, bucket, n, k, end: false);
        for (int i = 0; i < n; i++)
        {
            int j = sa[i] - 1;
            if (j >= 0 && !t[j])
            {
                sa[bucket[s[sOff + j]]++] = j;
            }
        }
    }

    private static void InduceS(bool[] t, int[] sa, int[] s, int sOff, int[] bucket, int n, int k)
    {
        Buckets(s, sOff, bucket, n, k, end: true);
        for (int i = n - 1; i >= 0; i--)
        {
            int j = sa[i] - 1;
            if (j >= 0 && t[j])
            {
                sa[--bucket[s[sOff + j]]] = j;
            }
        }
    }
}
