using System;
using System.Collections.Generic;
using System.IO;
using OpenScct.Content;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// The maps a hub visit plays, loaded the way a playing machine needs them: the online <c>PlayCatalog</c> over more
/// roots (the Godot client's PlayCatalog takes its extra map roots the same way). The hub map is not converted
/// content: it is kept in the OpenSCCT checkout as <c>fixtures/hub</c>, which the default roots do not include for a
/// bridge loaded from the Unreal project, so the folder that holds it comes first. Every map found is registered with
/// a <see cref="PlayMapLoader"/>, so the hub loads with movement, camera and animation (without it the pawns stand in
/// their bind pose).
/// </summary>
public sealed class HubPlayCatalog
{
    private readonly Dictionary<string, PlayMapLoader> _loaders = new(StringComparer.OrdinalIgnoreCase);
    private readonly object _gate = new();

    public HubPlayCatalog(IReadOnlyList<string> extraRoots)
    {
        ArgumentNullException.ThrowIfNull(extraRoots);
        var roots = new List<string>(extraRoots);
        roots.AddRange(MapCatalog.CreateDefault().Roots);
        Maps = new MapCatalog([.. roots]);
        Maps.FolderBuilder = (id, folder) => Directory.Exists(Path.Combine(folder, "render"))
            ? (world, isClient) => Loader(id).Load(world, isClient)
            : null;
        foreach (MapListEntry entry in Maps.ListMaps())
        {
            if (entry.Folder is { } folder && Directory.Exists(Path.Combine(folder, "render")))
            {
                string id = entry.Id;
                Maps.Register(id, (world, isClient) => Loader(id).Load(world, isClient));
            }
        }
    }

    /// <summary>The catalog the hub's client session (and a hub this PC hosts) load with.</summary>
    public MapCatalog Maps { get; }

    /// <summary>The play loader of a map (its SVMap parsed once).</summary>
    public PlayMapLoader Loader(string map)
    {
        lock (_gate)
        {
            if (!_loaders.TryGetValue(map, out PlayMapLoader? loader))
            {
                string folder = Maps.FindFolder(map) ?? throw new FileNotFoundException($"Map '{map}' is not installed.");
                loader = new PlayMapLoader(SvMap.Load(folder), string.Empty, PlayMapLoader.FindCharsDir(folder));
                _loaders[map] = loader;
            }

            return loader;
        }
    }

    /// <summary>True when the map has the render content a visit is played on (the Godot client's CanRender).</summary>
    public bool CanPlay(string map) =>
        Maps.FindFolder(map) is { } folder && Directory.Exists(Path.Combine(folder, "render"));
}
