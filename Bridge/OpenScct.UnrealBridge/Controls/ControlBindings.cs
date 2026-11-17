using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// A named action a controller button (or a bindings menu) can pick, and the command in the original's binding
/// grammar it stands for. The ids and commands are OpenSCCT's (game/Scripts/UI/ControlActions.cs); the hub's own
/// (category "Hub") come from Hub/HubBindings.cs.
/// </summary>
public sealed record ControlAction(string Id, string Category, string Label, string Command, bool Spy = true, bool Merc = true);

public static class ControlActions
{
    /// <summary>The prefix of a pad binding that names an action (<c>Action:jump</c>).</summary>
    public const string PadPrefix = "Action:";

    /// <summary>
    /// The prefix of the made-up key a pad control holds while its action is down
    /// (<c>svm-action:&lt;control&gt;:&lt;action id&gt;</c>): the input mapper treats it as a key bound to the action's
    /// command, so the pad works without any keyboard binding.
    /// </summary>
    public const string VirtualPrefix = "svm-action:";

    public const string AutoHackCommand = "ToggleAutoHack";

    public static readonly IReadOnlyList<ControlAction> All =
    [
        new("forward", "Movement", "Move forward", "Button BT_Up"),
        new("back", "Movement", "Move backward", "Button BT_Down"),
        new("left", "Movement", "Move left", "Button BT_Left"),
        new("right", "Movement", "Move right", "Button BT_Right"),
        new("jump", "Movement", "Jump / contextual traversal", "Function Jump | Axis aUp Speed=+280.0"),
        new("crouch", "Movement", "Crouch", "Button bCrouch"),
        new("roll", "Movement", "Roll", "Button bRoll", Merc: false),
        new("use", "Movement", "Interact / action menu", "InvButton bUsing"),
        new("wall", "Movement", "Back to wall", "InvButton bUseBackToWall", Merc: false),
        new("coop", "Movement", "Co-op move", "PerformCoopMvt", Merc: false),
        new("speed", "Movement", "Walk", "Walk"),
        new("longroll", "Movement", "Long roll", "LongRoll | Button bRoll", Merc: false),
        new("fire", "Combat", "Primary fire / contextual attack", "Button bFire | Function Fire"),
        new("altfire", "Combat", "Use selected gadget / alternate action", "Button bAltFire | Function AltFire"),
        new("grab", "Combat", "Grab only", "GrabOnly", Merc: false),
        new("ready", "Combat", "Ready / holster weapon", "InvButton bFireMode", Merc: false),
        new("reload", "Combat", "Reload", "Button bReload"),
        new("swapweapon", "Combat", "Swap weapon", "SwapWeapon", Spy: false),
        new("rof", "Combat", "Switch fire mode", "Button bROF", Spy: false),
        new("breath", "Combat", "Hold breath", "Button bHoldBreath", Spy: false),
        new("cloak", "Combat", "Cloak", "Button bCloak", Merc: false),
        new("cloakquick", "Combat", "Toggle cloak", "ToggleCloak", Merc: false),
        new("quick1", "Gadgets", "Quick-use gadget slot 1", "GadgetQuickChoice 0", Merc: false),
        new("quick2", "Gadgets", "Quick-use gadget slot 2", "GadgetQuickChoice 1", Merc: false),
        new("launch1", "Gadgets", "Launch gadget slot 1", "LaunchGadget 0", Merc: false),
        new("launch2", "Gadgets", "Launch gadget slot 2", "LaunchGadget 1", Merc: false),
        new("launch3", "Gadgets", "Launch gadget slot 3", "LaunchGadget 2", Merc: false),
        new("launch4", "Gadgets", "Launch gadget slot 4", "LaunchGadget 3", Merc: false),
        new("cameragas", "Gadgets", "Release sticky camera gas", "PadCameraGas", Merc: false),
        new("cameraexit", "Gadgets", "Leave camera", "PadCameraExit"),
        new("slot1", "Gadgets", "Select gadget slot 1", "SelectGadget 0"),
        new("slot2", "Gadgets", "Select gadget slot 2", "SelectGadget 1"),
        new("slot3", "Gadgets", "Select gadget slot 3", "SelectGadget 2"),
        new("slot4", "Gadgets", "Select gadget slot 4", "SelectGadget 3"),
        Gadget("smoke", "Smoke Grenade", SGadget.aSGadSmokeGrenade),
        Gadget("chaff", "Chaff Grenade", SGadget.aSGadChaffeGrenade),
        Gadget("flashbang", "Flashbang", SGadget.aSGadFlashBang),
        Gadget("stickycam", "Sticky Camera", SGadget.aSGadStickyCam),
        Gadget("spybullet", "Spy Bullet", SGadget.aSGadSpyBulletLauncher),
        Gadget("alarmsnare", "Alarm Snare", SGadget.aSGadNoiseEmitter),
        Gadget("heartbeat", "Heartbeat Sensor", SGadget.BindHeartbeatSensor),
        Gadget("clone", "Clone", SGadget.BindClone),
        Gadget("minehack", "Mine Hack", SGadget.BindMineHack),
        Gadget("frag", "Frag Grenade", SGadget.aSGadFragGrenade, merc: true),
        Gadget("flare", "Flare", SGadget.aSGadFlareLauncher, merc: true),
        Gadget("mines", "Mines", SGadget.aSGadMultiMinesLauncher, merc: true),
        Gadget("presencedetector", "Presence Detector", SGadget.aSGadMultiPresDetectorLauncher, merc: true),
        Gadget("taser", "Taser", SGadget.aSGadTazerLauncher, merc: true),
        Gadget("cameranetwork", "Camera Network", SGadget.aSGadCamNetwork, merc: true),
        Gadget("gasmask", "Gas Mask", SGadget.aSGadGasMask, merc: true),
        Gadget("backpack", "Backpack", SGadget.aSGadBackPack, merc: true),
        new("next", "Gadgets", "Next gadget", "NextGadget"),
        new("previous", "Gadgets", "Previous gadget", "PreviousGadget"),
        new("laser", "Gadgets", "Toggle laser gadget", "GadLaserSwitch", Spy: false),
        new("torch", "Gadgets", "Toggle flashlight gadget", "GadTorcheSwitch", Spy: false),
        new("weaponlaser", "Gadgets", "Toggle weapon laser", "SwitchLaser", Merc: false),
        new("sticky", "Gadgets", "Previous sticky camera", "PrevStickyCam", Merc: false),
        new("vision1", "Vision", "First vision mode", "SwitchHeadset 1"),
        new("vision2", "Vision", "Second vision mode", "SwitchHeadset 2"),
        new("visioncycle", "Vision", "Cycle vision modes", "SwitchHeadset 3"),
        new("scope", "Vision", "Binoculars / sniper scope", "SwitchBinocularVision"),
        new("perspective", "Vision", "First / third person", "SwitchSpyPerspective", Merc: false),
        new("firstperson", "Vision", "First person", "SetSpyFirstPerson 1", Merc: false),
        new("thirdperson", "Vision", "Third person", "SetSpyFirstPerson 0", Merc: false),
        new("zoomin", "Vision", "Zoom in", "Button bZoomIn | Function ZoomIn"),
        new("zoomout", "Vision", "Zoom out", "Button bZoomOut | Function ZoomOut"),
        new("er", "Vision", "Toggle Enhanced Reality", "SwitchEnhancedReality"),
        new("hackcomms", "Communication", "Hack enemy communications", "Button bSpecialsChat"),
        new("autohack", "Communication", "Toggle auto-hack", AutoHackCommand),
        new("chat", "Communication", "Chat", "Talk"),
        new("teamchat", "Communication", "Team chat", "TeamTalk"),
        new("quickchat", "Communication", "Quick chat", "QuickChat"),
        new("ping", "Communication", "Team ping", "TeamPing"),
        new("voice", "Communication", "Push to talk", "PushToTalk"),
        new("teamvoice", "Communication", "Team push to talk", "TeamPushToTalk"),
        new("mute", "Communication", "Toggle microphone mute", "ToggleVoiceMute"),
        new("map", "Interface", "Show strategic map", "DisplayStrategicMap"),
        new("session", "Interface", "Show session status", "DisplaySessionStatus"),
        new("players", "Interface", "Toggle scoreboard", "ShowConnectedPlayers"),
        new("scoreboard_hold", "Interface", "Hold scoreboard", "HoldScoreboard"),
        new("teammatenames", "Interface", "Toggle teammate names", "ToggleTeammateNames"),
        new("gear", "Interface", "Change gear", "ChangeGear"),
        new("hidehud", "Interface", "Toggle HUD visibility", "HideHud"),
        new("freecam", "Interface", "Toggle cinematic free camera", "FreeCam"),
        new("menu", "Interface", "Open game menu", "ShowMenu"),
        new("grabarea", "Interface", "Practice: show grab areas", "GrabArea"),
        .. OpenScct.UnrealBridge.Hub.HubBindings.Actions,
    ];

