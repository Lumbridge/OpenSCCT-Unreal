using System;
using System.Globalization;
using System.IO;
using OpenScct.Maps;
using OpenScct.Server;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Online;

/// <summary>The lobby's map line when the local player lacks the setup's map (or has another version of it).</summary>
/// <param name="Line">What the lobby shows.</param>
/// <param name="CanDownload">A Download (or Retry) button is offered.</param>
/// <param name="Running">A download is under way (a Stop button and the progress).</param>
/// <param name="Fraction">The share of what is downloading now (0 to 1).</param>
/// <param name="Problem">The line is a failure or a reason nothing can be downloaded.</param>
public readonly record struct LobbyMapFetch(string Line, bool CanDownload, bool Running, double Fraction, bool Problem);

/// <summary>
/// The setup's map when this player lacks it: the Godot client's OnlineLobby.MapFetch, StartMapFetch and
/// UpdateMapFetch. The map is taken from OpenSCCT-Content when the index has the host's version of it (the same
/// content hash); once installed the visit's catalog is read again and the server told, so the player can be ready
/// and the next round loads the map (through Unreal's runtime map level when it has no imported one).
/// </summary>
public sealed partial class OnlineBridge
{
    private ContentPacks.DownloadJob? _fetchJob;
    private int _fetchGeneration = ContentPacks.Generation;

    /// <summary>The local player's map state for the setup, or null outside a lobby.</summary>
    private MapState? LocalMapState => Client?.Lobby.Local?.Map;

    private bool LocalLacksMap => LocalMapState is MapState.Missing or MapState.Differs;

    /// <summary>The lobby's map line, or null when there is nothing to say (the player has the map).</summary>
    public LobbyMapFetch? MapFetch
    {
        get
        {
            if (Client is not { } client)
            {
                return null;
            }

            string map = client.Lobby.Setup.Map;
            if (_fetchJob is { } job && string.Equals(job.Map?.Code, map, StringComparison.OrdinalIgnoreCase))
            {
                if (job.Running)
                {
                    return new LobbyMapFetch(job.Describe(), false, true, job.Fraction, false);
                }

                if (job.Done)
                {
                    return LocalLacksMap
                        ? new LobbyMapFetch($"{job.Title} downloaded; checking it against the host's...", false, false, 1, false)
                        : null;
                }

                if (job.Error == "stopped")
                {
                    return new LobbyMapFetch($"Stopped downloading {job.Title}.", true, false, 0, false);
                }

                return new LobbyMapFetch($"Could not download {job.Title}: {job.Error}", true, false, 0, true);
            }

            if (!LocalLacksMap)
            {
                // The map is here, but no level can be drawn for it: the round would be sat out.
                return null;
            }

            if (Downloadable(client, out ContentMap? pack, out string? why))
            {
                string verb = LocalMapState == MapState.Missing ? "You don't have" : "Your copy differs from the host's for";
                return new LobbyMapFetch(
                    string.Create(CultureInfo.InvariantCulture, $"{verb} {pack!.DisplayTitle}. Download it ({pack.DownloadSize() / 1048576.0:0} MB) to play."),
                    true, false, 0, false);
            }

            return why is null
                ? new LobbyMapFetch($"Looking for {map} on OpenSCCT-Content...", false, false, 0, false)
                : new LobbyMapFetch(why, false, false, 0, true);
        }
    }

    /// <summary>
    /// The setup's map as OpenSCCT-Content has it, when downloading it would give the host's version. Otherwise
    /// <paramref name="why"/> says what the player can do, or is null while the index is still loading.
    /// </summary>
    private bool Downloadable(OnlineClient client, out ContentMap? pack, out string? why)
    {
        pack = null;
        why = null;
        string map = client.Lobby.Setup.Map;
        string? hostHash = client.Lobby.SetupContentHash != 0
            ? client.Lobby.SetupContentHash.ToString("x16", CultureInfo.InvariantCulture)
            : null;
        if (OpenScct.Maps.SteamVersusMaps.Contains(map))
        {
            why = $"Build or repair {map} from your Steam installation in the OpenSCCT launcher.";
            return false;
        }

        if (ContentPacks.Index is not { } index)
        {
            why = ContentPacks.IndexError is { } error
                ? $"You don't have the host's {map}, and the map list could not be read: {error}"
                : null;
            return false;
        }

        ContentMap? found = index.FindMap(map, hostHash);
        if (found is null)
        {
            // The host's map came from SCCT-Maps: the Godot client would build it from the player's own game files.
            why = client.Lobby.SetupSource is not null
                ? $"You don't have the host's {map}, and OpenSCCT-Content does not have it. {MapSources.Problem()}"
                : $"You don't have the host's {map}, and OpenSCCT-Content does not have it. Ask the host to share it.";
            return false;
        }

        if (hostHash is not null && !string.Equals(found.ContentHash, hostHash, StringComparison.OrdinalIgnoreCase))
        {
            why = $"The host's {found.DisplayTitle} is a different version from OpenSCCT-Content's. Ask the host to share theirs.";
            return false;
        }

        // A map converted from the player's install is found before downloaded ones, so a download would not replace it.
        if (LocalMapState == MapState.Differs && client.Maps.FindFolder(map) is { } folder &&
            !Path.GetFullPath(folder).StartsWith(Path.GetFullPath(ContentPacks.Store.SvMapRoot), StringComparison.OrdinalIgnoreCase))
        {
            why = $"Your copy of {found.DisplayTitle} (converted from your game) differs from the host's.";
            return false;
        }

        pack = found;
        return true;
    }

    /// <summary>Starts downloading the setup's map. False when there is nothing to download (MapFetch says why).</summary>
    public bool StartMapFetch()
    {
        if (Client is not { } client || _fetchJob is { Running: true } || !Downloadable(client, out ContentMap? pack, out _))
        {
            return false;
        }

        _fetchJob = ContentPacks.Download(pack!);
        Touch();
        return true;
    }

    /// <summary>Stops the lobby's map download.</summary>
    public void CancelMapFetch()
    {
        _fetchJob?.Cancel();
        Touch();
    }

    /// <summary>The lobby's download, running or finished, else null.</summary>
    public ContentPacks.DownloadJob? MapFetchJob => _fetchJob;

    /// <summary>
    /// Called every frame from <see cref="Update"/>: a map that came in makes the catalogs read their folders again,
    /// and the server is told this player now has the setup's map.
    /// </summary>
    private void UpdateMapFetch()
    {
        if (ContentPacks.Generation != _fetchGeneration)
        {
            _fetchGeneration = ContentPacks.Generation;
            // The visit's catalog is the one its sessions load with; outside a visit the next read makes it again.
            _catalog?.Maps.Refresh();
            Client?.RecheckMap();
            Touch();
        }

        if (_fetchJob is { Running: false } job && Client is { } client &&
            !string.Equals(job.Map?.Code, client.Lobby.Setup.Map, StringComparison.OrdinalIgnoreCase))
        {
            // A finished, failed or stopped download of a map the host has since changed away from.
            _fetchJob = null;
            Touch();
        }
    }
}
