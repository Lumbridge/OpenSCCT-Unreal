using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Game.Modes;
using OpenScct.Game.SBase;
using OpenScct.Net.Discovery;
using OpenScct.Server;
using OpenScct.UnrealBridge.Controls;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>One mode of a map as the match setup page offers it: whether it plays there and the map's own rules for it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeMenuMode
{
    /// <summary>1 when the map has what the mode plays with.</summary>
    public int Playable;

    /// <summary>Minutes, 0 for no limit.</summary>
    public int TimeLimit;

    /// <summary>Lives of each spy and merc, 0 for unlimited. In Merc Deathmatch SpyLives is 0 and ScoreLimit the kills to win.</summary>
    public int SpyLives, MercLives;

    public int RespawnSeconds;
    public int ScoreLimit;
}

/// <summary>A map as the menus list it (OpenSCCT's map catalog entry, cut to what the menus show).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeMenuMap
{
    public const int IdCapacity = 32;
    public const int TitleCapacity = 64;

    /// <summary><see cref="BridgeMenuMapFlags"/>.</summary>
    public int Flags;

    public int RecommendedPlayers;
    public int SpyStarts, MercStarts;

    /// <summary>The original's mode box, in its order.</summary>
    public BridgeMenuMode Story, DiskHunt, Deathmatch, MercDeathmatch;

    /// <summary>The map's folder name ("AquaE").</summary>
    public fixed char Id[IdCapacity];

    /// <summary>The name every page shows ("ENHANCED AQUARIUS").</summary>
    public fixed char Title[TitleCapacity];
}

public static class BridgeMenuMapFlags
{
    /// <summary>The map has the converted content a round is played on.</summary>
    public const int CanPlay = 1;
}

/// <summary>The texts of a map <c>GetMapText</c> reads.</summary>
public enum BridgeMenuMapText
{
    Description = 0,
    PreviewImage = 1,     // PNG path of the map list picture, empty when the map has none
    BannerImage = 2,      // PNG path of the 16:9 picture
    LoadingSpyImage = 3,
    LoadingMercImage = 4,
    Legend = 5,           // SMapSettings.LegendMap, lines split by LF
    BriefSpy = 6,         // the map's briefing for the spies
    BriefMerc = 7,
    Places = 8,           // the numbered places of the plan, one per line
}

/// <summary>An action a key or pad control can be bound to (OpenSCCT's ControlActions).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeMenuAction
{
    public const int IdCapacity = 32;
    public const int CategoryCapacity = 24;
    public const int LabelCapacity = 64;

    /// <summary>1 the spies have it, 2 the mercs have it.</summary>
    public int Sides;

    public fixed char Id[IdCapacity];
    public fixed char Category[CategoryCapacity];
    public fixed char Label[LabelCapacity];
}

/// <summary>The texts <c>GetText</c> reads.</summary>
public enum BridgeMenuText
{
    GameVersion = 0,  // OpenSCCT's release version this bridge was built from ("0.8.150")
    BuildNumber = 1,
    ContentRoot = 2,  // the converted content folder in use, empty when none
    SharedRoot = 3,   // its shared folder (interface sounds and pictures), empty when none
    Language = 4,     // the language of the content's texts ("en")
}

/// <summary>
/// What the menus need with no match running. Unreal asks for <see cref="MenuExports.GetApi"/> once
/// (type <c>OpenScct.UnrealBridge.MenuExports, OpenScct.UnrealBridge</c>) and keeps the table.
/// Source/OpenSCCT/Runtime/ScctMenuApi.h mirrors it field for field. Strings are UTF-16, zero terminated.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MenuApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<BridgeMenuMap*, int, int> GetMaps;
    public delegate* unmanaged[Cdecl]<char*, int, char*, int, int> GetMapText;
    public delegate* unmanaged[Cdecl]<char*, int, int, int, char*, int, int> GetModeText;
    public delegate* unmanaged[Cdecl]<char*, char*, char*, char*, int, int> Localize;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<BridgeMenuAction*, int, int> GetActions;
    public delegate* unmanaged[Cdecl]<int, int, char*, char*, int, int> GetActionKeys;
    public delegate* unmanaged[Cdecl]<int, int, char*, char*, int, int> GetKeyActions;
    public delegate* unmanaged[Cdecl]<int, int, char*, char*, int, int> BindAction;
    public delegate* unmanaged[Cdecl]<void> RefreshMaps;
}

public static unsafe class MenuExports
{
    public const int ApiVersion = 1;

    private static readonly object s_gate = new();
    private static MapCatalog? s_catalog;
    private static string? s_catalogRoot;
    private static int s_catalogFolders = -1;
    private static HudStrings? s_strings;
    private static string? s_stringsRoot;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(MenuApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetMaps = &GetMaps;
        api->GetMapText = &GetMapText;
        api->GetModeText = &GetModeText;
        api->Localize = &Localize;
        api->GetText = &GetText;
        api->GetActions = &GetActions;
        api->GetActionKeys = &GetActionKeys;
        api->GetKeyActions = &GetKeyActions;
        api->BindAction = &BindAction;
        api->RefreshMaps = &RefreshMaps;
        return ApiVersion;
    }