    private static readonly Dictionary<string, List<KeyAction>> s_resolved = Build();

    /// <summary>The parts of the action a virtual key (<see cref="VirtualPrefix"/>) holds, empty for anything else.</summary>
    public static IReadOnlyList<KeyAction> VirtualActions(string key)
    {
        if (!key.StartsWith(VirtualPrefix, StringComparison.Ordinal))
        {
            return Array.Empty<KeyAction>();
        }

        int colon = key.LastIndexOf(':');
        return s_resolved.GetAlternateLookup<ReadOnlySpan<char>>().TryGetValue(key.AsSpan(colon + 1), out List<KeyAction>? actions)
            ? actions
            : Array.Empty<KeyAction>();
    }

    public static bool Known(string id) => s_resolved.ContainsKey(id);

    private static ControlAction Gadget(string id, string label, int gadget, bool merc = false) =>
        new("gadget_" + id, "Gadgets", label, "UseGadget " + gadget.ToString(CultureInfo.InvariantCulture), Spy: !merc, Merc: merc);

    private static Dictionary<string, List<KeyAction>> Build()
    {
        var resolved = new Dictionary<string, List<KeyAction>>(StringComparer.Ordinal);
        foreach (ControlAction action in All)
        {
            resolved[action.Id] = KeyLayout.Resolve(action.Command);
        }

        return resolved;
    }
}

