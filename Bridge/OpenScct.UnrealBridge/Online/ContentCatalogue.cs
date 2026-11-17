using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using OpenScct.Maps;
using OpenScct.Server;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// The Maps &amp; Content page's rows (Source/OpenSCCT/Menu/SScctMenuContent.cpp), ported from the Godot client's
/// game/Scripts/UI/ContentScreen.cs: every playable map of the OpenSCCT-Content index with what this PC has of it,
/// the downloaded maps the index no longer lists (so they can still be removed), and the shared packs. One line per
/// row, fields separated by tabs (see <see cref="Lines"/>); Unreal filters and draws them.
/// </summary>
public static class ContentCatalogue
{
    /// <summary>
    /// The rows, one per line: kind ("map" or "shared"), key (map code or pack id), title, category, size in bytes,
    /// state (available, downloaded, update, ingame, unlisted, notneeded), version, job (empty, running, done,
    /// stopped, failed), the job's line, the job's fraction (0 to 1), picture (a PNG path or empty), modes (comma
    /// separated), detail (the row's tooltip, lines separated by '|').
    /// </summary>
    public static string Lines()
    {
        ContentIndex? index = ContentPacks.Index;
        ContentStore store = ContentPacks.Store;
        IReadOnlyList<InstalledContent> installed = store.Installed();
        var text = new StringBuilder();

        var packs = installed.Where(p => !p.IsShared).GroupBy(p => p.Code, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(g => g.Key, g => g.Last(), StringComparer.OrdinalIgnoreCase);
        if (index is not null)
        {
            foreach (ContentMap map in index.PlayableMaps().OrderBy(m => ContentIndex.CategoryRank(m.Category))
                .ThenBy(m => m.DisplayTitle, StringComparer.OrdinalIgnoreCase))
            {
                InstalledContent? pack = packs.GetValueOrDefault(map.Code);
                packs.Remove(map.Code);
                string state = pack is not null ? (store.Has(map) ? "downloaded" : "update") : InGameFiles(map.Code) ? "ingame" : "available";
                var detail = new List<string> { "Map code " + map.Code };
                if (map.Version.Length > 0)
                {
                    detail.Add("Version " + map.Version);
                }

                if (map.Modes.Count > 0)
                {
                    detail.Add("Modes: " + string.Join(", ", map.Modes));
                }

                if (pack is not null)
                {
                    detail.Add("Downloaded " + pack.InstalledAt.ToLocalTime().ToString("g", CultureInfo.InvariantCulture));
                }

                Row(text, "map", map.Code, map.DisplayTitle, map.Category, map.Full.Size, state,
                    state == "downloaded" ? pack!.Version : map.Version, ContentPacks.Job(map.Code), Picture(map.Code), string.Join(',', map.Modes), detail);
            }
        }

        foreach (InstalledContent pack in packs.Values.OrderBy(p => p.Code, StringComparer.OrdinalIgnoreCase))
        {
            string category = pack.Id.Split('/') is [var first, _] ? first : string.Empty;
            Row(text, "map", pack.Code, pack.Code, category, 0, "unlisted", pack.Version, ContentPacks.Job(pack.Code), Picture(pack.Code), string.Empty,
                ["Map code " + pack.Code, "Downloaded " + pack.InstalledAt.ToLocalTime().ToString("g", CultureInfo.InvariantCulture)]);
        }

        bool own = ContentPacks.UsesOwnSharedContent;
        if (index is not null)
        {
            foreach (ContentSharedPack pack in index.Shared)
            {
                bool has = store.HasShared(pack);
                bool older = !has && installed.Any(c => c.IsShared && string.Equals(c.Code, pack.Id, StringComparison.OrdinalIgnoreCase));
                string state = has ? "downloaded" : older ? "update" : own ? "notneeded" : "available";
                Row(text, "shared", pack.Id, pack.Title.Length > 0 ? pack.Title : pack.Id, string.Empty, pack.Download.Size, state, pack.Version,
                    ContentPacks.SharedJob(pack.Id), string.Empty, string.Empty,
                    [pack.Contents.Count > 0 ? "Holds " + string.Join(", ", pack.Contents) : pack.Id,
                        has || older ? string.Empty : own ? "Your converted game files have it." : "Maps download it when they need it."]);
            }
        }

        return text.ToString();
    }

    /// <summary>Where the content comes from and where downloads go (two lines).</summary>
    public static string Library()
    {
        string own = ContentPaths.Root is { } root
            ? "Converted from your game: " + root
            : ContentPacks.UsesOwnSharedContent
                ? "Converted from your game"
                : "No converted game files: the shared packs supply what maps need.";
        return own + "\nDownloads: " + ContentPaths.PacksFolder;
    }

    /// <summary>The page's status line: loading, the index's failure, or how many maps there are and are downloaded.</summary>
    public static string Summary()
    {
        ContentIndex? index = ContentPacks.Index;
        if (index is null)
        {
            return ContentPacks.IndexError is { } e
                ? "Could not read the content list (" + e + "). Maps already on this PC are listed; press REFRESH to try again."
                : "Loading the content list...";
        }

        int count = index.PlayableMaps().Count();
        int have = ContentPacks.Store.Installed().Count(c => !c.IsShared);
        return string.Create(CultureInfo.InvariantCulture, $"{count} maps on OpenSCCT-Content, {have} downloaded.") + (index.Problem is { } p ? " " + p : string.Empty);
    }

    /// <summary>
    /// Removes a downloaded map. Returns 1 removed, 0 no download of that code is on this PC, -1 its download is
    /// running, -2 failed (<paramref name="error"/> says why).
    /// </summary>
    public static int Remove(string code, out string error)
    {
        error = string.Empty;
        if (ContentPacks.Job(code) is { Running: true })
        {
            error = "its download is running";
            return -1;
        }

        if (ContentPacks.Store.Installed().All(c => c.IsShared || !string.Equals(c.Code, code, StringComparison.OrdinalIgnoreCase)))
        {
            error = "it was not downloaded";
            return 0;
        }

        try
        {
            ContentPacks.Remove(code);
            return 1;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            error = e.Message;
            return -2;
        }
    }

    /// <summary>Starts a shared pack's download. Returns 1 started (or running), 0 the index has no such pack, -1 the index is not read yet.</summary>
    public static int StartShared(string id)
    {
        if (ContentPacks.Index is not { } index)
        {
            return -1;
        }

        if (index.FindShared(id) is not { } pack)
        {
            return 0;
        }

        ContentPacks.DownloadShared(pack);
        return 1;
    }

    private static bool InGameFiles(string code) =>
        ContentPaths.SvMap is { } svmap && File.Exists(Path.Combine(svmap, code, "manifest.json"));

    /// <summary>The map's wide picture, else its list picture, from the catalogs this client plays with; empty without one.</summary>
    private static string Picture(string code)
    {
        try
        {
            MapCatalogEntry? entry = OnlineBridge.Instance.Catalog.Maps.GetCatalogEntry(code);
            foreach (string? path in new[] { entry?.BannerImage, entry?.PreviewImage })
            {
                if (!string.IsNullOrEmpty(path) && File.Exists(path))
                {
                    return path;
                }
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            // A map folder that cannot be read has no picture; the row still lists it.
        }

        return string.Empty;
    }

    private static void Row(StringBuilder text, string kind, string key, string title, string category, long size, string state, string version,
        ContentPacks.DownloadJob? job, string picture, string modes, IEnumerable<string> detail)
    {
        string jobState = job is null ? string.Empty : job.Running ? "running" : job.Done ? "done" : job.Error == "stopped" ? "stopped" : "failed";
        string jobLine = job is null ? string.Empty : job.Running ? job.DescribeBrief() : job.Error is { } e ? e : job.Describe();
        text.Append(kind).Append('\t').Append(Clean(key)).Append('\t').Append(Clean(title)).Append('\t').Append(Clean(category)).Append('\t')
            .Append(size.ToString(CultureInfo.InvariantCulture)).Append('\t').Append(state).Append('\t').Append(Clean(version)).Append('\t')
            .Append(jobState).Append('\t').Append(Clean(jobLine)).Append('\t')
            .Append((job?.Fraction ?? 0).ToString("0.###", CultureInfo.InvariantCulture)).Append('\t').Append(Clean(picture)).Append('\t')
            .Append(Clean(modes)).Append('\t').Append(Clean(string.Join('|', detail.Where(d => d.Length > 0)))).Append('\n');
    }

    private static string Clean(string value) => value.Replace('\t', ' ').Replace('\r', ' ').Replace('\n', ' ');
}
