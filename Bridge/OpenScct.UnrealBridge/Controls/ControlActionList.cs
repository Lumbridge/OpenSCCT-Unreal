using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// An action as the key bindings editor lists it: its label and what it does, the command a key gets for it, and the
/// parts of a binding that count as it (<see cref="Tokens"/>, compared by <see cref="ControlActionList.Token"/>), so
/// a key whose binding holds several actions ("Button bFire | Function Fire | GadgetQuickChoice 0") shows on each.
/// </summary>
public sealed record ControlActionInfo(string Id, string Category, string Label, string Description, string Command,
    string[] Tokens, bool Spy = true, bool Merc = true, bool Available = true)
{
    public bool Matches(string part)
    {
        string token = ControlActionList.Token(part);
        foreach (string t in Tokens)
        {
            if (string.Equals(t, token, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }
}

/// <summary>
/// The actions of the key bindings editor, in the Godot client's order with its labels and descriptions
/// (game/Scripts/UI/ControlActions.cs, verified there against LocalInput and SPlayerController's handlers). The hub's
/// actions are left out (there is no hub in this client). <see cref="ControlActions"/> keeps the shorter list the
/// controller's bindings resolve through.
/// </summary>
public static class ControlActionList
{
    public const string PingId = "ping";
    public const string MineHackId = "gadget_minehack";
    public const string PingCommand = "TeamPing";
    public const string AutoHackId = "autohack";

    public static readonly IReadOnlyList<ControlActionInfo> All =
    [
        A("forward", "Movement", "Move forward", "Hold to move forward.", "Button BT_Up", "BT_Up"),
        A("back", "Movement", "Move backward", "Hold to move backward.", "Button BT_Down", "BT_Down"),
        A("left", "Movement", "Move left", "Hold to move left.", "Button BT_Left", "BT_Left"),
        A("right", "Movement", "Move right", "Hold to move right.", "Button BT_Right", "BT_Right"),
        A("jump", "Movement", "Jump / contextual traversal", "Jump or perform the traversal available in your current position.", "Function Jump | Axis aUp Speed=+280.0", "Jump", "aUp"),
        A("crouch", "Movement", "Crouch", "Crouch; behavior can depend on your movement state.", "Button bCrouch", "bCrouch"),
        A("roll", "Movement", "Roll", "Roll from a crouch or dive-roll while running, when movement and clearance allow it.", "Button bRoll", "bRoll") with { Merc = false },
        A("use", "Movement", "Interact / action menu", "Use nearby objects or interact with a player. Hold where an interaction or action menu requires it.", "InvButton bUsing", "bUsing"),
        A("wall", "Movement", "Back to wall", "Enter or leave a back-to-wall position where supported.", "InvButton bUseBackToWall", "bUseBackToWall") with { Merc = false },
        A("coop", "Movement", "Co-op move", "Request the co-op movement available with your teammate.", "PerformCoopMvt", "PerformCoopMvt") with { Merc = false },
        A("run_compat", "Movement", "Run (saved shortcut)", "Compatibility for a previously configured run shortcut.", "Run", "Run") with { Available = false },
        A("speed", "Movement", "Walk", "Run by default. Hold or toggle Walk using the behavior selected for this role. Wheel bindings always toggle. Contextual scrolling takes priority in zoom views and interaction menus.", "Walk", "Walk", "ChangeSpeed"),
        A("fire", "Combat", "Primary fire / contextual attack", "Fire the weapon or perform the primary action allowed by your current combat state.", "Button bFire | Function Fire", "bFire", "Fire"),
        A("altfire", "Combat", "Use selected gadget / alternate action", "Use the selected gadget or the alternate action of the current state. Grenade use can require holding and releasing.", "Button bAltFire | Function AltFire", "bAltFire", "AltFire"),
        A("grab", "Combat", "Grab only", "Grab a mercenary from behind, or pull one over the ledge you hang from. Unlike Interact, it never revives or elbows when no grab is possible. Starts unbound.", "GrabOnly", "GrabOnly") with { Merc = false },
        A("ready", "Combat", "Ready / holster weapon", "With the weapon holstered, tap to ready it with the laser; hold longer than 0.5 seconds to ready it without the laser. Press again to leave weapon mode where permitted.", "InvButton bFireMode", "bFireMode") with { Merc = false },
        A("reload", "Combat", "Reload", "Reload the current weapon when permitted.", "Button bReload", "bReload"),
        A("swapweapon", "Combat", "Swap weapon", "Switch between your primary and carried pistol. A pistol occupies one gadget slot.", "SwapWeapon", "SwapWeapon") with { Spy = false },
        A("rof", "Combat", "Switch fire mode", "Switch the mercenary rifle's firing mode (burst / full auto), once per press.", "Button bROF", "bROF", "SwitchROF") with { Spy = false },
        A("breath", "Combat", "Hold breath", "Hold to steady the mercenary sniper scope when available.", "Button bHoldBreath", "bHoldBreath") with { Spy = false },
        A("cloak", "Combat", "Cloak", "Use the spy's built-in camo suit, independently of gadgets. Hold activates on press and deactivates on release; Toggle switches on each press. Choose the behavior for this action. Wheel bindings always toggle.", "Button bCloak", "bCloak") with { Merc = false },
        A("quick1", "Gadgets", "Quick-use gadget slot 1", "While the spy is unarmed, quick-launch a supported gadget from slot 1.", "GadgetQuickChoice 0", "GadgetQuickChoice 0") with { Merc = false },
        A("quick2", "Gadgets", "Quick-use gadget slot 2", "While the spy is unarmed, quick-launch a supported gadget from slot 2.", "GadgetQuickChoice 1", "GadgetQuickChoice 1") with { Merc = false },
        A("launch1", "Gadgets", "Launch gadget slot 1", "Select and fire gadget slot 1 with the weapon drawn.", "LaunchGadget 0", "LaunchGadget 0") with { Merc = false },
        A("launch2", "Gadgets", "Launch gadget slot 2", "Select and fire gadget slot 2 with the weapon drawn.", "LaunchGadget 1", "LaunchGadget 1") with { Merc = false },
        A("launch3", "Gadgets", "Launch gadget slot 3", "Select and fire gadget slot 3 with the weapon drawn.", "LaunchGadget 2", "LaunchGadget 2") with { Merc = false },
        A("launch4", "Gadgets", "Launch gadget slot 4", "Select and fire gadget slot 4 with the weapon drawn.", "LaunchGadget 3", "LaunchGadget 3") with { Merc = false },
        A("longroll", "Movement", "Long roll", "Stand from crouch when needed and roll.", "LongRoll | Button bRoll", "LongRoll") with { Merc = false, Available = false },
        A("cloakquick", "Combat", "Toggle cloak", "Toggle the spy's built-in camo suit.", "ToggleCloak", "ToggleCloak", "PadCloakQuick") with { Merc = false, Available = false },
        A("cameragas", "Gadgets", "Release sticky camera gas", "Release gas from the camera being used.", "PadCameraGas", "PadCameraGas") with { Merc = false, Available = false },
        A("cameraexit", "Gadgets", "Leave camera", "Leave the current sticky camera or camera network view.", "PadCameraExit", "PadCameraExit") with { Available = false },
        A("slot1", "Gadgets", "Select gadget slot 1", "Select the gadget in slot 1.", "SelectGadget 0", "SelectGadget 0"),
        A("slot2", "Gadgets", "Select gadget slot 2", "Select the gadget in slot 2.", "SelectGadget 1", "SelectGadget 1"),
        A("slot3", "Gadgets", "Select gadget slot 3", "Select the gadget in slot 3.", "SelectGadget 2", "SelectGadget 2"),
        A("slot4", "Gadgets", "Select gadget slot 4", "Select the gadget in slot 4.", "SelectGadget 3", "SelectGadget 3"),
        G("smoke", "Smoke Grenade", SGadget.aSGadSmokeGrenade),
        G("chaff", "Chaff Grenade", SGadget.aSGadChaffeGrenade),
        G("flashbang", "Flashbang", SGadget.aSGadFlashBang),
        G("stickycam", "Sticky Camera", SGadget.aSGadStickyCam, description: "Fire an equipped sticky camera with the Shocker drawn."),
        G("spybullet", "Spy Bullet", SGadget.aSGadSpyBulletLauncher),
        G("alarmsnare", "Alarm Snare", SGadget.aSGadNoiseEmitter),
        G("heartbeat", "Heartbeat Sensor", SGadget.BindHeartbeatSensor, description: "Select the equipped sensor. It scans with the Shocker drawn."),
        G("clone", "Clone", SGadget.BindClone, description: "Send your equipped Clone running forward while grounded."),
        G("minehack", "Mine Hack", SGadget.BindMineHack, description: "Aim at a nearby mine and hold to hack it with Mine Hack equipped."),
        G("frag", "Frag Grenade", SGadget.aSGadFragGrenade, merc: true),
        G("flare", "Flare", SGadget.aSGadFlareLauncher, merc: true),
        G("mines", "Mines", SGadget.aSGadMultiMinesLauncher, merc: true),
        G("presencedetector", "Presence Detector", SGadget.aSGadMultiPresDetectorLauncher, merc: true),
        G("taser", "Taser", SGadget.aSGadTazerLauncher, merc: true),
        G("cameranetwork", "Camera Network", SGadget.aSGadCamNetwork, merc: true),
        G("gasmask", "Gas Mask", SGadget.aSGadGasMask, merc: true),
        G("backpack", "Backpack", SGadget.aSGadBackPack, merc: true),
        A("next", "Gadgets", "Next gadget", "Select the next gadget.", "NextGadget", "NextGadget", "BT_NextItem"),
        A("previous", "Gadgets", "Previous gadget", "Select the previous gadget.", "PreviousGadget", "PreviousGadget", "BT_PrevItem"),
        A("laser", "Gadgets", "Toggle laser gadget", "Switch to the laser sight and toggle it. The laser sight and flashlight cannot be active together.", "GadLaserSwitch", "GadLaserSwitch") with { Spy = false },
        A("torch", "Gadgets", "Toggle flashlight gadget", "Switch to the flashlight and toggle it. The flashlight and laser sight cannot be active together.", "GadTorcheSwitch", "GadTorcheSwitch") with { Spy = false },
        A("weaponlaser", "Gadgets", "Toggle weapon laser", "Toggle the spy weapon laser in weapon states that support it.", "SwitchLaser", "SwitchLaser") with { Merc = false },
        A("sticky", "Gadgets", "Previous sticky camera", "Switch to the previous available sticky camera.", "PrevStickyCam", "PrevStickyCam") with { Merc = false },
        A("vision1", "Vision", "First vision mode", "Toggle the role's first vision mode, when allowed by match settings.", "SwitchHeadset 1", "SwitchHeadset 1"),
        A("vision2", "Vision", "Second vision mode", "Toggle the role's second vision mode, when allowed by match settings.", "SwitchHeadset 2", "SwitchHeadset 2"),
        A("visioncycle", "Vision", "Cycle vision modes", "Cycle the available vision modes.", "SwitchHeadset 3", "SwitchHeadset 3"),
        A("scope", "Vision", "Binoculars / sniper scope", "Toggle the role's zoom view. A spy with the weapon drawn first requests holstering it.", "SwitchBinocularVision", "SwitchBinocularVision"),
        A("perspective", "Vision", "First / third person", "Use first person for tight spaces. Toggle switches perspective on each press; Hold uses first person until released. Wheel bindings always toggle. The selected perspective behavior also applies to controller bindings.", "SwitchSpyPerspective", "SwitchSpyPerspective") with { Merc = false },
        A("firstperson", "Vision", "First person", "Select first person on press, including each scroll notch. Repeated presses keep first person selected. Starts unbound.", "SetSpyFirstPerson 1", "SetSpyFirstPerson 1") with { Merc = false },
        A("thirdperson", "Vision", "Third person", "Select third person on press, including each scroll notch. Repeated presses keep third person selected. Starts unbound.", "SetSpyFirstPerson 0", "SetSpyFirstPerson 0") with { Merc = false },
        A("zoomin", "Vision", "Zoom in", "Increase zoom in views that support it.", "Button bZoomIn | Function ZoomIn", "ZoomIn", "bZoomIn"),
        A("zoomout", "Vision", "Zoom out", "Decrease zoom in views that support it.", "Button bZoomOut | Function ZoomOut", "ZoomOut", "bZoomOut"),
        A("er", "Vision", "Toggle Enhanced Reality", "Toggle the Enhanced Reality display.", "SwitchEnhancedReality", "SwitchEnhancedReality"),
        A("wheelup", "Gadgets", "Mouse wheel up action", "Uses the shared mouse-wheel preference: gadget selection or movement speed.", "WheelUp", "WheelUp") with { Available = false },
        A("wheeldown", "Gadgets", "Mouse wheel down action", "Uses the shared mouse-wheel preference: gadget selection or movement speed.", "WheelDown", "WheelDown") with { Available = false },
        A("hackcomms", "Communication", "Hack enemy communications", "Request communications hacking when available.", "Button bSpecialsChat", "bSpecialsChat"),
        A(AutoHackId, "Communication", "Toggle auto-hack", "Switch auto-hack on or off. While it is on, enemy communications are hacked whenever they can be, without holding the hack key. Double tap the hack key by default.", ControlActions.AutoHackCommand, ControlActions.AutoHackCommand),
        A("chat", "Communication", "Chat", "Open public text chat.", "Talk", "Talk"),
        A("teamchat", "Communication", "Team chat", "Open team text chat.", "TeamTalk", "TeamTalk"),
        A("quickchat", "Communication", "Quick chat", "Open team quick chat, choose a category, then a phrase with 1-4 or the D-pad. Escape / B goes back. Starts unbound.", "QuickChat", "QuickChat"),
        A(PingId, "Communication", "Team ping", "Tap to mark the point you are looking at for your team. Hold to open the wheel, select with the mouse or right stick, and release to send.", PingCommand, PingCommand),
        A("voice", "Communication", "Push to talk", "Hold to transmit on the all channel; also forces transmission in voice activation mode.", "PushToTalk", "PushToTalk"),
        A("teamvoice", "Communication", "Team push to talk", "Hold to transmit to your team only.", "TeamPushToTalk", "TeamPushToTalk"),
        A("mute", "Communication", "Toggle microphone mute", "Mute or unmute your microphone.", "ToggleVoiceMute", "ToggleVoiceMute"),
        A("controls", "Interface", "Controls reference (unavailable)", "This legacy shortcut has no active handler in the current build.", "DisplayDefaultControls", "DisplayDefaultControls") with { Available = false },
        A("hudhelp", "Interface", "HUD help (unavailable)", "This legacy shortcut has no active handler in the current build.", "DisplayHud", "DisplayHud") with { Available = false },
        A("map", "Interface", "Show strategic map", "Open the strategic map.", "DisplayStrategicMap", "DisplayStrategicMap"),
        A("session", "Interface", "Show session status", "Show session status.", "DisplaySessionStatus", "DisplaySessionStatus"),
        A("players", "Interface", "Toggle scoreboard", "Open or close the round scoreboard without stopping gameplay. Combat stats are delayed 30 seconds.", "ShowConnectedPlayers", "ShowConnectedPlayers"),
        A("scoreboard_hold", "Interface", "Hold scoreboard", "Show the round scoreboard while held. Starts unbound; Tab remains assigned to vision mode. Use a key, mouse button or controller button, not a mouse-wheel direction.", "HoldScoreboard", "HoldScoreboard"),
        A("teammatenames", "Interface", "Toggle teammate names", "Toggle teammate names through walls for your current role during play.", "ToggleTeammateNames", "ToggleTeammateNames"),
        A("gear", "Interface", "Change gear", "Open the gear picker where gear changes are allowed.", "ChangeGear", "ChangeGear"),
        A("hidehud", "Interface", "Toggle HUD visibility", "Toggle HUD visibility when cinematic controls are allowed by the host.", "HideHud", "HideHud"),
        A("freecam", "Interface", "Toggle cinematic free camera", "Toggle cinematic free camera when allowed by the host.", "FreeCam", "FreeCam"),
        A("menu", "Interface", "Open game menu", "Open the in-game menu.", "ShowMenu", "ShowMenu"),
        A("quit", "Interface", "Quit shortcut (unavailable)", "This legacy shortcut has no active handler in the current build. Use the game menu to quit.", "Quit", "Quit") with { Available = false },
    ];

    private static readonly Dictionary<string, ControlActionInfo> s_byId = All.ToDictionary(a => a.Id, StringComparer.Ordinal);

    public static ControlActionInfo? Find(string id) => s_byId.GetValueOrDefault(id);

    /// <summary>
    /// The actions one side's editor lists, with that side's names for the shared ones (the spy's night and thermal
    /// vision are the merc's motion tracking and EMF), in the categories' order: movement, combat, gadgets, vision,
    /// communication, interface (ControlActions.ForRole).
    /// </summary>
    public static IEnumerable<ControlActionInfo> ForRole(bool merc) => All.Where(a => a.Available && (merc ? a.Merc : a.Spy)).Select(a => a.Id switch
    {
        "vision1" => a with { Label = merc ? "Motion tracking vision" : "Night vision" },
        "vision2" => a with { Label = merc ? "EMF vision" : "Thermal vision" },
        "scope" => a with
        {
            Label = merc ? "Sniper scope" : "Binoculars",
            Description = merc ? "Aim through the sniper scope using the selected Hold or Toggle behavior. Wheel assignments always toggle." : "Toggle binoculars. With the weapon drawn, this first requests holstering it.",
        },
        _ => a,
    }).OrderBy(a => a.Category switch
    {
        "Movement" => 0, "Combat" => 1, "Gadgets" => 2, "Vision" => 3, "Communication" => 4, _ => 5,
    });

    /// <summary>
    /// What a part of a binding is called for matching: its name without Button, InvButton, Function or Axis, with the
    /// number kept for the commands whose number picks something (SelectGadget 1, SwitchHeadset 2).
    /// </summary>
    public static string Token(string raw)
    {
        ArgumentNullException.ThrowIfNull(raw);
        string[] words = raw.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (words.Length == 0) return "";
        int start = words[0].ToUpperInvariant() is "BUTTON" or "INVBUTTON" or "FUNCTION" or "AXIS" ? 1 : 0;
        if (start >= words.Length) return raw;
        string name = words[start];
        return name.Equals("SelectGadget", StringComparison.OrdinalIgnoreCase) || name.Equals("GadgetQuickChoice", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("LaunchGadget", StringComparison.OrdinalIgnoreCase) || name.Equals("UseGadget", StringComparison.OrdinalIgnoreCase) ||
            name.Equals("SwitchHeadset", StringComparison.OrdinalIgnoreCase) || name.Equals("SetSpyFirstPerson", StringComparison.OrdinalIgnoreCase)
            ? name + " " + (words.Length > start + 1 ? words[start + 1] : "0")
            : name;
    }

    /// <summary>
    /// Double taps run through the play input. Interface, chat and Enhanced Reality shortcuts are read from the plain
    /// key by their own screens, so they do not offer one (OptionsMenu.SupportsDoubleTap).
    /// </summary>
    public static bool SupportsDoubleTap(ControlActionInfo action) =>
        action.Category != "Interface" && action.Id is not ("chat" or "teamchat" or "quickchat" or "er");

    /// <summary>The actions that only a keyboard key can start (their screens read the key itself).</summary>
    public static bool NeedsKeyboardKey(ControlActionInfo action) => action.Category == "Interface" || action.Id is "chat" or "teamchat" or "er";

    private static ControlActionInfo A(string id, string category, string label, string description, string command, params string[] tokens) =>
        new(id, category, label, description, command, tokens);

    private static ControlActionInfo G(string id, string label, int gadget, bool merc = false, string? description = null)
    {
        string command = "UseGadget " + gadget.ToString(CultureInfo.InvariantCulture);
        return new ControlActionInfo("gadget_" + id, "Gadgets", label,
            description ?? ("Select and use the equipped " + label + "."), command, [command], Spy: !merc, Merc: merc);
    }
}