/// <summary>The bindings of one side: its key layout and its controller buttons.</summary>
public sealed class SideBindings
{
    public SideBindings(KeyLayout keys, Dictionary<string, string> pad)
    {
        Keys = keys;
        Pad = pad;
    }

    /// <summary>Keyboard and mouse.</summary>
    public KeyLayout Keys { get; }

    /// <summary>
    /// Controller: control (<see cref="ControlBindings.PadControls"/>) to what it does, "" for nothing. Replaced by
    /// a new dictionary on every change (readers compare the reference).
    /// </summary>
    public Dictionary<string, string> Pad { get; private set; }

    public void SetPad(string control, string action)
    {
        Pad = new Dictionary<string, string>(Pad, StringComparer.Ordinal) { [control] = action ?? "" };
    }
}

/// <summary>
/// Every binding of the game as data: per side, a key layout and a controller layout. The defaults are OpenSCCT's
/// (the SCCT Enhanced keyboard layout of game/Scripts/Input/DefaultKeyLayout.cs with the additions a first run gets,
/// and Reloaded's native controller layout of game/Scripts/Options/GameSettings.cs PadSettings.DefaultButtons).
/// Saved as JSON where the caller says, so a bindings menu can edit and keep them.
/// </summary>
public sealed class ControlBindings
{
    /// <summary>
    /// The stock keyboard and mouse layout. Enhanced's Default_kbd_0.ini (spy) and Default_kbd_1.ini (merc) resolve
    /// to the same bindings, so both sides start from this one table; a key that means something for one side only
    /// (Q rolls the spy and switches the merc's laser gadget) carries both commands.
    /// </summary>
    public static readonly IReadOnlyList<(string Key, string Command)> StockKeys =
    [
        ("W", "Button BT_Up"),
        ("S", "Button BT_Down"),
        ("A", "Button BT_Left"),
        ("D", "Button BT_Right"),
        ("Space", "Function Jump | Axis aUp Speed=+280.0"),
        ("C", "Button bCrouch"),
        ("Q", "Button bRoll | GadLaserSwitch"),
        ("Shift", "InvButton bUsing"),
        ("Z", "InvButton bUseBackToWall"),
        ("T", "PerformCoopMvt"),
        ("MiddleMouse", "ChangeSpeed"),
        ("LeftMouse", "Button bFire | Function Fire | GadgetQuickChoice 0"),
        ("RightMouse", "Button bAltFire | Function AltFire | GadgetQuickChoice 1 | Button bHoldBreath"),
        ("E", "InvButton bFireMode | GadTorcheSwitch"),
        ("R", "Button bReload"),
        ("F", "Button bROF"),
        ("H", "Button bSpecialsChat"),
        ("B", "SwitchBinocularVision"),
        ("L", "SwitchLaser"),
        ("V", "PrevStickyCam"),
        ("PageUp", "Button bZoomIn | Function ZoomIn"),
        ("PageDown", "Button bZoomOut | Function ZoomOut"),
        ("1", "SelectGadget 0"),
        ("2", "SelectGadget 1"),
        ("3", "SelectGadget 2"),
        ("4", "SelectGadget 3"),
        ("5", "Button BT_PrevItem"),
        ("6", "Button BT_NextItem"),
        ("MouseWheelUp", "WheelUp"),
        ("MouseWheelDown", "WheelDown"),
        ("Tab", "SwitchHeadset 3"),
        ("F1", "SwitchHeadset 1"),
        ("F2", "SwitchHeadset 2"),
        ("F3", "DisplayDefaultControls"),
        ("F4", "DisplayHud"),
        ("F5", "Talk"),
        ("F6", "TeamTalk"),
        ("F7", "DisplayStrategicMap"),
        ("F8", "DisplaySessionStatus"),
        ("F9", "SwitchEnhancedReality"),
        ("F10", "ShowConnectedPlayers"),
        ("F11", "Quit"),
        ("Escape", "ShowMenu"),
    ];

