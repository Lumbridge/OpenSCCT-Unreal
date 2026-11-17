using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenScct.Content;
using OpenScct.Server;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// The map folders this client looks in besides the defaults of <see cref="MapCatalog.CreateDefault"/> (the content's
/// <c>svmap</c>, the downloaded packs' <c>svmap</c> and <c>maps</c> next to the bridge): a port of the roots of
/// OpenSCCT's game/Scripts/Maps/MapLocator.cs. Searched first, every folder Unreal names (<c>-ScctMaps=&lt;dir&gt;</c>,
/// as Godot's <c>--svm-maps</c>); searched last, <c>maps</c> next to the game's executable and the content's own
/// <c>maps</c> (where svmbuild puts TestBox and the synthetic maps). A map found in any of them plays offline and
/// online without rebuilding the client, as a downloaded one does: the play catalog and a local match search them,
/// and Unreal builds a level for a map it has not imported from the map's render files.
/// <para>A folder named here may hold map folders, or be a map folder itself (one holding a manifest.json): then
/// its parent is searched, which is the only way the catalog finds a map, by its folder name.</para>
/// </summary>
public static class MapFolders
{
    private static readonly object Gate = new();
    private static string[] s_first = [];
    private static string[] s_last = [];
    private static int s_revision;

    /// <summary>Goes up whenever the folders change, so a catalog made before knows to be made again.</summary>
    public static int Revision => System.Threading.Volatile.Read(ref s_revision);

    /// <summary>
    /// Sets the folders from Unreal: <paramref name="first"/> searched before the defaults, <paramref name="last"/>
    /// after them. Folders that do not exist are kept (a map may be copied there later) but say so in the count.
    /// Returns how many of the folders exist.
    /// </summary>
    public static int Set(IEnumerable<string> first, IEnumerable<string> last)
    {
        string[] before = Normalise(first);
        string[] after = Normalise(last);
        lock (Gate)
        {
            s_first = before;
            s_last = after;
            System.Threading.Interlocked.Increment(ref s_revision);
        }

        int existing = 0;
        foreach (string folder in Concat(before, after))
        {
            existing += Directory.Exists(folder) ? 1 : 0;
        }

        return existing;
    }

    /// <summary>The folders searched before the defaults.</summary>
    public static IReadOnlyList<string> First
    {
        get
        {
            lock (Gate)
            {
                return s_first;
            }
        }
    }

    /// <summary>The folders searched after the defaults, ending with the configured content's own <c>maps</c>.</summary>
    public static IReadOnlyList<string> Last
    {
        get
        {
            var list = new List<string>();
            lock (Gate)
            {
                list.AddRange(s_last);
            }

            if (ContentPaths.Root is { } content)
            {
                list.Add(Path.Combine(content, "maps"));
            }

            return list;
        }
    }

    /// <summary>Every root a catalog of this client searches, in order: the first folders, the defaults, the last.</summary>
    public static string[] AllRoots()
    {
        var roots = new List<string>(First);
        roots.AddRange(MapCatalog.CreateDefault().Roots);
        roots.AddRange(Last);
        return [.. roots];
    }

    /// <summary>The extra roots a local match searches before its defaults (PlaySettings.MapRoots).</summary>
    public static string[] ExtraRoots()
    {
        var roots = new List<string>(First);
        roots.AddRange(Last);
        return [.. roots];
    }

    /// <summary>True when a map folder has the render files a level is drawn from.</summary>
    public static bool HasRender(string folder) => File.Exists(Path.Combine(folder, "render", "world.glb"));

    private static string[] Normalise(IEnumerable<string> folders)
    {
        var list = new List<string>();
        foreach (string folder in folders ?? [])
        {
            if (string.IsNullOrWhiteSpace(folder))
            {
                continue;
            }

            string full = Path.GetFullPath(folder.Trim().Trim('"'));
            // A map folder itself: the catalog finds maps by their folder name inside a root.
            if (File.Exists(Path.Combine(full, SvMapHash.ManifestFile)) && Path.GetDirectoryName(full) is { } parent)
            {
                full = parent;
            }

            if (!list.Contains(full, StringComparer.OrdinalIgnoreCase))
            {
                list.Add(full);
            }
        }

        return [.. list];
    }

    private static IEnumerable<string> Concat(string[] a, string[] b)
    {
        foreach (string s in a)
        {
            yield return s;
        }

        foreach (string s in b)
        {
            yield return s;
        }
    }
}
