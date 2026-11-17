using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using OpenScct.Net;
using OpenScct.Net.Discovery;
using OpenScct.Net.Scoreboards;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Social;
using OnlineDemo = OpenScct.Net.Discovery.DemoEntry;

namespace OpenScct.UnrealBridge.Demo;

/// <summary>A demo file on this machine, as the Demos page lists it (the Godot client's DemoLibrary DemoEntry).</summary>
public sealed record LocalDemo(string Path, DemoHeader Header, DemoIndex Index, long Bytes, bool Cached)
{
    public string FileName => System.IO.Path.GetFileName(Path);

    public int DurationSeconds => (int)(Index.DurationTicks / (double)Math.Max(1, Header.TickRate));

    public bool Playable => Header.ProtocolHash == DemoBridge.Protocol;
}

/// <summary>Where the online list stands (DemoLibraryInfoBlock.OnlineState, EScctDemoOnlineState).</summary>
public enum DemoOnlineState
{
    NoMaster = 0,
    SignedOut = 1,
    NotLoaded = 2,
    Loading = 3,
    Failed = 4,
    Ready = 5,
}

/// <summary>
/// The demo library behind Unreal's Demos page, ported from the Godot client's game/Scripts/Play/Demo/DemoLibrary.cs
/// and game/Scripts/UI/DemosScreen*.cs with the widgets taken out: the demos on this computer (Saved/Demos, kept, and
/// Saved/Demos/Cache, downloads that expire after seven days), keeping a download, deleting a recording, the master's
/// recent online demos page by page, and downloading one (checked against its size and SHA-256 before it is shown).
/// A finished download meant for watching is handed back once through <see cref="TakeWatchPath"/>.
/// <para>Game thread only; the master calls and the download run on worker threads and are picked up by
/// <see cref="Pump"/>, which every read calls.</para>
/// </summary>
public static class DemoLibrary
{
    /// <summary>The master keeps demos seven days; the download cache is cleaned on the same schedule.</summary>
    private static readonly TimeSpan CacheLife = TimeSpan.FromDays(7);

    private static readonly List<LocalDemo> s_local = new();
    private static DemoListResponse? s_online;
    private static Task<DemoListResponse>? s_listing;
    private static string s_listingError = string.Empty;
    private static Task<string>? s_download;
    private static CancellationTokenSource? s_downloadCancel;
    private static bool s_keepDownload;
    private static string s_downloadMap = string.Empty;
    private static long s_downloadBytes, s_downloadSize;
    private static string? s_watchPath;
    private static string s_status = string.Empty;
    private static bool s_statusError;
    private static int s_revision = 1;
    private static bool s_pruned;

    public static int Revision => s_revision;

    /// <summary>Kept demos: the folder the recordings are saved to (Unreal's Saved/Demos).</summary>
    public static string? SavedFolder => DemoSaving.Folder;

    /// <summary>Downloaded demos, until kept or seven days old.</summary>
    public static string? CacheFolder => SavedFolder is { } saved ? System.IO.Path.Combine(saved, "Cache") : null;

    public static IReadOnlyList<LocalDemo> Local => s_local;

    public static IReadOnlyList<OnlineDemo> Online => s_online?.Demos ?? (IReadOnlyList<OnlineDemo>)[];

    public static string Status => s_status;

    public static bool StatusIsError => s_statusError;

    public static string ListingError => s_listingError;

    public static bool Downloading => s_download is not null;

    /// <summary>A download meant for watching has finished and waits for <see cref="TakeWatchPath"/>.</summary>
    public static bool WatchReady => s_watchPath is not null;

    // ---- the demos on this computer -----------------------------------------------------------------------------

    /// <summary>Reads both folders again (DemosScreen.ReadLocal): kept ones first, then downloads not also kept, newest first in each.</summary>
    public static void RefreshLocal()
    {
        s_local.Clear();
        if (SavedFolder is not { } saved)
        {
            SetStatus("There is no demo folder yet.", error: true);
            return;
        }

        try
        {
            Directory.CreateDirectory(saved);
            if (!s_pruned)
            {
                s_pruned = true;
                PruneCache();
            }

            List<LocalDemo> kept = List(saved, cached: false);
            var names = new HashSet<string>(kept.Select(e => e.FileName), StringComparer.OrdinalIgnoreCase);
            s_local.AddRange(kept);
            s_local.AddRange(List(CacheFolder!, cached: true).Where(e => !names.Contains(e.FileName)));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            SetStatus("Could not read the demos folder: " + e.Message, error: true);
        }

        Touch();
    }