    /// <summary>
    /// What OpenSCCT adds to the stock layout on a first run (DefaultKeyLayout.ApplyProfile, HubBindings.Upgrade),
    /// each on a key the stock layout leaves free; F8 keeps its stock command and also mutes the microphone.
    /// </summary>
    public static readonly IReadOnlyList<(string Key, string Command)> AddedKeys =
    [
        ("I", "ChangeGear"),
        ("G", "SwapWeapon"),
        ("N", "ToggleTeammateNames"),
        ("CapsLock", "PushToTalk"),
        ("Y", "TeamPushToTalk"),
        ("F8", "DisplaySessionStatus | ToggleVoiceMute"),
        ("X", "SwitchSpyPerspective"),
        ("K", "Button bCloak"),
        ("Double+H", ControlActions.AutoHackCommand),
        ("Enter", "Talk"),
    ];

    /// <summary>The pad controls a layout binds (Xbox names), then the held button and the combinations.</summary>
    public static readonly string[] PadControls =
    [
        "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Start", "Back",
        "P1", "P2", "P3", "P4",
        "B:Hold", "LS+Y", "LS+X", "LS+B", "LS+A", "LB+Y", "LB+X", "LB+B", "LB+A", "LB+DpadDown", "LB+Back", "LB+RB",
    ];

    /// <summary>The controls that are buttons or triggers of the controller itself.</summary>
    public const int PhysicalPadControls = 20;

    /// <summary>The pad bind held for the first sensitivity clutch.</summary>
    public const string ClutchOneAction = "Clutch1";

    /// <summary>The pad bind held for the second clutch (the merc's).</summary>
    public const string ClutchTwoAction = "Clutch2";

    /// <summary>The prefix of a pad binding that presses a key of the side's key layout (<c>Key:F7</c>).</summary>
    public const string KeyPrefix = "Key:";

    // Pad bindings whose action depends on what the player is doing (PadActionState).
    public const string WallLaserAction = "Pad:walllaser";
    public const string SpyFireAction = "Pad:spyfire";
    public const string SpyGadgetAction = "Pad:spygadget";
    public const string MercGadgetAction = "Pad:mercgadget";
    public const string CrouchAction = "Pad:crouch";
    public const string LongRollAction = "Pad:longroll";

    public ControlBindings()
        : this("Gadget")
    {
    }

    /// <summary>The defaults, with the wheel doing <paramref name="wheelMode"/> ("Gadget" or "Speed").</summary>
    public ControlBindings(string wheelMode)
    {
        Spy = new SideBindings(DefaultKeys(wheelMode), DefaultPad(merc: false));
        Merc = new SideBindings(DefaultKeys(wheelMode), DefaultPad(merc: true));
    }

    private ControlBindings(SideBindings spy, SideBindings merc)
    {
        Spy = spy;
        Merc = merc;
    }

    public SideBindings Spy { get; private set; }

    public SideBindings Merc { get; private set; }

    public SideBindings Of(bool merc) => merc ? Merc : Spy;

    /// <summary>
    /// The spy and the merc have a key layout each (true, the default), or both play the spy's (false: the Godot
    /// client's use_split_spy_merc_keyboard off). The controller's buttons are always per side.
    /// </summary>
    public bool Split { get; set; } = true;

    /// <summary>The key layout a side plays: its own, or the spy's while the layouts are shared.</summary>
    public KeyLayout KeysOf(bool merc) => (merc && !Split ? Spy : Of(merc)).Keys;

