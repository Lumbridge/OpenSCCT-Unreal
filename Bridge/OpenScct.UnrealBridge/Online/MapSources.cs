using System;
using System.IO;
using System.Linq;
using OpenScct.Maps;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// Maps from the SCCT-Maps repository, the part of the Godot client's game/Scripts/Online/MapDownloads.cs this client
/// can do: where a hosted map came from (told to the players with the setup, the server's <c>mapsource</c> line), and
/// why this client cannot fetch such a map itself. Fetching one needs the content builder (svmbuild) and the original
/// game's files, which the Unreal client does not ship: a player is offered OpenSCCT-Content's copy instead
/// (<see cref="ContentPacks"/>), and told <see cref="Problem"/> when it has none.
/// </summary>
public static class MapSources
{
    /// <summary>The original game folder (Packages, System) when SVM_GAME_ROOT names one, else empty.</summary>
    public static string GameRoot => Environment.GetEnvironmentVariable("SVM_GAME_ROOT") ?? string.Empty;

    /// <summary>
    /// Where the host's <paramref name="map"/> came from: the downloaded SCCT-Maps map that brought it, unless the
    /// original install has the map itself. Null for every other map (MapDownloads.SourceOf).
    /// </summary>
    public static MapSourceInfo? SourceOf(string map)
    {
        try
        {
            if (string.IsNullOrEmpty(map) ||
                (GameRoot.Length > 0 && File.Exists(Path.Combine(GameRoot, "Packages", "Maps", map + ".sdc"))))
            {
                return null;
            }

            InstalledMap? installed = new MapLibrary(MapLibrary.DefaultRoot).Installed()
                .FirstOrDefault(m => m.Codes.Any(c => string.Equals(c, map, StringComparison.OrdinalIgnoreCase)));
            return installed is { Commit.Length: 40 } ? new MapSourceInfo(installed.Id, installed.Commit, installed.Fingerprint) : null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /// <summary>
    /// Why this client cannot prepare a map from SCCT-Maps (MapDownloads.Problem's wording): the builder is never
    /// shipped with the Unreal client, so it is always the first reason.
    /// </summary>
    public static string Problem() => "This version of OpenSCCT cannot prepare downloaded maps. Update it from the launcher.";
}
