using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using OpenScct.Server;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>MapBrowserApi.GetText's kinds.</summary>
public enum MapBrowserText
{
    /// <summary>
    /// The map browser's groups (MapCatalog.GetMapFilters: All, Enhanced maps, Stock maps, Community maps, 3v3
    /// optimised maps), one per line: "name TAB title TAB description TAB map ids (comma separated, host order)".
    /// The name is what the settings keep (active_map_playlist); the title is what the list shows.
    /// </summary>
    Groups = 0,

    /// <summary>
    /// What the search box looks through and what a tile's tooltip adds, one host map per line: "Id TAB search words
    /// (name, sort name, authors, tags) TAB description TAB credit (By A, B, version V)". Empty fields are kept.
    /// </summary>
    Details = 1,
}

/// <summary>The map browser's table (Source/OpenSCCT/Runtime/ScctMapBrowserApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MapBrowserApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
}

/// <summary>
/// What the map lists of the menus (Create Session, the lobby's match settings, the hub laptop) ask about the
/// catalog beyond the hostable maps themselves: the groups a list can be narrowed to and the words its search box
/// looks through. The groups are the shared catalog's own (MapCatalog.GetMapFilters), so a group holds the same maps
/// here as in the Godot client. A table of its own, so the host table keeps its layout.
/// </summary>
public static unsafe class MapBrowserExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(MapBrowserApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetText = &GetText;
        return ApiVersion;
    }

    /// <summary>Copies a text (<see cref="MapBrowserText"/>) into <paramref name="buffer"/>. Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, TextOf(OnlineBridge.Instance.Catalog.Maps, (MapBrowserText)kind));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>A text as <c>GetText</c> copies it.</summary>
    public static string TextOf(MapCatalog maps, MapBrowserText kind)
    {
        ArgumentNullException.ThrowIfNull(maps);
        return kind switch
        {
            MapBrowserText.Groups => Groups(maps),
            MapBrowserText.Details => Details(maps),
            _ => string.Empty,
        };
    }

    /// <summary>The group's name as the list shows it ("All" is every installed map; MapPickerView.PlaylistTitle).</summary>
    public static string Title(MapPlaylist group) => group.Name == MapCatalog.AllMapsPlaylist ? "All installed maps" : group.Name;

    private static string Groups(MapCatalog maps)
    {
        var text = new StringBuilder();
        foreach (MapPlaylist group in maps.GetMapFilters())
        {
            text.Append(Clean(group.Name)).Append('\t').Append(Clean(Title(group))).Append('\t').Append(Clean(group.Description))
                .Append('\t').Append(string.Join(',', group.Maps)).Append('\n');
        }

        return text.ToString();
    }

    private static string Details(MapCatalog maps)
    {
        var text = new StringBuilder();
        foreach (MapCatalogEntry entry in maps.GetHostMaps())
        {
            var words = new List<string> { entry.Name, entry.SortName };
            words.AddRange(entry.Authors);
            words.AddRange(entry.Tags);
            string credit = entry.Authors.Count > 0
                ? "By " + string.Join(", ", entry.Authors) + (entry.Version is { } version ? ", version " + version : string.Empty)
                : string.Empty;
            text.Append(entry.Id).Append('\t').Append(Clean(string.Join(" | ", words))).Append('\t').Append(Clean(entry.Description))
                .Append('\t').Append(Clean(credit)).Append('\n');
        }

        return text.ToString();
    }

    private static string Clean(string? text) =>
        (text ?? string.Empty).Replace('\t', ' ').Replace('\n', ' ').Replace('\r', ' ');
}
