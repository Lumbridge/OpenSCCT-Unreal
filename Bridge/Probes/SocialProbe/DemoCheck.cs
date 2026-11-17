using System;
using System.IO;
using System.Linq;
using OpenScct.Net;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Demo;

/// <summary>
/// SocialProbe demo [file or folder]: the demo table's record size, the saving choice's texts, and every .scctdemo in
/// the folder (Saved/Demos by default) read back with OpenSCCT's own demo reader: header, length, players, markers.
/// A file that cannot be read fails the check (a match that was not finished leaves no index).
/// </summary>
internal static class DemoCheck
{
    public static int Run(string[] args)
    {
        int failures = 0;
        void Check(bool ok, string what)
        {
            if (!ok)
            {
                failures++;
                Console.WriteLine("FAILED: " + what);
            }
        }

        unsafe
        {
            Console.WriteLine($"record sizes: DemoInfoBlock {sizeof(DemoInfoBlock)}");
            Check(sizeof(DemoInfoBlock) == 8, "DemoInfoBlock must be 8 bytes, as Source/OpenSCCT/Runtime/ScctDemoApi.h asserts");
        }

        string target = args.Length > 0 ? args[0] : Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "Saved", "Demos"));

        // The choice as Unreal hands it over, and what the pages then read.
        DemoSaving.Configure(null, offline: false, online: false);
        Console.WriteLine($"off:     online '{DemoSaving.OnlineStatus}' | offline '{DemoSaving.OfflineStatus}' | flags {DemoSaving.Flags()}");
        Check(DemoSaving.OnlineStatus == DemoSaving.OnlineOffText, "online saving off reads as off");
        int revision = DemoSaving.Revision();
        string folder = Directory.Exists(target) ? target : Path.GetDirectoryName(target) ?? ".";
        DemoSaving.Configure(folder, offline: true, online: true);
        Console.WriteLine($"on:      online '{DemoSaving.OnlineStatus}' | offline '{DemoSaving.OfflineStatus}' | flags {DemoSaving.Flags()} | folder {DemoSaving.Folder}");
        Check(DemoSaving.OnlineStatus == DemoSaving.OnlineOnText, "online saving on reads as on");
        Check(DemoSaving.Flags() == (DemoFlags.RecordOffline | DemoFlags.RecordOnline), "both choices are in the flags");
        Check(DemoSaving.Revision() != revision, "the revision moves when the choice changes");

        string[] files = File.Exists(target) ? [target] :
            Directory.Exists(target) ? Directory.GetFiles(target, "*" + DemoFile.Extension).OrderBy(f => f).ToArray() : [];
        Console.WriteLine($"{files.Length} demo(s) in {target}");
        Check(files.Length > 0, "there is at least one demo to read");
        foreach (string file in files)
        {
            long size = new FileInfo(file).Length;
            try
            {
                (DemoHeader header, DemoIndex index) = DemoFile.ReadInfo(file);
                double seconds = index.DurationTicks / (double)Math.Max(1, header.TickRate);
                Console.WriteLine($"{Path.GetFileName(file)}: {size} bytes, format {header.FormatVersion}, map {header.Map}, server '{header.ServerName}', " +
                    $"build {header.GameVersion}, options '{header.GameOptions}', {seconds:F1} s ({index.Packets} packets, {index.RawBytes} raw bytes), " +
                    $"players [{string.Join(", ", index.Players.Select(p => $"{p.Name} ({(p.Team == 0 ? "spy" : p.Team == 1 ? "merc" : p.Team.ToString())})"))}], " +
                    $"{index.Markers.Count} markers{(index.Markers.Count > 0 ? ": " + string.Join(", ", index.Markers.Take(6).Select(m => $"{m.Kind}@{m.Tick}")) : "")}, " +
                    $"scoreboard {(index.Scoreboard is null ? "none" : "yes")}");
                Check(size > 0 && index.Packets > 0, $"{Path.GetFileName(file)} has packets");
            }
            catch (Exception e) when (e is IOException or InvalidDataException or ArgumentException)
            {
                Console.WriteLine($"{Path.GetFileName(file)}: {size} bytes, unreadable: {e.Message}");
                Check(false, $"{Path.GetFileName(file)} reads back");
            }
        }

        Console.WriteLine(failures == 0 ? "demo check passed" : $"demo check: {failures} failure(s)");
        return failures == 0 ? 0 : 1;
    }
}
