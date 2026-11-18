using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OpenScct.UnrealBridge.Controls;

// A port of OpenSCCT's game/Scripts/Options/EnhancedImport.cs (and the parts of Input/KeyLayout.cs and
// Input/DefaultKeyLayout.cs it leans on: the original ini's aliases, a profile's key file laid over the stock layout,
// and the additions a first run gets). The Godot class stages everything in its GameSettings draft; here the settings
// are split between owners, so the import hands back three things:
//   - the controls (sensitivities, controller tuning, inversions, the wheel, widescreen) as a changed ControlSettings
//     and the key layouts as a changed ControlBindings, both the bridge's own (ControlsStore);
//   - the gear as a kit text for Unreal's saved kit (Settings/ScctProfileOptions.h ApplyLoadoutText);
//   - everything Unreal keeps (volumes, display, graphics, HUD, voice) as settings console keys and values
//     (Source/OpenSCCT/Settings/ScctSettingsConsole.h), which Unreal sets through the same code the console's Set runs.

/// <summary>One setting Unreal keeps, as the settings console names it ("Audio.Effects") and a value it takes.</summary>
public readonly record struct ImportedSetting(string Key, string Value);

/// <summary>What an import found: the changed controls and bindings, the kit, Unreal's settings and a report.</summary>
public sealed class EnhancedImportResult
{
    public required ControlSettings Controls { get; init; }

    public required ControlBindings Bindings { get; init; }

    /// <summary>"spy:A,B,C,D;merc:A,B,C,D;weapon:W" with only the parts the profile names, empty for none.</summary>
    public string Loadout { get; init; } = "";

    public List<ImportedSetting> Settings { get; } = new();

    /// <summary>One line per group taken over or left out, for the player.</summary>
    public List<string> Report { get; } = new();
}

/// <summary>
/// Reads a player's settings out of an Enhanced SCCT Versus (Reloaded) install:
/// <list type="bullet">
/// <item>Reloaded's <c>System\SCCT_Versus.config</c> (JSON with comments): sensitivities, display, graphics, HUD,
/// voice and controller settings.</item>
/// <item>The game profile <c>&lt;name&gt;_prf.ini</c>: inverted axes, volumes, mouse wheel, gear.</item>
/// <item>The profile's key layouts <c>&lt;name&gt;_kbd_0.ini</c> (spy) and <c>&lt;name&gt;_kbd_1.ini</c> (merc).</item>
/// </list>
/// The profile files are not in the install: they sit in the folder <c>System\_Profiles_.lnk</c> points to
/// (ProgramData\Ubisoft\...\Saved Games\Versus), shared by every install on the machine. Nothing is written here.
/// </summary>
public sealed class EnhancedImport
{
    public const string ConfigName = "SCCT_Versus.config";

    private const string ProfileSuffix = "_prf.ini";

    private readonly Dictionary<string, string> _profile = new(StringComparer.OrdinalIgnoreCase);
    private readonly JsonElement _config;
    private readonly string? _keys0;
    private readonly string? _keys1;

    private EnhancedImport(string? root, string? configPath, string? profilePath)
    {
        Root = root;
        ConfigPath = configPath;
        ProfilePath = profilePath;
        if (configPath is not null)
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(configPath),
                new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
            _config = document.RootElement.Clone();
        }

