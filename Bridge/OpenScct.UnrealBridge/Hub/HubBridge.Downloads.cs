using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using OpenScct.Maps;
using OpenScct.Server.Hub;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// Maps downloaded from the hub (ported from the Godot client's HubService.WatchDownloads and HubLaptopPanel's
/// download rows): a pad's laptop also lists the maps of OpenSCCT-Content the player lacks, and picking one downloads
/// it; a player queued on a pad whose map is not installed gets it downloaded while waiting. A map that came in makes
/// the visit's catalog read its folders again, so it can be picked and played at once.
/// </summary>
public sealed partial class HubBridge
{
    private int _hubContentGeneration = ContentPacks.Generation;
    private string? _laptopDownload;

    /// <summary>The download of the queued pad's map for the queue line, else null.</summary>
    public string? QueueDownloadNote { get; private set; }

    /// <summary>The queue line with the download of the pad's map under it.</summary>
    public string QueueTextOf(HubClient client) =>
        client.Queue.PadId is null ? string.Empty : QueueDownloadNote is { } note ? client.Queue.Text + "\n" + note : client.Queue.Text;

    /// <summary>The maps of the content index the player lacks, for the laptop (the index is asked for when it is old).</summary>
    public IReadOnlyList<ContentMap> LaptopDownloads()
    {
        ContentPacks.RefreshIndex();
        if (ContentPacks.Index is not { } index)
        {
            return [];
        }

        var maps = Catalog.Maps;
        return index.PlayableMaps().Where(m => maps.FindMap(m.Code) is null).ToList();
    }

    /// <summary>"Aquarius - download 31 MB".</summary>
    public static string DownloadTitle(ContentMap map) =>
        string.Create(CultureInfo.InvariantCulture, $"{map.DisplayTitle} - download {Math.Max(1, map.DownloadSize() / 1048576)} MB");

    /// <summary>Picking a map the player lacks at the laptop: starts its download. False when the index does not have it.</summary>
    private bool StartLaptopDownload(string mapId)
    {
        if (Catalog.Maps.FindMap(mapId) is not null || ContentPacks.Find(mapId) is not { } map)
        {
            return false;
        }

        ContentPacks.DownloadJob job = ContentPacks.Download(map);
        _laptopDownload = map.Code;
        _laptopStatus = $"You do not have {map.DisplayTitle}. {job.Describe()}";
        Log($"the laptop downloads {map.Code}");
        Touch();
        return true;
    }

    /// <summary>Once a frame of a visit: downloaded maps come into the catalog, and the downloads' lines are kept up to date.</summary>
    private void WatchDownloads(HubClient client)
    {
        if (ContentPacks.Generation != _hubContentGeneration)
        {
            _hubContentGeneration = ContentPacks.Generation;
            Catalog.Maps.Refresh();
            Touch();
        }

        if (_laptopDownload is { } code)
        {
            string status;
            if (_laptopPad is null || ContentPacks.Job(code) is not { } job)
            {
                _laptopDownload = null;
                status = _laptopStatus;
            }
            else if (job.Error is not null)
            {
                _laptopDownload = null;
                status = job.Describe();
            }
            else if (job.Done)
            {
                _laptopDownload = null;
                status = $"{job.Title} downloaded. Pick it to set it on {PadTitle(_laptopPad)}.";
            }
            else
            {
                status = job.Describe();
            }

            if (status != _laptopStatus)
            {
                _laptopStatus = status;
                Touch();
            }
        }

        string? note = null;
        if (client.Queue.PadId is { } padId && client.Pads.TryGetValue(padId, out HubPadInfo? pad) && Catalog.Maps.FindMap(pad.Map) is null)
        {
            if (ContentPacks.Job(pad.Map) is { } queued)
            {
                note = queued.Describe();
            }
            else if (ContentPacks.Find(pad.Map) is { } map)
            {
                note = ContentPacks.Download(map).Describe();
            }
            else
            {
                ContentPacks.RefreshIndex();
                note = ContentPacks.Index is null && ContentPacks.IndexError is null
                    ? $"You do not have {pad.Title}; looking it up in the map list..."
                    : $"You do not have {pad.Title}, and the map list does not have it.";
            }
        }

        if (note != QueueDownloadNote)
        {
            QueueDownloadNote = note;
            Touch();
        }
    }
}
