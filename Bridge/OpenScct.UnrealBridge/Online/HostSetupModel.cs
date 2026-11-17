using System;
using System.Collections.Generic;
using System.Globalization;
using System.Numerics;
using System.Text;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.Server.Online;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge.Online;

/// <summary>HostApi.SetupText's kinds: what the Create Session page asks the bridge.</summary>
public enum SetupText
{
    /// <summary>The maps a game can be hosted on, one per line: "Id TAB name TAB picture path TAB mode keys (comma separated)".</summary>
    Maps = 0,

    /// <summary>The modes the map named by the argument offers, as mode keys separated by commas, in the page's order.</summary>
    Modes = 1,

    /// <summary>
    /// A fresh setup for the argument's MapName and Mode on top of the options (the page's session-wide values kept,
    /// the rules reloaded from the map; Create Session's change of map or mode). An empty Mode keeps the options'
    /// mode when the map offers it, else the map's first. Returns the setup line.
    /// </summary>
    Defaults = 2,

    /// <summary>The options as the setup line the session reads (LobbySetup.ToOptions), every value inside the page's ranges.</summary>
    Normalize = 3,

    /// <summary>The setup in one line as the lobby describes it.</summary>
    Describe = 4,

    /// <summary>
    /// The page's rows for the options, one per line: "Row TAB kind TAB caption TAB shown text TAB value TAB min TAB max
    /// TAB visible TAB choices (| separated)". Kind is number, check or choice. Then "hint TAB Voice|Bots TAB text" lines.
    /// </summary>
    Rows = 5,

    /// <summary>
    /// A change of one row (the argument: "Row value", e.g. "TimeLimit 12", "Revenge 1", "Mode DiskHunt", "Map AquaE")
    /// with Create Session's rules (Revenge forces two games, the disk clamps, Merc Deathmatch's kills, a new map or
    /// mode reloads the rules). Returns the new setup line.
    /// </summary>
    Edit = 6,

    /// <summary>
    /// The Alternate rules of the options, one row per line: "ability TAB bit TAB side (0 spy, 1 merc, 2 both) TAB caption
    /// TAB on", "gadget TAB side TAB option TAB name TAB allowed", "slots TAB side TAB count TAB most".
    /// </summary>
    Alternate = 7,

    /// <summary>
    /// A change of the Alternate rules (the argument: "ability BIT 0|1", "gadget SIDE OPTION 0|1", "slots SIDE COUNT",
    /// "reset"). Returns the new setup line.
    /// </summary>
    EditAlternate = 8,
}

/// <summary>
/// The Create Session page's rules for Unreal: OpenSCCT's own <see cref="CreateSessionModel"/> (linked from the Godot
/// client, which has no Godot in it) over the bridge's map catalog. The page in Unreal holds the setup as an options
/// line (LobbySetup.ToOptions) and asks here for its rows, for the change of one row and for a map's defaults, so the
/// ranges, captions, visibility and the rules between rows are the ones the Godot page and the server use.
/// </summary>
public static class HostSetupModel
{
    /// <summary>The text of <paramref name="kind"/>.</summary>
    /// <param name="options">The page's setup line (may be empty: the first map's defaults).</param>
    /// <param name="argument">What the kind takes (a map, a row change), or null.</param>
    /// <param name="maxPlayers">Player slots the host picked, or 0 or less while they follow the map.</param>
    public static string Text(OnlineBridge online, SetupText kind, string? options, string? argument, int maxPlayers)
    {
        ArgumentNullException.ThrowIfNull(online);
        return kind switch
        {
            SetupText.Maps => MapLines(online),
            SetupText.Modes => string.Join(',', ModesOf(Model(online, $"?MapName={argument}", maxPlayers).Modes)),
            SetupText.Defaults => Defaults(online, options, argument, maxPlayers),
            SetupText.Normalize => Model(online, options, maxPlayers).Setup.ToOptions(),
            SetupText.Describe => Model(online, options, maxPlayers).Setup.Describe(),
            SetupText.Rows => Rows(Model(online, options, maxPlayers)),
            SetupText.Edit => Edit(online, Model(online, options, maxPlayers), argument ?? string.Empty),
            SetupText.Alternate => AlternateLines(Model(online, options, maxPlayers).Setup.Alternate),
            SetupText.EditAlternate => EditAlternate(Model(online, options, maxPlayers), argument ?? string.Empty),
            _ => string.Empty,
        };
    }