        if (profilePath is not null)
        {
            string file = Path.GetFileName(profilePath);
            ProfileName = file[..^ProfileSuffix.Length];
            foreach (string raw in File.ReadAllLines(profilePath, Encoding.Latin1))
            {
                int eq = raw.IndexOf('=', StringComparison.Ordinal);
                if (eq > 0)
                {
                    _profile[raw[..eq].Trim()] = raw[(eq + 1)..].Trim();
                }
            }

            string folder = Path.GetDirectoryName(profilePath) ?? ".";
            _keys0 = Existing(Path.Combine(folder, ProfileName + "_kbd_0.ini"));
            _keys1 = Existing(Path.Combine(folder, ProfileName + "_kbd_1.ini"));
        }
    }

    /// <summary>The install folder (the one holding System), or null when only a profile folder was chosen.</summary>
    public string? Root { get; }

    public string? ConfigPath { get; }

    public string? ProfilePath { get; }

    /// <summary>The profile's name (the file name before <c>_prf.ini</c>), empty without a profile.</summary>
    public string ProfileName { get; } = "";

    /// <summary>
    /// Reloaded's use_split_spy_merc_keyboard; an install without the config plays one layout (stock). The profile
    /// alone (no install chosen) has no config to say: two key files that differ are then taken as the spy's and the
    /// mercenary's, since one layout for both would have given the spy the mercenary's keys.
    /// </summary>
    public bool SplitKeyboard => Flag(_config, "use_split_spy_merc_keyboard") ?? (Root is null && KeyFilesDiffer);

    private bool KeyFilesDiffer
    {
        get
        {
            if (_keys0 is null || _keys1 is null)
            {
                return false;
            }

            try
            {
                return !string.Equals(File.ReadAllText(_keys0).ReplaceLineEndings("\n").Trim(), File.ReadAllText(_keys1).ReplaceLineEndings("\n").Trim(), StringComparison.Ordinal);
            }
            catch (IOException)
            {
                return false;
            }
        }
    }

    /// <summary>The wheel mode of the profile (SelectedMouse 0 changes gadgets, 1 walk and run), or null without one.</summary>
    public string? WheelMode => ProfileInt("SelectedMouse") switch { 0 => "Gadget", 1 => "Speed", _ => null };

    /// <summary>Where every install on the machine keeps its Versus profiles and key layouts.</summary>
    public static string UsualProfileFolder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
        "Ubisoft", "Tom Clancy's Splinter Cell Chaos Theory", "Saved Games", "Versus");

    /// <summary>
    /// Looks for an install at or above <paramref name="folder"/> (any folder inside the install will do), or takes
    /// <paramref name="folder"/> itself when it holds profiles. Null when there is nothing to import.
    /// </summary>
    public static EnhancedImport? Find(string folder)
    {
        if (string.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder))
        {
            return null;
        }

        string? root = InstallRoot(folder);
        string? profiles = root is not null ? ProfileFolder(root) : folder;
        string? config = root is null ? null : Existing(Path.Combine(root, "System", ConfigName));
        string? profile = profiles is null ? null : NewestProfile(profiles);
        return config is null && profile is null ? null : new EnhancedImport(root, config, profile);
    }

    /// <summary>
    /// The import without an install: the profile and key layouts from <see cref="UsualProfileFolder"/>. Reloaded's
    /// own settings (sensitivities, display, voice) are in the install and are not part of it. Null without a profile.
    /// </summary>
    public static EnhancedImport? FindWithoutInstall() =>
        Directory.Exists(UsualProfileFolder) && NewestProfile(UsualProfileFolder) is { } profile ? new EnhancedImport(null, null, profile) : null;

    private static string? InstallRoot(string folder)
    {
        var dir = new DirectoryInfo(folder);
        for (int depth = 0; dir is not null && depth < 8; depth++, dir = dir.Parent)
        {
            string system = Path.Combine(dir.FullName, "System");
            if (File.Exists(Path.Combine(system, ConfigName)) || File.Exists(Path.Combine(system, "SCCT_Versus.exe")) ||
                File.Exists(Path.Combine(system, "Default_kbd_0.ini")))
            {
                return dir.FullName;
            }
        }

        return null;
    }

    /// <summary>Where the install keeps its profiles: the shortcut's target, else the game's usual ProgramData folder.</summary>
    private static string? ProfileFolder(string root)
    {
        string link = Path.Combine(root, "System", "_Profiles_.lnk");
        if (File.Exists(link) && ShortcutTarget(File.ReadAllBytes(link)) is { } target && Directory.Exists(target))
        {
            return target;
        }

        return Directory.Exists(UsualProfileFolder) ? UsualProfileFolder : null;
    }

    /// <summary>The profile played last (the game rewrites it on every run).</summary>
    private static string? NewestProfile(string folder) => new DirectoryInfo(folder)
        .EnumerateFiles("*" + ProfileSuffix).OrderByDescending(f => f.LastWriteTimeUtc).FirstOrDefault()?.FullName;

    private static string? Existing(string path) => File.Exists(path) ? path : null;

    /// <summary>
    /// The local path a Windows shortcut points to (MS-SHLLINK: the header, an optional item list, then LinkInfo with
    /// the local base path and a common path suffix), or null.
    /// </summary>
    internal static string? ShortcutTarget(byte[] link)
    {
        const int Header = 0x4C;
        if (link.Length < Header + 4 || BitConverter.ToUInt32(link, 0) != Header)
        {
            return null;
        }

        uint flags = BitConverter.ToUInt32(link, 0x14);
        int at = Header;
        if ((flags & 1) != 0)
        {
            at += 2 + BitConverter.ToUInt16(link, at);
        }

        if ((flags & 2) == 0 || at + 36 > link.Length)
        {
            return null;
        }

        uint headerSize = BitConverter.ToUInt32(link, at + 4);
        if ((BitConverter.ToUInt32(link, at + 8) & 1) == 0)
        {
            return null;
        }

        string path = headerSize >= 0x24
            ? Chars(at + (int)BitConverter.ToUInt32(link, at + 28), wide: true) + Chars(at + (int)BitConverter.ToUInt32(link, at + 32), wide: true)
            : Chars(at + (int)BitConverter.ToUInt32(link, at + 16), wide: false) + Chars(at + (int)BitConverter.ToUInt32(link, at + 24), wide: false);
        return path.Length > 0 ? path : null;

        string Chars(int start, bool wide)
        {
            if (start < 0 || start >= link.Length)
            {
                return "";
            }

            int step = wide ? 2 : 1;
            int end = start;
            while (end + step <= link.Length && (link[end] != 0 || (wide && link[end + 1] != 0)))
            {
                end += step;
            }

            return (wide ? Encoding.Unicode : Encoding.Latin1).GetString(link, start, end - start);
        }
    }

    // ---- the whole import ------------------------------------------------------------------------------------------

    /// <summary>
    /// Everything the install holds, put onto copies of <paramref name="controls"/> and <paramref name="bindings"/>
    /// (values it does not hold stay as they are), with the kit, Unreal's settings and the report.
    /// </summary>
    public EnhancedImportResult Apply(ControlSettings controls, ControlBindings bindings)
    {
        ArgumentNullException.ThrowIfNull(controls);
        ArgumentNullException.ThrowIfNull(bindings);
        var result = new EnhancedImportResult
        {
            Controls = ControlSettings.FromJson(controls.ToJson()),
            Bindings = bindings.Clone(),
            Loadout = ProfilePath is null ? "" : LoadoutText(out _),
        };

        if (ConfigPath is not null)
        {
            ApplyConfig(result);
        }
        else
        {
            result.Report.Add($"No {ConfigName} (it is in the install's System folder): sensitivities, display and voice settings were not imported.");
        }

        if (ProfilePath is not null)
        {
            ApplyProfile(result);
        }
        else
        {
            result.Report.Add("No game profile found: volumes, gear and key layouts were not imported.");
        }

        result.Controls.Clamp();
        KeyLayout? spy = Keys(merc: false, result.Controls.MouseWheel);
        KeyLayout? merc = Keys(merc: true, result.Controls.MouseWheel);
        if (spy is not null || merc is not null)
        {
            ControlBindings keys = result.Bindings;
            keys.SetSplit(true);
            if (spy is not null)
            {
                Replace(keys, merc: false, spy);
            }

            if (merc is not null)
            {
                Replace(keys, merc: true, merc);
            }

            // One layout for both roles when Reloaded played one: the spy's keys are the merc's too.
            keys.SetSplit(SplitKeyboard);
            result.Report.Add(SplitKeyboard ? "Spy and mercenary key layouts." : "Key layout (one for both roles).");
        }

        return result;
    }

    private static void Replace(ControlBindings bindings, bool merc, KeyLayout layout)
    {
        KeyLayout keys = bindings.Of(merc).Keys;
        foreach (string key in keys.Bindings.Keys.ToArray())
        {
            keys.Bind(key, "");
        }

        foreach ((string key, string command) in layout.Bindings)
        {
            keys.Bind(key, command);
        }
    }

    // ---- key layouts -----------------------------------------------------------------------------------------------

    /// <summary>
    /// The profile's key layout for a side, complete (stock aliases expanded, OpenSCCT's own actions added on free keys
    /// as on a first run), or null when the profile has no key file. With Reloaded's split keyboards off, both sides
    /// get the layout the profile selects (SelectedKeyboard).
    /// </summary>
    public KeyLayout? Keys(bool merc, string wheelMode)
    {
        bool second = SplitKeyboard ? merc : ProfileInt("SelectedKeyboard") == 1;
        string? path = second ? _keys1 ?? _keys0 : _keys0 ?? _keys1;
        if (path is null)
        {
            return null;
        }

        string? stockPath = Root is null ? null : Existing(Path.Combine(Root, "System", second ? "Default_kbd_1.ini" : "Default_kbd_0.ini"));
        IniLayout stock;
        if (stockPath is not null)
        {
            stock = IniLayout.Parse(File.ReadAllText(stockPath, Encoding.Latin1));
        }
        else
        {
            // The profile's key files name the stock aliases without defining them.
            stock = IniLayout.Parse(StockAliases);
            foreach ((string key, string command) in ControlBindings.StockKeys)
            {
                stock.Bindings[key] = command;
            }
        }

        IniLayout profile = IniLayout.Parse(File.ReadAllText(path, Encoding.Latin1));
        IniLayout merged = IniLayout.Overlay(stock, profile);
        var layout = new KeyLayout();
        foreach ((string key, string raw) in merged.Bindings)
        {
            if (raw.Length == 0 || key.Equals("None", StringComparison.OrdinalIgnoreCase) ||
                key.Equals("MouseX", StringComparison.OrdinalIgnoreCase) || key.Equals("MouseY", StringComparison.OrdinalIgnoreCase))
            {
                // The mouse's axes are the look, not a binding of this client; IK_None binds nothing.
                continue;
            }

            layout.Bind(key, string.Join(" | ", merged.Expanded(raw)));
        }

        AddFirstRunActions(layout);
        OpenScct.UnrealBridge.Hub.HubBindings.Upgrade(layout);
        layout.UpgradeWheelBindings(wheelMode);
        return layout;
    }

    /// <summary>
    /// DefaultKeyLayout.ApplyProfile for a profile's layout (an overlay, not a snapshot of OpenSCCT's): each OpenSCCT
    /// action the layout has nowhere goes on its default key when that key is free; the voice keys share theirs.
    /// </summary>
    private static void AddFirstRunActions(KeyLayout layout)
    {
        AddIfFree(layout, "I", "ChangeGear");
        AddIfFree(layout, "G", "SwapWeapon");
        AddIfFree(layout, "N", "ToggleTeammateNames");
        foreach ((string key, string command) in new[] { ("CapsLock", "PushToTalk"), ("Y", "TeamPushToTalk"), ("F8", "ToggleVoiceMute") })
        {
            if (HasAction(layout, command))
            {
                continue;
            }

            string existing = layout.Bindings.TryGetValue(key, out string? raw) ? raw : "";
            layout.Bind(key, existing.Length == 0 ? command : existing + " | " + command);
        }

        AddIfFree(layout, "X", "SwitchSpyPerspective");
        AddIfFree(layout, "K", "Button bCloak");
        AddIfFree(layout, new BindingKey(HackKey(layout), BindingModifiers.Double).Canonical, ControlActions.AutoHackCommand);
    }

    /// <summary>The key that hacks communications (H in the stock layouts): its double tap toggles auto hack.</summary>
    private static string HackKey(KeyLayout layout)
    {
        bool Hacks(string key) => BindingKey.TryParse(key, out BindingKey parsed) && parsed.Modifiers == BindingModifiers.None &&
            !parsed.IsPulse && layout.Actions(key).Any(a => a.Kind == KeyActionKind.Button &&
                a.Name.Equals("bSpecialsChat", StringComparison.OrdinalIgnoreCase));
        return Hacks("H") ? "H" : layout.Bindings.Keys.FirstOrDefault(Hacks) ?? "H";
    }

    private static bool HasAction(KeyLayout layout, string command) =>
        layout.Bindings.Keys.Any(key => layout.Actions(key).Any(a => a.Name.Equals(command, StringComparison.OrdinalIgnoreCase)));

    private static void AddIfFree(KeyLayout layout, string key, string command)
    {
        string action = command.StartsWith("Button ", StringComparison.Ordinal) ? command[7..] : command;
        if (!layout.Bindings.ContainsKey(key) && !HasAction(layout, action))
        {
            layout.Bind(key, command);
        }
    }

    private const string StockAliases = """
        [Engine.Input]
        Aliases[0]=(Command="Button bFire | Function Fire",Alias="Fire")
        Aliases[1]=(Command="Button bAltFire | Function AltFire",Alias="AltFire")
        Aliases[2]=(Command="Button BT_Up",Alias="AliasHaut")
        Aliases[3]=(Command="Button BT_Down",Alias="AliasBas")
        Aliases[4]=(Command="Button BT_Left",Alias="AliasGauche")
        Aliases[5]=(Command="Button BT_Right",Alias="AliasDroite")
        Aliases[6]=(Command="Button BT_PrevItem",Alias="AliasPrevItem")
        Aliases[7]=(Command="Button BT_NextItem",Alias="AliasNextItem")
        Aliases[8]=(Command="Function Jump | Axis aUp Speed=+280.0",Alias="Jump")
        Aliases[9]=(Command="Button bSpecialsChat",Alias="HackComm")
        Aliases[10]=(Command="InvButton bUseBackToWall",Alias="BTW")
        Aliases[11]=(Command="InvButton bFireMode",Alias="FireMode")
        Aliases[12]=(Command="InvButton bUsing",Alias="Use")
        Aliases[13]=(Command="Button bHoldBreath",Alias="HoldBreath")
        Aliases[14]=(Command="Button bCrouch",Alias="Crouch")
        Aliases[15]=(Command="Button bRoll",Alias="Roll")
        Aliases[16]=(Command="Button bReload",Alias="Reload")
        Aliases[17]=(Command="Button bROF",Alias="ROF")
        Aliases[18]=(Command="Button bZoomIn | Function ZoomIn",Alias="ZoomIn")
        Aliases[19]=(Command="Button bZoomOut | Function ZoomOut",Alias="ZoomOut")
        """;

    /// <summary>
    /// The original's key ini as written (Godot's KeyLayout.Parse): the <c>[Engine.Input]</c> aliases and the raw
    /// <c>IK_</c> bindings, an empty one included (it unbinds the key when laid over the stock layout).
    /// </summary>
    private sealed class IniLayout
    {
        public Dictionary<string, string> Aliases { get; } = new(StringComparer.OrdinalIgnoreCase);

        public SortedDictionary<string, string> Bindings { get; } = new(StringComparer.OrdinalIgnoreCase);

        private static readonly Regex s_alias = new(@"Command\s*=\s*""(?<command>[^""]*)""\s*,\s*Alias\s*=\s*""(?<alias>[^""]*)""", RegexOptions.IgnoreCase);

        public static IniLayout Parse(string ini)
        {
            var layout = new IniLayout();
            bool inInput = true;
            foreach (string raw in ini.Replace("\r\n", "\n", StringComparison.Ordinal).Split('\n'))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line.StartsWith(';'))
                {
                    continue;
                }

                if (line.StartsWith('['))
                {
                    inInput = string.Equals(line, "[Engine.Input]", StringComparison.OrdinalIgnoreCase);
                    continue;
                }

                int eq = line.IndexOf('=', StringComparison.Ordinal);
                if (!inInput || eq <= 0)
                {
                    continue;
                }

                string key = line[..eq].Trim();
                string value = line[(eq + 1)..].Trim();
                if (key.StartsWith("Aliases[", StringComparison.OrdinalIgnoreCase))
                {
                    Match m = s_alias.Match(value);
                    if (m.Success && m.Groups["alias"].Value.Length > 0)
                    {
                        layout.Aliases[m.Groups["alias"].Value] = m.Groups["command"].Value;
                    }
                }
                else if (key.StartsWith("IK_", StringComparison.OrdinalIgnoreCase))
                {
                    string input = key[3..];
                    layout.Bindings[BindingKey.TryParse(input, out BindingKey binding) ? binding.Canonical : input] = value;
                }
            }

            return layout;
        }

        /// <summary>
        /// KeyLayout.ApplyOverrides for an old overlay: the stock layout, the user's aliases added, a stock key whose
        /// command the user put on another key unbound, then every user binding (an empty one unbinds).
        /// </summary>
        public static IniLayout Overlay(IniLayout stock, IniLayout user)
        {
            var merged = new IniLayout();
            foreach ((string alias, string command) in stock.Aliases)
            {
                merged.Aliases[alias] = command;
            }

            foreach ((string alias, string command) in user.Aliases)
            {
                merged.Aliases[alias] = command;
            }

            var commands = new HashSet<string>(user.Bindings.Values, StringComparer.OrdinalIgnoreCase);
            foreach ((string key, string command) in stock.Bindings)
            {
                if (!user.Bindings.ContainsKey(key) && commands.Contains(command))
                {
                    continue;
                }

                merged.Bindings[key] = command;
            }

            foreach ((string key, string command) in user.Bindings)
            {
                if (command.Length == 0)
                {
                    merged.Bindings.Remove(key);
                }
                else
                {
                    merged.Bindings[key] = command;
                }
            }

            return merged;
        }

        /// <summary>The binding's parts with the aliases expanded, custom arguments and axis speeds kept.</summary>
        public List<string> Expanded(string command, int depth = 0)
        {
            var parts = new List<string>();
            foreach (string raw in command.Split('|', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
            {
                if (depth < 8 && Aliases.TryGetValue(raw, out string? alias))
                {
                    parts.AddRange(Expanded(alias, depth + 1));
                }
                else
                {
                    parts.Add(raw);
                }
            }

            return parts;
        }
    }

    // ---- settings --------------------------------------------------------------------------------------------------

    private void ApplyConfig(EnhancedImportResult result)
    {
        JsonElement c = _config;
        ControlSettings s = result.Controls;
        List<ImportedSetting> unreal = result.Settings;
        void Set(string key, string value) => unreal.Add(new ImportedSetting(key, value));
        static string On(bool v) => v ? "true" : "false";
        static string Num(float v) => v.ToString("0.###", CultureInfo.InvariantCulture);

        Look(c, "controls_spy_config_kbm", s.SpyKbm, spy: true);
        Look(c, "controls_merc_config_kbm", s.MercKbm, spy: false);
        result.Report.Add("Mouse sensitivities for both roles.");

        Look(c, "controls_spy_config_controller", s.SpyPad, spy: true);
        Look(c, "controls_merc_config_controller", s.MercPad, spy: false);
        PadTuning pad = s.Pad;
        // Controller support is only ever switched on: off in Enhanced says nothing about playing with a pad here.
        if (Flag(c, "controller_enabled") == true)
        {
            pad.Enabled = true;
        }

        Flag(c, "controller_binds_enabled", v => pad.Binds = v);
        Flag(c, "controller_left_stick_enabled", v => pad.Move = v);
        Flag(c, "controller_right_stick_enabled", v => pad.Aim = v);
        Flag(c, "controller_left_stick_slow_walk_enabled", v => pad.SlowWalk = v);
        Flag(c, "controller_left_stick_8_directional_spy", v => pad.SpyMove8 = v);
        Flag(c, "controller_left_stick_8_directional_merc", v => pad.MercMove8 = v);
        Flag(c, "controller_rumble_enabled", v => pad.Rumble = v);
        Number(c, "controller_rumble_strength", v => pad.RumbleStrength = v);
        Number(c, "controller_left_thumb_deadzone", v => pad.DeadzoneLeft = v);
        Number(c, "controller_right_thumb_deadzone", v => pad.DeadzoneRight = v);
        Number(c, "controller_right_thumb_vertical_sensitivty", v => pad.VerticalScale = v);
        Number(c, "controller_walk_threshold", v => pad.RunThreshold = v);
        result.Report.Add("Controller sensitivities and tuning (button layouts are not stored by Reloaded).");

        // Display: OpenSCCT's names for Reloaded's window modes and positions, Unreal's for its own.
        Text(c, "display_mode", v => Set("Display.WindowMode", v switch
        {
            "Fullscreen" => "Fullscreen",
            "Windowed" => "Windowed",
            _ => "Borderless",
        }));
        Number(c, "display_monitor", v => Set("Display.Monitor", ((int)Math.Max(0f, v - 1f)).ToString(CultureInfo.InvariantCulture)));
        Text(c, "window_position", v => Set("Display.WindowPosition", v switch
        {
            "Default" => "Top left",
            "Stretched" => "Fill the screen",
            _ => "Centred",
        }));
        Flag(c, "widescreen", v => s.Widescreen = v);
        Number(c, "ws_fov", v => s.MercFovCap = v);
        Number(c, "resolution_scale", v => Set("Graphics.RenderScale", ((int)MathF.Round(v * 100f)).ToString(CultureInfo.InvariantCulture)));
        Number(c, "filter_level", v => Set("Graphics.TextureFiltering", (int)v switch
        {
            >= 16 => "16x",
            >= 8 => "8x",
            >= 4 => "4x",
            >= 2 => "2x",
            _ => "Trilinear",
        }));
        // Reloaded's MSAA and SMAA have no like in Unreal's deferred renderer: either on asks for its best (TSR).
        bool? msaa = Flag(c, "msaa");
        bool? smaa = Flag(c, "smaa");
        if (msaa is not null || smaa is not null)
        {
            Set("Graphics.AntiAliasing", msaa == true || smaa == true ? "TSR" : "Off");
        }

        Flag(c, "ambient_occlusion", v => Set("Graphics.AmbientOcclusion", On(v)));
        // A spot light's shadow map is 256 x shadow_quality texels (0.5 to 16, 3 on a first run): onto Unreal's four levels.
        Number(c, "shadow_quality", v => Set("Graphics.Shadows", (v <= 1f ? 0 : v <= 2f ? 1 : v <= 4f ? 2 : 3).ToString(CultureInfo.InvariantCulture)));
        Text(c, "low_latency_mode", v => Set("Performance.LowLatency", v));
        // One cap here; Reloaded keeps one per role (the merc's, as older OpenSCCT settings files are read). -1 follows the monitor.
        if (!Number(c, "fps_merc", v => Set("Performance.MaxFps", v < 0 ? "monitor" : ((int)v).ToString(CultureInfo.InvariantCulture))))
        {
            Number(c, "fps_spy", v => Set("Performance.MaxFps", v < 0 ? "monitor" : ((int)v).ToString(CultureInfo.InvariantCulture)));
        }

        result.Report.Add("Display and graphics settings (anti-aliasing and shadows onto Unreal's own choices; the widescreen HUD, small Enhanced Reality, throttle stop and the console key have no counterpart here).");

        bool? net = Flag(c, "show_net_stats");
        Flag(c, "show_fps", v => Set("Hud.FpsMode", v ? "Simple" : "Off"));
        if (net is not null)
        {
            Set("Hud.NetMode", net == true ? (Flag(c, "show_net_loss") == true ? "Detailed" : "Simple") : "Off");
        }

        Flag(c, "classic_crosshair", v => Set("Hud.ClassicCrosshair", On(v)));
        result.Report.Add("HUD settings (the frame rate and network readouts and the classic crosshair; the location line, the hacking icon's opacity and the map playlist have no counterpart here).");

        Flag(c, "voice_push_to_talk", v => Set("Voice.PushToTalk", On(v)));
        Flag(c, "voice_muted", v => Set("Voice.MicMuted", On(v)));
        Number(c, "voice_activation_threshold", v => Set("Voice.Threshold", Num(v)));
        Number(c, "voice_activation_hold_ms", v => Set("Voice.HoldMs", ((int)v).ToString(CultureInfo.InvariantCulture)));
        Number(c, "voice_input_gain", v => Set("Voice.InputGain", Num(v)));
        Flag(c, "voice_auto_gain", v => Set("Voice.AutoGain", On(v)));
        Flag(c, "voice_noise_suppression", v => Set("Voice.NoiseSuppression", On(v)));
        Text(c, "voice_noise_suppression_level", v => Set("Voice.NoiseSuppressionDb", v));
        Flag(c, "voice_echo_cancellation", v => Set("Voice.EchoCancellation", On(v)));
        Number(c, "voice_jitter_frames", v => Set("Voice.JitterFrames", ((int)v).ToString(CultureInfo.InvariantCulture)));
        Number(c, "voice_output_volume", v => Set("Audio.Voice", Percent(v)));
        result.Report.Add("Voice settings (the microphone device is not imported: choose it on the Voice page).");

        if (ProfilePath is null)
        {
            // Reloaded's copies of the profile's volumes.
            Number(c, "z_cache_sound_volume", v => SetSoundVolume(unreal, v, objectives: true));
            Number(c, "z_cache_music_volume", v => SetMusicVolume(unreal, v));
        }
    }

    /// <summary>One of Reloaded's control groups. The spy's has a free look and a gun look, each with a response.</summary>
    private static void Look(JsonElement config, string name, LookSettings look, bool spy)
    {
        if (config.ValueKind != JsonValueKind.Object || !config.TryGetProperty(name, out JsonElement g) || g.ValueKind != JsonValueKind.Object)
        {
            return;
        }

        if (spy)
        {
            Number(g, "free_look", v => look.FreeLook = v);
            if (!Number(g, "gun_look", v => look.Look = v))
            {
                Number(g, "look", v => look.Look = v);
            }

            if (!Text(g, "free_look_response", v => look.LookResponse = v))
            {
                Text(g, "look_response", v => look.LookResponse = v);
            }

            Text(g, "gun_look_response", v => look.GunLookResponse = v);
        }
        else
        {
            Number(g, "look", v => look.Look = v);
            Text(g, "look_response", v => look.LookResponse = v);
            Number(g, "scope_vertical", v => look.ScopeVertical = v);
            Number(g, "clutch_two", v => look.ClutchTwo = v);
            Flag(g, "clutch_two_enabled", v => look.ClutchTwoEnabled = v);
        }

        Number(g, "scope", v => look.Scope = v);
        Number(g, "vertical", v => look.Vertical = v);
        Number(g, "cam", v => look.Cam = v);
        Number(g, "clutch_one", v => look.ClutchOne = v);
        Flag(g, "clutch_one_enabled", v => look.ClutchOneEnabled = v);
        Flag(g, "swap_ls_lb", v => look.SwapLsLb = v);
        Flag(g, "swap_rs_rb", v => look.SwapRsRb = v);
    }

    /// <summary>The player name is left alone: it is set up in OpenSCCT itself.</summary>
    private void ApplyProfile(EnhancedImportResult result)
    {
        ControlSettings s = result.Controls;
        List<ImportedSetting> unreal = result.Settings;
        if (ProfileFlag("InvertY") is { } invertY)
        {
            s.InvertY = invertY;
        }

        if (ProfileFlag("InvertX") is { } invertX)
        {
            s.InvertXSpy = invertX;
        }

        if (ProfileFlag("bInFeet") is { } feet)
        {
            unreal.Add(new ImportedSetting("Hud.DistanceInFeet", feet ? "true" : "false"));
        }

        if (ProfileFlag("allowVoiceChat") is { } voice)
        {
            unreal.Add(new ImportedSetting("Voice.Enabled", voice ? "true" : "false"));
        }

        if (WheelMode is { } wheel)
        {
            s.MouseWheel = wheel;
        }

        bool objectives = ProfileFlag("bObjectiveSounds") ?? true;
        if (ProfileFlag("bObjectiveSounds") is { } objectiveSounds)
        {
            unreal.Add(new ImportedSetting("Audio.ObjectiveSounds", objectiveSounds ? "true" : "false"));
        }

        if (ProfileNumber("iEffectVolume") is { } sound)
        {
            SetSoundVolume(unreal, sound, objectives);
        }

        if (ProfileNumber("iMusicVolume") is { } music)
        {
            SetMusicVolume(unreal, music);
        }

        // Reloaded's own voice volume wins when the config has one.
        if (ConfigPath is null && ProfileNumber("iVoiceChatVolume") is { } voiceVolume)
        {
            unreal.Add(new ImportedSetting("Audio.Voice", Percent(voiceVolume)));
        }

        LoadoutText(out bool gear);
        result.Report.Add($"Profile \"{ProfileName}\": inverted axes, volumes, mouse wheel{(gear ? ", gear" : "")}.");
    }

    /// <summary>The original has one effects volume; it sets the three OpenSCCT split it into.</summary>
    private static void SetSoundVolume(List<ImportedSetting> unreal, float volume, bool objectives)
    {
        unreal.Add(new ImportedSetting("Audio.Effects", Percent(volume)));
        unreal.Add(new ImportedSetting("Audio.MenuSounds", Percent(volume)));
        unreal.Add(new ImportedSetting("Audio.Objective", objectives ? Percent(volume) : "0"));
    }

    private static void SetMusicVolume(List<ImportedSetting> unreal, float volume)
    {
        unreal.Add(new ImportedSetting("Audio.Music", Percent(volume)));
        unreal.Add(new ImportedSetting("Audio.MenuMusic", Percent(volume)));
    }

    /// <summary>A 0..1 volume as Unreal's 0..100.</summary>
    private static string Percent(float volume) => ((int)MathF.Round(Math.Clamp(volume, 0f, 1f) * 100f)).ToString(CultureInfo.InvariantCulture);

    /// <summary>
    /// The profile's gear as a kit text: a side's four gadget slots only when the profile names all four, the merc's
    /// weapon only when it is one (SBase.SWeapon...). <paramref name="any"/> says whether anything was found.
    /// </summary>
    private string LoadoutText(out bool any)
    {
        var parts = new List<string>();
        if (Gear("ATTEquipment") is { } spy)
        {
            parts.Add("spy:" + string.Join(",", spy));
        }

        if (Gear("DEFEquipment") is { } merc)
        {
            parts.Add("merc:" + string.Join(",", merc));
        }

        if (ProfileClass("DEFWeapClass") is { } weapon && weapon.StartsWith("SBase.SWeapon", StringComparison.Ordinal))
        {
            parts.Add("weapon:" + weapon);
        }

        any = parts.Count > 0;
        return string.Join(";", parts);
    }

    private string[]? Gear(string key)
    {
        var classes = new string[LoadoutChoiceSlots];
        for (int i = 0; i < classes.Length; i++)
        {
            if (ProfileClass($"{key}[{i}]") is not { } gadget || !gadget.StartsWith("SBase.SGad", StringComparison.Ordinal))
            {
                return null;
            }

            classes[i] = gadget;
        }

        return classes;
    }

    private const int LoadoutChoiceSlots = OpenScct.UnrealBridge.Loadout.LoadoutChoice.Slots;

    // ---- value readers ---------------------------------------------------------------------------------------------

    private static bool? Flag(JsonElement group, string key) =>
        group.ValueKind == JsonValueKind.Object && group.TryGetProperty(key, out JsonElement v) && v.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? v.GetBoolean() : null;

    private static bool Flag(JsonElement group, string key, Action<bool> set)
    {
        if (Flag(group, key) is not { } value)
        {
            return false;
        }

        set(value);
        return true;
    }

    private static bool Number(JsonElement group, string key, Action<float> set)
    {
        if (group.ValueKind != JsonValueKind.Object || !group.TryGetProperty(key, out JsonElement v) || v.ValueKind != JsonValueKind.Number ||
            !v.TryGetDouble(out double number) || !double.IsFinite(number))
        {
            return false;
        }

        set((float)number);
        return true;
    }

    private static bool Text(JsonElement group, string key, Action<string> set)
    {
        if (group.ValueKind != JsonValueKind.Object || !group.TryGetProperty(key, out JsonElement v) || v.ValueKind != JsonValueKind.String ||
            v.GetString() is not { Length: > 0 } text)
        {
            return false;
        }

        set(text);
        return true;
    }

    private bool? ProfileFlag(string key) =>
        _profile.TryGetValue(key, out string? v) && bool.TryParse(v, out bool flag) ? flag : null;

    private int? ProfileInt(string key) =>
        _profile.TryGetValue(key, out string? v) && int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) ? n : null;

    private float? ProfileNumber(string key) =>
        _profile.TryGetValue(key, out string? v) && float.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out float n) && float.IsFinite(n) ? n : null;

    /// <summary>
    /// A class value as the profile writes it, <c>Class§(677)'SBase§(180).SGadSmokeGrenade§(707695)'</c> (each name
    /// followed by a marker byte and a number), as a plain class path, or null.
    /// </summary>
    private string? ProfileClass(string key)
    {
        if (!_profile.TryGetValue(key, out string? v))
        {
            return null;
        }

        Match m = Regex.Match(Regex.Replace(v, @"[^\x20-\x7E]\(\d+\)", ""), @"'([A-Za-z0-9_.]+)'");
        return m.Success ? m.Groups[1].Value : null;
    }
}