    /// <summary>
    /// Shares one layout between the sides or gives each its own. Going to a shared layout copies the spy's keys over
    /// the merc's, so splitting again later starts both sides from the layout that was played.
    /// </summary>
    public void SetSplit(bool split)
    {
        if (!split)
        {
            Merc = new SideBindings(Spy.Keys.Clone(), Merc.Pad);
        }

        Split = split;
    }

    /// <summary>A copy that shares nothing with this one, so it can be edited apart.</summary>
    public ControlBindings Clone() =>
        new(new SideBindings(Spy.Keys.Clone(), new Dictionary<string, string>(Spy.Pad, StringComparer.Ordinal)),
            new SideBindings(Merc.Keys.Clone(), new Dictionary<string, string>(Merc.Pad, StringComparer.Ordinal))) { Split = Split };

    /// <summary>Puts one side back to the defaults (with a shared layout, the keys of both).</summary>
    public void Reset(bool merc, string wheelMode = "Gadget")
    {
        var side = new SideBindings(DefaultKeys(wheelMode), DefaultPad(merc));
        if (merc) Merc = side; else Spy = side;
        if (!Split)
        {
            // One layout for both: its keys go back to the defaults whichever side asked; the other side's pad stays.
            if (merc) Spy = new SideBindings(DefaultKeys(wheelMode), Spy.Pad);
            SetSplit(false);
        }
    }

    /// <summary>Puts one side's keys (not its controller) back to the defaults; with a shared layout, the shared keys.</summary>
    public void ResetKeys(bool merc, string wheelMode = "Gadget")
    {
        if (merc && Split) Merc = new SideBindings(DefaultKeys(wheelMode), Merc.Pad);
        else Spy = new SideBindings(DefaultKeys(wheelMode), Spy.Pad);
        if (!Split)
        {
            SetSplit(false);
        }
    }

    /// <summary>The default key layout of either side.</summary>
    public static KeyLayout DefaultKeys(string wheelMode = "Gadget")
    {
        var layout = new KeyLayout();
        foreach ((string key, string command) in StockKeys)
        {
            layout.Bind(key, command);
        }

        foreach ((string key, string command) in AddedKeys)
        {
            layout.Bind(key, command);
        }

        layout.UpgradeWheelBindings(wheelMode);
        OpenScct.UnrealBridge.Hub.HubBindings.Upgrade(layout);
        return layout;
    }

    /// <summary>Reloaded's native controller layout, including the held button and the combinations.</summary>
    public static Dictionary<string, string> DefaultPad(bool merc) => new(StringComparer.Ordinal)
    {
        ["A"] = "Action:use",
        ["B"] = CrouchAction,
        ["X"] = merc ? "Action:reload" : "Action:ready",
        ["Y"] = "Action:jump",
        ["LB"] = merc ? ClutchOneAction : "Action:coop",
        ["RB"] = merc ? "Action:scope" : "Action:roll",
        ["LT"] = merc ? MercGadgetAction : SpyGadgetAction,
        ["RT"] = merc ? "Action:fire" : SpyFireAction,
        ["LS"] = "",
        ["RS"] = merc ? ClutchTwoAction : ClutchOneAction,
        ["DpadUp"] = merc ? "Action:torch" : "Action:sticky",
        ["DpadDown"] = merc ? "Action:laser" : WallLaserAction,
        ["DpadLeft"] = "Action:vision1",
        ["DpadRight"] = "Action:vision2",
        ["Start"] = "Action:menu",
        ["Back"] = "Action:hackcomms",
        ["P1"] = "",
        ["P2"] = "",
        ["P3"] = "",
        ["P4"] = "",
        ["B:Hold"] = merc ? "" : LongRollAction,
        ["LS+Y"] = "Action:slot1",
        ["LS+X"] = "Action:slot2",
        ["LS+B"] = "Action:slot3",
        ["LS+A"] = "Action:slot4",
        ["LB+Y"] = merc ? "" : "Action:launch1",
        ["LB+X"] = merc ? "" : "Action:launch2",
        ["LB+B"] = merc ? "" : "Action:launch3",
        ["LB+A"] = merc ? "" : "Action:launch4",
        ["LB+DpadDown"] = merc ? "Action:rof" : "Action:scope",
        ["LB+Back"] = "Action:session",
        ["LB+RB"] = merc ? "" : "Action:cloak",
    };

    // ---- JSON ---------------------------------------------------------------------------------------------------

    private sealed class SideFile
    {
        [JsonPropertyName("keys")]
        public SortedDictionary<string, string>? Keys { get; set; }