    /// <summary>The maps the page lists: installed, playable here, neither a menu nor a training map; with their names.</summary>
    public static List<(string Id, string Title)> HostableMaps(OnlineBridge online)
    {
        PlayCatalog catalog = online.Catalog;
        var list = new List<(string Id, string Title)>();
        foreach (MapListEntry entry in catalog.Maps.ListMaps())
        {
            if (MapCatalog.IsMenuMap(entry.Id) || !catalog.CanPlay(entry.Id))
            {
                continue;
            }

            MapCatalogEntry? info = catalog.Maps.GetCatalogEntry(entry.Id);
            if (info?.Kind is MapKind.Menu or MapKind.Training)
            {
                continue;
            }

            list.Add((entry.Id, info?.DisplayName ?? entry.Id));
        }

        return list;
    }

    private static string MapLines(OnlineBridge online)
    {
        MapCatalog maps = online.Catalog.Maps;
        var text = new StringBuilder();
        List<(string Id, string Title)> list = HostableMaps(online);
        foreach ((string id, string title) in list)
        {
            var model = new CreateSessionModel(list, maps.GetMapSettings, LobbySetup.ForMap(id, EGameMode.GM_Multi, maps.GetMapSettings(id)));
            text.Append(id).Append('\t').Append(Clean(title)).Append('\t').Append(maps.GetCatalogEntry(id)?.PreviewImage ?? string.Empty)
                .Append('\t').Append(string.Join(',', ModesOf(model.Modes))).Append('\n');
        }

        return text.ToString();
    }

    private static IEnumerable<string> ModesOf(IReadOnlyList<EGameMode> modes)
    {
        foreach (EGameMode mode in modes)
        {
            yield return MapCatalog.ModeKey(mode);
        }
    }

    /// <summary>The page's model over the options: the map's defaults with the options' values on top.</summary>
    private static CreateSessionModel Model(OnlineBridge online, string? options, int maxPlayers)
    {
        MapCatalog maps = online.Catalog.Maps;
        List<(string Id, string Title)> list = HostableMaps(online);
        string line = options ?? string.Empty;
        string map = UcOptions.ParseOption(line, "MapName");
        if (map.Length == 0 || maps.FindMap(map) is null)
        {
            // Godot's Create Session opens on Steel when it is there, else on the first map.
            map = list.Exists(m => string.Equals(m.Id, "SteeE", StringComparison.OrdinalIgnoreCase)) ? "SteeE" : list.Count > 0 ? list[0].Id : MapCatalog.TestBox;
        }

        bool chosen = maxPlayers > 0;
        int players = chosen ? maxPlayers : CreateSessionModel.FollowedPlayers(maps.GetCatalogEntry(map)?.StatedPlayers);
        var mode = (EGameMode)(byte)UcOptions.GetIntOption(line, "GameMode", (int)EGameMode.GM_Multi);
        LobbySetup fallback = LobbySetup.ForMap(map, mode, maps.GetMapSettings(map), players);
        LobbySetup start = LobbySetup.FromOptions(line, fallback) with { Map = map, MaxPlayers = players };
        return new CreateSessionModel(list, maps.GetMapSettings, start, id => maps.GetCatalogEntry(id)?.StatedPlayers, chosen);
    }

    private static string Defaults(OnlineBridge online, string? options, string? argument, int maxPlayers)
    {
        CreateSessionModel model = Model(online, options, maxPlayers);
        string map = UcOptions.ParseOption(argument ?? string.Empty, "MapName");
        if (map.Length > 0 && online.Catalog.Maps.FindMap(map) is { } entry)
        {
            model.SelectMap(entry.Id);
        }

        string modeKey = UcOptions.ParseOption(argument ?? string.Empty, "Mode");
        if (modeKey.Length > 0 && MapCatalog.ModeOfKey(modeKey) is { } mode && Contains(model.Modes, mode))
        {
            model.SelectMode(mode);
        }

        return model.Setup.ToOptions();
    }