    /// <summary>
    /// The install's map catalog, read once per content folder and per change of the map folders Unreal names
    /// (Online/MapFolders.cs: -ScctMaps, maps next to the game), so a converted map with no imported level is offered too.
    /// </summary>
    private static MapCatalog Catalog
    {
        get
        {
            lock (s_gate)
            {
                string? root = ContentPaths.Root;
                if (s_catalog is null || !string.Equals(root, s_catalogRoot, StringComparison.OrdinalIgnoreCase) || s_catalogFolders != Online.MapFolders.Revision)
                {
                    s_catalogFolders = Online.MapFolders.Revision;
                    s_catalog = new MapCatalog(Online.MapFolders.AllRoots());
                    s_catalogRoot = root;
                }

                return s_catalog;
            }
        }
    }

    private static HudStrings Strings
    {
        get
        {
            lock (s_gate)
            {
                string? shared = ContentPaths.Shared;
                if (s_strings is null || !string.Equals(shared, s_stringsRoot, StringComparison.OrdinalIgnoreCase))
                {
                    s_strings = HudStrings.Load(shared);
                    s_stringsRoot = shared;
                }

                return s_strings;
            }
        }
    }

    /// <summary>
    /// Writes the maps a match can be hosted on (the host list, in its order). Returns their count (only
    /// <paramref name="capacity"/> are written).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMaps(BridgeMenuMap* maps, int capacity)
    {
        try
        {
            MapCatalog catalog = Catalog;
            IReadOnlyList<MapCatalogEntry> entries = catalog.GetHostMaps();
            for (int i = 0; i < entries.Count && i < capacity && maps != null; i++)
            {
                MapCatalogEntry entry = entries[i];
                USMapSettings? settings = catalog.GetMapSettings(entry.Id);
                BridgeMenuMap map = default;
                map.Flags = entry.CanRender ? BridgeMenuMapFlags.CanPlay : 0;
                map.RecommendedPlayers = entry.RecommendedPlayers;
                map.SpyStarts = entry.SpyStarts;
                map.MercStarts = entry.MercStarts;
                map.Story = ModeOf(entry, settings, EGameMode.GM_Multi);
                map.DiskHunt = ModeOf(entry, settings, EGameMode.GM_DiskHunt);
                map.Deathmatch = ModeOf(entry, settings, EGameMode.GM_DeathMatch);
                map.MercDeathmatch = ModeOf(entry, settings, EGameMode.GM_MercDeathMatch);
                Fill(entry.Id, map.Id, BridgeMenuMap.IdCapacity);
                Fill(entry.DisplayName, map.Title, BridgeMenuMap.TitleCapacity);
                maps[i] = map;
            }

            return entries.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static BridgeMenuMode ModeOf(MapCatalogEntry entry, USMapSettings? settings, EGameMode mode)
    {
        if (entry.Mode(mode) is not { Playable: true })
        {
            return default;
        }

        // The untouched match creation page for the mode, as a local match and a new lobby start with.
        USMatchInfo match = MatchSetup.Defaults(settings, mode, entry.Id).Match;
        return new BridgeMenuMode
        {
            Playable = 1,
            TimeLimit = match.TimeLimit,
            SpyLives = match.NumLivesATT,
            MercLives = match.NumLivesDEF,
            RespawnSeconds = match.TimesRespawn,
            ScoreLimit = match.ScoreLimit,
        };
    }

    /// <summary>Copies a text of a map (<see cref="BridgeMenuMapText"/>). Returns its full length, 0 when the map has none.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMapText(char* map, int kind, char* buffer, int capacity)
    {
        try
        {
            MapCatalog catalog = Catalog;
            if (map == null || catalog.GetCatalogEntry(new string(map)) is not { } entry)
            {
                return Copy("", buffer, capacity);
            }

            USMapSettings? settings = kind is (int)BridgeMenuMapText.BriefSpy or (int)BridgeMenuMapText.BriefMerc
                ? catalog.GetMapSettings(entry.Id)
                : null;
            string text = (BridgeMenuMapText)kind switch
            {
                BridgeMenuMapText.Description => entry.Description ?? "",
                BridgeMenuMapText.PreviewImage => entry.PreviewImage ?? "",
                BridgeMenuMapText.BannerImage => entry.BannerImage ?? "",
                BridgeMenuMapText.LoadingSpyImage => entry.LoadingSpyImage ?? "",
                BridgeMenuMapText.LoadingMercImage => entry.LoadingMercImage ?? "",
                BridgeMenuMapText.Legend => (entry.Legend ?? "").Replace('|', '\n'),
                BridgeMenuMapText.BriefSpy => settings?.ExpliMapInBriefSpy ?? "",
                BridgeMenuMapText.BriefMerc => settings?.ExpliMapInBriefSentry ?? "",
                BridgeMenuMapText.Places => string.Join('\n', entry.Places.Select(static p => p.ToString())),
                _ => "",
            };
            return Copy(text, buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>
    /// Copies what a map says about a mode for a side (<paramref name="mode"/> is EGameMode; <paramref name="merc"/>
    /// the side; <paramref name="kind"/> 0 the one-line objective, 1 the briefing paragraph). Returns its full length.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetModeText(char* map, int mode, int merc, int kind, char* buffer, int capacity)
    {
        try
        {
            MapCatalog catalog = Catalog;
            if (map == null || catalog.GetMapSettings(new string(map)) is not { } settings)
            {
                return Copy("", buffer, capacity);
            }

            int index = settings.SearchForGameMode(USMatchInfo.RulesModeFor((EGameMode)mode));
            if (index < 0 || index >= settings.ModesStrings.Count)
            {
                return Copy("", buffer, capacity);
            }

            FGameModeInfoStrings words = settings.ModesStrings[index];
            string text = kind == 0
                ? (merc != 0 ? words.ExplicationModeSentry : words.ExplicationModeSpy)
                : (merc != 0 ? words.ExpliModeInBriefSentry : words.ExpliModeInBriefSpy);
            return Copy(text ?? "", buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>
    /// Copies a string of the game's interface text (the player's content: System/&lt;file&gt;.int, [section], key), with
    /// or without a match running. Returns its full length, 0 when the content has no such string.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Localize(char* file, char* section, char* key, char* buffer, int capacity)
    {
        try
        {
            return file == null || section == null || key == null
                ? Copy("", buffer, capacity)
                : Copy(Strings.Get(new string(file), new string(section), new string(key)), buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>Copies one of <see cref="BridgeMenuText"/>. Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            string text = (BridgeMenuText)kind switch
            {
                BridgeMenuText.GameVersion => BuildInfo.Version,
                BridgeMenuText.BuildNumber => BuildInfo.BuildNumber,
                BridgeMenuText.ContentRoot => ContentPaths.Root ?? "",
                BridgeMenuText.SharedRoot => ContentPaths.Shared ?? "",
                BridgeMenuText.Language => ContentPaths.Language,
                _ => "",
            };
            return Copy(text, buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>Forgets the map catalog and the strings, so content converted since is listed.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void RefreshMaps()
    {
        lock (s_gate)
        {
            s_catalog = null;
            s_strings = null;
        }
    }

    // ---- bindings ---------------------------------------------------------------------------------------------

    /// <summary>Writes every bindable action, in the order a bindings page lists them. Returns their count.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActions(BridgeMenuAction* actions, int capacity)
    {
        try
        {
            IReadOnlyList<ControlAction> all = ControlActions.All;
            for (int i = 0; i < all.Count && i < capacity && actions != null; i++)
            {
                BridgeMenuAction action = default;
                action.Sides = (all[i].Spy ? 1 : 0) | (all[i].Merc ? 2 : 0);
                Fill(all[i].Id, action.Id, BridgeMenuAction.IdCapacity);
                Fill(all[i].Category, action.Category, BridgeMenuAction.CategoryCapacity);
                Fill(all[i].Label, action.Label, BridgeMenuAction.LabelCapacity);
                actions[i] = action;
            }

            return all.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Copies the keys (or, with <paramref name="pad"/>, the pad controls) of one side that do an action, one per
    /// line (ActionBindings.KeysOf). Returns the text's full length.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActionKeys(int merc, int pad, char* action, char* buffer, int capacity)
    {
        try
        {
            return action != null && ActionBindings.Find(new string(action)) is { } found
                ? Copy(string.Join('\n', ActionBindings.KeysOf(ControlsStore.Bindings.Of(merc != 0), pad != 0, found)), buffer, capacity)
                : Copy("", buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>
    /// Copies the labels of the side's actions a key or pad control does now, one per line: what a new binding of
    /// that key would take away. Returns the text's full length, 0 for a free key.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetKeyActions(int merc, int pad, char* key, char* buffer, int capacity)
    {
        try
        {
            return key == null
                ? Copy("", buffer, capacity)
                : Copy(string.Join('\n', ActionBindings.LabelsOf(ControlsStore.Bindings.Of(merc != 0), merc != 0, pad != 0, new string(key))), buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return Copy("", buffer, capacity);
        }
    }

    /// <summary>
    /// Binds an action of one side in place of what did it (ActionBindings.Bind). Returns 1, or 0 for an unknown
    /// action or a name that is not a key. Not saved until the controls table's SaveBindings.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int BindAction(int merc, int pad, char* action, char* key, int keepOthers)
    {
        try
        {
            return action != null && ActionBindings.Find(new string(action)) is { } found
                && ActionBindings.Bind(ControlsStore.Bindings.Of(merc != 0), pad != 0, found, key == null ? null : new string(key), keepOthers != 0)
                ? 1
                : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static void Fill(string text, char* buffer, int capacity)
    {
        int count = Math.Min(text.Length, capacity - 1);
        text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
        buffer[count] = '\0';
    }

    /// <summary>Copies <paramref name="text"/> with its terminator, as much as fits. Returns the full length.</summary>
    private static int Copy(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}