    /// <summary>The demos in <paramref name="folder"/>, newest first; unreadable files are skipped.</summary>
    private static List<LocalDemo> List(string folder, bool cached)
    {
        var list = new List<LocalDemo>();
        if (!Directory.Exists(folder))
        {
            return list;
        }

        foreach (string file in Directory.EnumerateFiles(folder, "*" + DemoFile.Extension))
        {
            try
            {
                (DemoHeader header, DemoIndex index) = DemoFile.ReadInfo(file);
                list.Add(new LocalDemo(file, header, index, new FileInfo(file).Length, cached));
            }
            catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or EndOfStreamException or ArgumentException)
            {
                // A match still recording has no index yet; it shows once it is finished.
                Console.Error.WriteLine($"SVM_DEMO skipped {file}: {e.Message}");
            }
        }

        list.Sort(static (a, b) => b.Header.StartUnixMs.CompareTo(a.Header.StartUnixMs));
        return list;
    }

    /// <summary>Moves a download into the kept folder (it then outlives the cache), with its training data.</summary>
    public static bool Keep(int index)
    {
        if (index < 0 || index >= s_local.Count || !s_local[index].Cached)
        {
            return false;
        }

        LocalDemo entry = s_local[index];
        try
        {
            KeepFile(entry.Path);
            SetStatus($"Saved {entry.Header.Map} to your library. It will stay on this computer.");
            RefreshLocal();
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            SetStatus("Could not keep the demo: " + e.Message, error: true);
            return false;
        }
    }

    private static string KeepFile(string cachedPath)
    {
        string saved = SavedFolder ?? throw new IOException("there is no demo folder.");
        Directory.CreateDirectory(saved);
        string target = System.IO.Path.Combine(saved, System.IO.Path.GetFileName(cachedPath));
        if (!string.Equals(System.IO.Path.GetFullPath(cachedPath), System.IO.Path.GetFullPath(target), StringComparison.OrdinalIgnoreCase))
        {
            string training = TrainingDataFile.PathForDemo(cachedPath);
            if (File.Exists(training)) File.Move(training, TrainingDataFile.PathForDemo(target), overwrite: true);
            File.Move(cachedPath, target, overwrite: true);
        }

        return target;
    }

    /// <summary>Deletes one local recording and its training companion (the page has asked first).</summary>
    public static bool Delete(int index)
    {
        if (index < 0 || index >= s_local.Count)
        {
            return false;
        }

        LocalDemo entry = s_local[index];
        if (DemoBridge.Player is { } playing && string.Equals(playing.Path, entry.Path, StringComparison.OrdinalIgnoreCase))
        {
            SetStatus("That demo is being watched.", error: true);
            return false;
        }

        try
        {
            File.Delete(TrainingDataFile.PathForDemo(entry.Path));
            File.Delete(entry.Path);
            s_local.RemoveAt(index);
            SetStatus($"Deleted {entry.Header.Map} from this computer.");
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            SetStatus("Could not delete the demo: " + e.Message, error: true);
            return false;
        }
    }

    /// <summary>Deletes cached downloads older than seven days.</summary>
    private static void PruneCache()
    {
        if (CacheFolder is not { } cache || !Directory.Exists(cache))
        {
            return;
        }

        DateTime cutoff = DateTime.UtcNow - CacheLife;
        foreach (string file in Directory.EnumerateFiles(cache, "*" + DemoFile.Extension))
        {
            try
            {
                if (File.GetLastWriteTimeUtc(file) < cutoff)
                {
                    File.Delete(TrainingDataFile.PathForDemo(file));
                    File.Delete(file);
                }
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Console.Error.WriteLine($"SVM_DEMO cache file {file} not removed: {e.Message}");
            }
        }
    }

    /// <summary>The final scoreboard a recording carries (format 3 and later), or null.</summary>
    public static RoundScoreboard? Scoreboard(int index) => index >= 0 && index < s_local.Count ? s_local[index].Index.Scoreboard : null;

    // ---- recent online demos ------------------------------------------------------------------------------------

    private static OnlineMaster? Master => OnlineBridge.Existing?.Master;

    public static DemoOnlineState OnlineState =>
        Master?.Url is null ? DemoOnlineState.NoMaster :
        Master.AccountToken is null ? DemoOnlineState.SignedOut :
        s_listing is not null ? DemoOnlineState.Loading :
        s_listingError.Length > 0 ? DemoOnlineState.Failed :
        s_online is not null ? DemoOnlineState.Ready : DemoOnlineState.NotLoaded;

    public static int OnlinePage => s_online?.Page ?? 0;

    public static int OnlineTotal => s_online?.Total ?? 0;

    public static int OnlinePages => Math.Max(1, (int)Math.Ceiling(OnlineTotal / (double)Math.Max(1, s_online?.PageSize ?? 1)));

    /// <summary>Asks the master for a page of recent demos (DemosScreen.LoadRecent); signed in only.</summary>
    public static void RequestOnline(int page)
    {
        Pump();
        if (s_listing is not null || Master?.Url is null || Master.AccountToken is null)
        {
            Touch();
            return;
        }

        s_listingError = string.Empty;
        int wanted = Math.Max(0, page);
        s_listing = AccountService.Call(client => client.DemosAsync(wanted));
        Touch();
    }

    /// <summary>
    /// Downloads a recent demo into the cache (DemosScreen.StartDownload), or uses the saved or cached copy.
    /// <paramref name="keep"/> moves it into the kept folder when done; otherwise it is handed to
    /// <see cref="TakeWatchPath"/> for watching. False when it cannot start (another download, an unknown row).
    /// </summary>
    public static bool Download(int index, bool keep)
    {
        Pump();
        if (s_download is not null || index < 0 || index >= Online.Count || SavedFolder is not { } saved)
        {
            return false;
        }

        OnlineDemo demo = Online[index];
        try
        {
            string safeMap = string.Concat(demo.Map.Select(c => System.IO.Path.GetInvalidFileNameChars().Contains(c) ? '_' : c));
            string name = $"{demo.Id}-{safeMap}{DemoFile.Extension}";
            string savedPath = System.IO.Path.Combine(saved, name);
            if (File.Exists(savedPath))
            {
                if (keep) SetStatus(demo.Map + " is already saved in your library.");
                else s_watchPath = savedPath;
                Touch();
                return true;
            }

            string path = System.IO.Path.Combine(CacheFolder!, name);
            s_keepDownload = keep;
            s_downloadMap = demo.Map;
            Interlocked.Exchange(ref s_downloadBytes, 0);
            Interlocked.Exchange(ref s_downloadSize, demo.Size);
            SetStatus("Preparing " + demo.Map + "…");
            s_downloadCancel = new CancellationTokenSource();
            CancellationToken token = s_downloadCancel.Token;
            s_download = AccountService.Call(client => DownloadAsync(client, demo, path, token));
            Touch();
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            SetStatus("Could not download the demo: " + e.Message, error: true);
            return false;
        }
    }

    /// <summary>A download progress from 0 to 100, or -1 when the size is not known.</summary>
    public static int DownloadPercent
    {
        get
        {
            long received = Interlocked.Read(ref s_downloadBytes);
            long total = Interlocked.Read(ref s_downloadSize);
            return s_download is null ? -1 : total > 0 ? (int)Math.Min(100, received * 100 / total) : -1;
        }
    }

    /// <summary>A finished download to watch, once; null when there is none.</summary>
    public static string? TakeWatchPath()
    {
        Pump();
        string? path = s_watchPath;
        s_watchPath = null;
        return path;
    }

    /// <summary>Picks up the master's answers and the download (DemosScreen._Process and ProcessDownload).</summary>
    public static void Pump()
    {
        if (s_listing is { IsCompleted: true } listing)
        {
            s_listing = null;
            if (listing.IsCompletedSuccessfully)
            {
                s_online = listing.Result;
                s_listingError = string.Empty;
            }
            else
            {
                s_listingError = AccountService.ErrorOf(listing);
            }

            Touch();
        }

        if (s_download is not { } download)
        {
            return;
        }

        if (!download.IsCompleted)
        {
            long received = Interlocked.Read(ref s_downloadBytes);
            long total = Interlocked.Read(ref s_downloadSize);
            string text = received == 0 ? "Preparing " + s_downloadMap + "…"
                : total > 0 && received >= total ? "Verifying " + s_downloadMap + "…"
                : $"Downloading {s_downloadMap} · {Bytes(received)}" + (total > 0 ? " / " + Bytes(total) : string.Empty);
            if (text != s_status)
            {
                SetStatus(text);
            }

            return;
        }

        s_download = null;
        s_downloadCancel?.Dispose();
        s_downloadCancel = null;
        if (!download.IsCompletedSuccessfully)
        {
            SetStatus("Could not download the demo: " + AccountService.ErrorOf(download), error: true);
            return;
        }

        if (!s_keepDownload)
        {
            SetStatus(string.Empty);
            s_watchPath = download.Result;
            RefreshLocal();
            return;
        }

        try
        {
            KeepFile(download.Result);
            RefreshLocal();
            SetStatus("Saved " + s_downloadMap + " to your library. It will stay on this computer.");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            RefreshLocal();
            SetStatus("Downloaded, but could not keep the demo: " + e.Message, error: true);
        }
    }

    /// <summary>Reuses a verified download, otherwise fetches and checks a temporary file before it is shown (DemosScreen.Download).</summary>
    private static async Task<string> DownloadAsync(MasterClient client, OnlineDemo demo, string path, CancellationToken token)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromMinutes(5));
        token = timeout.Token;
        if (File.Exists(path) && (demo.Size <= 0 || new FileInfo(path).Length == demo.Size) &&
            string.Equals(await HashOf(path, token).ConfigureAwait(false), demo.Sha256, StringComparison.OrdinalIgnoreCase))
        {
            return path;
        }

        DemoDownloadResponse link = await client.DemoDownloadAsync(demo.Id, token).ConfigureAwait(false);
        long expectedSize = link.Size > 0 ? link.Size : demo.Size;
        Interlocked.Exchange(ref s_downloadSize, expectedSize);
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);
        string part = path + "." + Guid.NewGuid().ToString("N") + ".part";
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
            using HttpResponseMessage response = await http.GetAsync(link.Url, HttpCompletionOption.ResponseHeadersRead, token).ConfigureAwait(false);
            response.EnsureSuccessStatusCode();
            await using (Stream input = await response.Content.ReadAsStreamAsync(token).ConfigureAwait(false))
            await using (FileStream file = File.Create(part))
            {
                byte[] buffer = new byte[65536];
                int count;
                while ((count = await input.ReadAsync(buffer, token).ConfigureAwait(false)) != 0)
                {
                    await file.WriteAsync(buffer.AsMemory(0, count), token).ConfigureAwait(false);
                    Interlocked.Add(ref s_downloadBytes, count);
                }
            }

            if (expectedSize > 0 && new FileInfo(part).Length != expectedSize)
                throw new InvalidDataException("the file did not match its expected size.");
            string expected = link.Sha256.Length > 0 ? link.Sha256 : demo.Sha256;
            if (!string.Equals(await HashOf(part, token).ConfigureAwait(false), expected, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("the file did not match its checksum.");
            token.ThrowIfCancellationRequested();
            File.Move(part, path, overwrite: true);
            return path;
        }
        finally
        {
            try { if (File.Exists(part)) File.Delete(part); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { Console.Error.WriteLine("SVM_DEMO temporary download could not be removed: " + e.Message); }
        }
    }

    private static async Task<string> HashOf(string path, CancellationToken token)
    {
        await using FileStream file = File.OpenRead(path);
        return Convert.ToHexStringLower(await SHA256.HashDataAsync(file, token).ConfigureAwait(false));
    }

    /// <summary>The online demo was recorded on this build's protocol (DemosScreen.RecentCard).</summary>
    public static bool OnlinePlayable(OnlineDemo demo) => string.Equals(demo.NetVersion, MasterApi.Hex(DemoBridge.Protocol), StringComparison.OrdinalIgnoreCase);

    // ---- names --------------------------------------------------------------------------------------------------

    /// <summary>A game mode's name by its number (DemosScreen.ModeName).</summary>
    public static string ModeName(int mode) => mode switch
    {
        1 => "Extraction", 2 => "Sabotage", 3 => "Neutralization", 4 => "Disk Hunt", 6 => "Deathmatch", 7 => "Merc Deathmatch", _ => "Story",
    };

    /// <summary>A local demo's mode name (DemosScreen.LocalMode).</summary>
    public static string LocalModeName(DemoHeader header) => DemoPlayback.ModeKey(header.GameOptions) switch
    {
        "DiskHunt" => "Disk Hunt", "MercDeathMatch" => "Merc Deathmatch", "DeathMatch" => "Deathmatch", var mode => mode,
    };

    /// <summary>"1.2 MB" or "640 KB".</summary>
    public static string Bytes(long bytes) => bytes >= 1024 * 1024
        ? string.Create(CultureInfo.InvariantCulture, $"{bytes / (1024d * 1024d):0.0} MB")
        : string.Create(CultureInfo.InvariantCulture, $"{Math.Max(1, bytes / 1024)} KB");

    /// <summary>A scoreboard's outcome line (DemosScreen.RenderHistory): who won, practice, objectives.</summary>
    public static string Outcome(RoundScoreboard board)
    {
        string outcome = board.Partial ? "Incomplete" : board.Winner == 0 ? "Spies win" : board.Winner == 1 ? "Mercenaries win" : "Round stopped";
        if (board.Practice) outcome += " · Practice";
        if (board.ObjectivesNeeded > 0) outcome += $" · objectives {board.ObjectivesTaken} of {board.ObjectivesNeeded}";
        return outcome;
    }

    private static void SetStatus(string text, bool error = false)
    {
        s_status = text;
        s_statusError = error && text.Length > 0;
        Touch();
    }

    private static void Touch() => s_revision++;
}