    private static readonly (SessionRow Row, string Name)[] RowNames =
    [
        (SessionRow.Mode, "Mode"), (SessionRow.VoiceChat, "Voice"), (SessionRow.MaxPlayers, "MaxPlayers"), (SessionRow.Revenge, "Revenge"),
        (SessionRow.RandomMap, "RandomMap"), (SessionRow.VoiceInLobby, "VoiceInLobby"), (SessionRow.AutoBalance, "AutoBalance"),
        (SessionRow.Alternate, "Alternate"), (SessionRow.GamesInARow, "GamesInARow"), (SessionRow.TimeLimit, "TimeLimit"),
        (SessionRow.SpyLives, "SpyLives"), (SessionRow.MercLives, "MercLives"), (SessionRow.Respawn, "Respawn"),
        (SessionRow.ActiveDisks, "ActiveDisks"), (SessionRow.DisksToTake, "DisksToTake"), (SessionRow.Bots, "Bots"),
        (SessionRow.BotsPerSide, "BotsPerSide"), (SessionRow.SpyBots, "SpyBots"), (SessionRow.MercBots, "MercBots"),
        (SessionRow.BotSkill, "BotSkill"), (SessionRow.Practice, "Practice"), (SessionRow.Sidearms, "Sidearms"),
        (SessionRow.Rifles, "Rifles"), (SessionRow.ResponsiveSmoke, "ResponsiveSmoke"),
    ];

    private static string Rows(CreateSessionModel model)
    {
        var text = new StringBuilder();
        foreach ((SessionRow row, string name) in RowNames)
        {
            string kind = CreateSessionModel.IsCheck(row) ? "check" : row is SessionRow.Mode or SessionRow.VoiceChat or SessionRow.Bots or SessionRow.BotSkill ? "choice" : "number";
            (int min, int max) = model.Range(row);
            int value = row switch
            {
                SessionRow.Mode => Math.Max(0, IndexOf(model.Modes, model.Setup.Mode)),
                _ when CreateSessionModel.IsCheck(row) => model.Checked(row) ? 1 : 0,
                _ => model.Number(row),
            };
            IEnumerable<string> choices = row switch
            {
                SessionRow.Mode => Names(model.Modes),
                SessionRow.VoiceChat => LobbySetup.VoiceModeNames,
                SessionRow.Bots => OnlineBotRoster.FillNames,
                SessionRow.BotSkill => OnlineBotRoster.SkillNames,
                _ => [],
            };
            text.Append(string.Create(CultureInfo.InvariantCulture,
                    $"{name}\t{kind}\t{Clean(model.Caption(row))}\t{Clean(model.Text(row))}\t{value}\t{min}\t{max}\t{(model.Visible(row) ? 1 : 0)}\t"))
                .Append(string.Join('|', choices)).Append('\n');
        }

        text.Append("hint\tVoice\t").Append(Clean(model.VoiceHint)).Append('\n');
        text.Append("hint\tBots\t").Append(Clean(model.BotHint)).Append('\n');
        return text.ToString();
    }

    private static IEnumerable<string> Names(IReadOnlyList<EGameMode> modes)
    {
        foreach (EGameMode mode in modes)
        {
            yield return CreateSessionModel.ModeName(mode);
        }
    }

    private static string Edit(OnlineBridge online, CreateSessionModel model, string argument)
    {
        string[] parts = argument.Trim().Split(' ', 2, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (parts.Length == 2)
        {
            string name = parts[0];
            string value = parts[1];
            if (name.Equals("Map", StringComparison.OrdinalIgnoreCase))
            {
                if (FindMap(online, value) is { } map)
                {
                    model.SelectMap(map);
                }
            }
            else if (name.Equals("Mode", StringComparison.OrdinalIgnoreCase))
            {
                EGameMode? mode = MapCatalog.ModeOfKey(value);
                if (mode is null && int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int index) && index >= 0 && index < model.Modes.Count)
                {
                    mode = model.Modes[index];
                }

                if (mode is { } m && Contains(model.Modes, m))
                {
                    model.SelectMode(m);
                }
            }
            else if (Array.Find(RowNames, r => r.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) is { Name: not null } found
                && int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int number))
            {
                SessionRow row = found.Row;
                if (CreateSessionModel.IsCheck(row))
                {
                    model.SetChecked(row, number != 0);
                }
                else if (row is SessionRow.VoiceChat or SessionRow.Bots or SessionRow.BotSkill)
                {
                    // The choice rows step; walk from the current index to the asked one.
                    model.Step(row, number - model.Number(row));
                }
                else
                {
                    model.SetNumber(row, number);
                }
            }
        }

