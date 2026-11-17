using System;
using System.Globalization;
using System.IO;
using OpenScct.Bots;
using OpenScct.Content;
using OpenScct.Game.AI;
using OpenScct.Maps;

namespace OpenScct.UnrealBridge.Online;

/// <summary>What <see cref="BotNavigationOffer.Check"/> found.</summary>
public enum BotNavigationState
{
    /// <summary>Nothing to offer: the graphs are prepared, the map is unknown, or the index has no prebuilt copy. Start now.</summary>
    None = 0,

    /// <summary>The content index is still being read: ask again (start anyway after about three seconds).</summary>
    Checking = 1,

    /// <summary>A prebuilt copy can be downloaded: the text is the question.</summary>
    Offer = 2,
}

/// <summary>
/// The Godot client's BotNavigationPrompt without its dialog: before an offline match or a hosted game starts with
/// bots, the map's prebuilt bot navigation is offered from OpenSCCT-Content when no prepared graphs are cached, so
/// the bots need not build them on this PC first (which can take minutes). Unreal shows the question and polls the
/// download (<see cref="ContentPacks.DownloadBotNavigation"/>).
/// </summary>
public static class BotNavigationOffer
{
    /// <summary>
    /// Whether to offer <paramref name="map"/>'s prebuilt navigation. <paramref name="text"/> is the question
    /// (Offer), "Checking for prebuilt bot navigation..." (Checking), or why nothing is offered (None).
    /// </summary>
    public static BotNavigationState Check(string map, out string text)
    {
        if (!Find(map, out string folder, out SvMapManifest? manifest, out text))
        {
            return BotNavigationState.None;
        }

        ContentIndex? index = ContentPacks.Index;
        ContentBotNavigation? nav = index?.FindBotNavigation(manifest!.Map, manifest.SimHash, NavGraph.FormatVersion);
        if (nav is null)
        {
            if (index is not null || ContentPacks.IndexError is not null)
            {
                text = ContentPacks.IndexError is { } error
                    ? $"The content index could not be read ({error}): the bots build their navigation for {manifest!.Map} on this PC."
                    : $"OpenSCCT-Content has no prebuilt bot navigation for this version of {manifest!.Map}: the bots build it on this PC.";
                return BotNavigationState.None;
            }

            text = "Checking for prebuilt bot navigation...";
            return BotNavigationState.Checking;
        }

        if (AiNavCache.HasPreparedGraphs(folder, manifest!.Map, nav.SimHash, nav.ActivationMask))
        {
            text = $"The bot navigation for {manifest.Map} is already prepared.";
            return BotNavigationState.None;
        }

        text = string.Create(CultureInfo.InvariantCulture,
            $"Download prebuilt bot routes for {manifest.Map} ({nav.Download.Size / 1048576.0:0.0} MiB)?\nBuilding them on this PC can take a few minutes.");
        return BotNavigationState.Offer;
    }

    /// <summary>Starts downloading the map's prebuilt navigation; null when the index has none for it.</summary>
    public static ContentPacks.DownloadJob? Start(string map)
    {
        if (!Find(map, out _, out SvMapManifest? manifest, out _) ||
            ContentPacks.Index?.FindBotNavigation(manifest!.Map, manifest.SimHash, NavGraph.FormatVersion) is not { } nav)
        {
            return null;
        }

        return ContentPacks.DownloadBotNavigation(manifest.Map, nav);
    }

    private static bool Find(string map, out string folder, out SvMapManifest? manifest, out string text)
    {
        manifest = null;
        text = string.Empty;
        folder = OnlineBridge.Instance.Catalog.Maps.FindFolder(map) ?? string.Empty;
        if (folder.Length == 0)
        {
            text = $"{map} is not installed.";
            return false;
        }

        try
        {
            manifest = SvMapManifest.Load(Path.Combine(folder, SvMapHash.ManifestFile));
            return true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or SvMapFormatException)
        {
            text = $"{map} has no readable manifest ({error.Message}).";
            return false;
        }
    }
}