        [JsonPropertyName("pad")]
        public Dictionary<string, string>? Pad { get; set; }

        /// <summary>The bindings editor's order of each action's keys (only written when there is one).</summary>
        [JsonPropertyName("order")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public SortedDictionary<string, string[]>? Order { get; set; }
    }

    private sealed class BindingsFile
    {
        [JsonPropertyName("version")]
        public int Version { get; set; } = 1;

        /// <summary>Each side has its own key layout (absent in older files: split).</summary>
        [JsonPropertyName("split")]
        public bool Split { get; set; } = true;

        [JsonPropertyName("spy")]
        public SideFile? Spy { get; set; }

        [JsonPropertyName("merc")]
        public SideFile? Merc { get; set; }

        /// <summary>The hub keys' version the layouts have (HubBindings.Version); 0 in a file from before them.</summary>
        [JsonPropertyName("hub")]
        public int Hub { get; set; }
    }

    public string ToJson() => JsonSerializer.Serialize(
        new BindingsFile { Split = Split, Spy = FileOf(Spy), Merc = FileOf(Merc), Hub = OpenScct.UnrealBridge.Hub.HubBindings.Version }, ControlSettings.Json);

    /// <summary>
    /// Reads bindings. A side's "keys" is the whole layout (a key left out is unbound, as the player left it); a
    /// side or a table the file does not have keeps the defaults; a pad control left out keeps its default.
    /// </summary>
    public static ControlBindings FromJson(string json, string wheelMode = "Gadget")
    {
        BindingsFile file = JsonSerializer.Deserialize<BindingsFile>(json, ControlSettings.Json) ?? new BindingsFile();
        var bindings = new ControlBindings(SideOf(file.Spy, merc: false, wheelMode, file.Hub), SideOf(file.Merc, merc: true, wheelMode, file.Hub));
        bindings.SetSplit(file.Split);
        return bindings;
    }

    /// <summary>Reads the bindings file; the defaults when there is none.</summary>
    public static ControlBindings Load(string path, string wheelMode = "Gadget") =>
        File.Exists(path) ? FromJson(File.ReadAllText(path), wheelMode) : new ControlBindings(wheelMode);

    public void Save(string path) => WriteWhole(path, ToJson());

    /// <summary>Writes a file through a temporary one, so a crash cannot leave half of it.</summary>
    internal static void WriteWhole(string path, string text)
    {
        string? folder = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(folder))
        {
            Directory.CreateDirectory(folder);
        }

        string temp = path + ".tmp";
        File.WriteAllText(temp, text);
        File.Move(temp, path, overwrite: true);
    }

    private static SideFile FileOf(SideBindings side) => new()
    {
        Keys = new SortedDictionary<string, string>(new Dictionary<string, string>(side.Keys.Bindings), StringComparer.OrdinalIgnoreCase),
        Pad = side.Pad,
        Order = side.Keys.InputOrders.Count == 0 ? null : new SortedDictionary<string, string[]>(new Dictionary<string, string[]>(side.Keys.InputOrders), StringComparer.Ordinal),
    };

    private static SideBindings SideOf(SideFile? file, bool merc, string wheelMode, int hubVersion)
    {
        KeyLayout keys;
        if (file?.Keys is { } saved)
        {
            keys = new KeyLayout();
            foreach ((string key, string command) in saved)
            {
                // A key name that cannot be one is left out; the rest of the file still loads.
                keys.Bind(key, command);
            }

            keys.UpgradeWheelBindings(wheelMode);

            // A layout saved before the hub's keys gets them once; after that a hub key the player unbound stays unbound.
            if (hubVersion < OpenScct.UnrealBridge.Hub.HubBindings.Version)
            {
                OpenScct.UnrealBridge.Hub.HubBindings.Upgrade(keys);
            }
        }
        else
        {
            keys = DefaultKeys(wheelMode);
        }

        if (file?.Order is { } order)
        {
            foreach ((string action, string[] inputs) in order)
            {
                if (inputs is not null)
                {
                    keys.SetInputOrder(action, inputs);
                }
            }
        }

        Dictionary<string, string> pad = DefaultPad(merc);
        if (file?.Pad is { } savedPad)
        {
            foreach (string control in PadControls)
            {
                if (savedPad.TryGetValue(control, out string? action))
                {
                    pad[control] = action ?? "";
                }
            }
        }

        return new SideBindings(keys, pad);
    }
}