        return model.Setup.ToOptions();
    }

    /// <summary>A hostable map's id as the catalog spells it, or null.</summary>
    private static string? FindMap(OnlineBridge online, string id) =>
        HostableMaps(online).Find(m => string.Equals(m.Id, id, StringComparison.OrdinalIgnoreCase)).Id;

    private static string AlternateLines(AlternateRules rules)
    {
        var text = new StringBuilder();
        foreach ((AlternateAbility ability, string caption) in AlternateRules.SpyAbilityRows)
        {
            text.Append(string.Create(CultureInfo.InvariantCulture, $"ability\t{(int)ability}\t0\t{caption}\t{(rules.Has(ability) ? 1 : 0)}\n"));
        }

        foreach ((AlternateAbility ability, string caption) in AlternateRules.MercAbilityRows)
        {
            text.Append(string.Create(CultureInfo.InvariantCulture, $"ability\t{(int)ability}\t1\t{caption}\t{(rules.Has(ability) ? 1 : 0)}\n"));
        }

        text.Append(string.Create(CultureInfo.InvariantCulture,
            $"ability\t{(int)AlternateAbility.CinematicCamera}\t2\tCinematic Camera\t{(rules.Has(AlternateAbility.CinematicCamera) ? 1 : 0)}\n"));

        for (int side = 0; side < 2; side++)
        {
            List<ClassRef> gadgets = side == 0 ? new STeamAttaquant().ClassInventory : new STeamDefenseur().ClassInventory;
            for (int option = 0; option < gadgets.Count; option++)
            {
                // The cloak's old option is not offered (the Godot page skips it as well).
                if (OpenScct.Game.Equipment.SpyCloak.IsLegacyGadget(gadgets[option].Path))
                {
                    continue;
                }

                text.Append(string.Create(CultureInfo.InvariantCulture,
                    $"gadget\t{side}\t{option}\t{Clean(GadgetName(gadgets[option].Path))}\t{(rules.GadgetAllowed(side, option) ? 1 : 0)}\n"));
            }

            int allowed = side == 0 ? rules.AvailableSpyGadgets : BitOperations.PopCount(rules.MercGadgets);
            text.Append(string.Create(CultureInfo.InvariantCulture,
                $"slots\t{side}\t{(side == 0 ? rules.SpyGadgetCount : rules.MercGadgetCount)}\t{Math.Min(AlternateRules.MaxGadgetCount, allowed)}\n"));
        }

        return text.ToString();
    }

    private static string EditAlternate(CreateSessionModel model, string argument)
    {
        AlternateRules rules = model.Setup.Alternate;
        string[] parts = argument.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
        int Arg(int i) => i < parts.Length && int.TryParse(parts[i], NumberStyles.Integer, CultureInfo.InvariantCulture, out int v) ? v : 0;
        switch (parts.Length > 0 ? parts[0].ToLowerInvariant() : string.Empty)
        {
            case "ability":
                rules = rules.With((AlternateAbility)Arg(1) & AlternateRules.AllAbilities, Arg(2) != 0);
                break;
            case "gadget":
                rules = rules.WithGadget(Arg(1), Arg(2), Arg(3) != 0);
                break;
            case "slots":
                rules = Arg(1) == 0 ? rules with { SpyGadgetCount = (byte)Math.Clamp(Arg(2), 0, 15) } : rules with { MercGadgetCount = (byte)Math.Clamp(Arg(2), 0, 15) };
                break;
            case "reset":
                rules = AlternateRules.Default with { On = rules.On };
                break;
        }

        model.SetAlternate(rules);
        return model.Setup.ToOptions();
    }

    /// <summary>A gadget's name as the Godot gear menu shows it: the device class's item name, else its text's, else the class name.</summary>
    private static string GadgetName(string? path)
    {
        if (string.IsNullOrEmpty(path))
        {
            return "Empty";
        }

        try
        {
            if (path.StartsWith("Svm.", StringComparison.OrdinalIgnoreCase) &&
                OpenScct.Game.Devices.DeviceClasses.Index.Find(path)?.Create() is Inventory gadget && gadget.ItemName is { Length: > 0 } item)
            {
                return item;
            }
        }
        catch (Exception)
        {
            // A device that cannot be made outside a level is named from its text below.
        }

        int dot = path.LastIndexOf('.');
        string fallback = dot >= 0 ? path[(dot + 1)..] : path;
        return UcText.Get(path, "ItemName", fallback) is { Length: > 0 } name ? name : fallback;
    }

    private static bool Contains(IReadOnlyList<EGameMode> modes, EGameMode mode) => IndexOf(modes, mode) >= 0;

    private static int IndexOf(IReadOnlyList<EGameMode> modes, EGameMode mode)
    {
        for (int i = 0; i < modes.Count; i++)
        {
            if (modes[i] == mode)
            {
                return i;
            }
        }

        return -1;
    }

    /// <summary>A text for a TAB separated line: no TABs or line breaks inside.</summary>
    private static string Clean(string? text) =>
        (text ?? string.Empty).Replace('\t', ' ').Replace('\n', ' ').Replace('\r', ' ');
}
