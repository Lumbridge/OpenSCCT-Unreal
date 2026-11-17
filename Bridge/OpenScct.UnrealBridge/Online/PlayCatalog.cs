using System;
using System.Collections.Generic;
using System.IO;
using OpenScct.Content;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// The maps an online client plays, loaded the way a playing machine needs them (ported from OpenSCCT's
/// game/Scripts/Online/PlayCatalog.cs): every installed map is registered with a <see cref="PlayMapLoader"/> (made
/// on first use), so a round's session loads its map with the movement, camera, device animation and combat layers.
/// Without it the plain load has no movement service and the pawns stand in their bind pose. The online lobby stays
/// the built-in empty level.
/// </summary>
public sealed class PlayCatalog
{
    private readonly Dictionary<string, PlayMapLoader> _loaders = new(StringComparer.OrdinalIgnoreCase);
    private readonly object _gate = new();

    /// <summary>A catalog over the configured content (<see cref="BridgeSession.Configure"/>) and the downloaded packs.</summary>
    public PlayCatalog()
    {
        Maps = new MapCatalog([.. MapCatalog.CreateDefault().Roots]);

        // A map installed while the game runs plays through the same loader.
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

    /// <summary>The catalog the client sessions load with.</summary>
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

    /// <summary>
    /// True when the map has the converted content a round is played on (the same test the loaders above are
    /// registered by; the Godot client calls it CanRender).
    /// </summary>
    public bool CanPlay(string map) =>
        Maps.FindFolder(map) is { } folder && Directory.Exists(Path.Combine(folder, "render"));
}
