using System;
using System.Collections.Generic;
using System.Linq;

namespace OpenScct.UnrealBridge.Controls;

internal static class MercBindings
{
    public const string AimId = "mercaim", GadgetId = "mercgadget";
    public const string AimButton = "bMercAim", GadgetButton = "bMercGadget";
    public const string AimCommand = "Button " + AimButton, GadgetCommand = "Button " + GadgetButton;
    public const string AimHeld = "bMercAimHeld";
    public const string Trigger = "Pad:merctrigger";
    public const string StockRight = "Button bAltFire | Function AltFire | GadgetQuickChoice 1 | Button bHoldBreath";
    public const string PadGadget = ControlActions.PadPrefix + GadgetId;

    // Version 2 profiles stored these commands in keyboard chains, pad slots and key ordering.
    private static readonly Dictionary<string, string> LegacyNames = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Button bBodycamFreeLook"] = ControlActionList.MercFreeLookCommand,
        ["Button bBodycamAim"] = AimCommand,
        ["Button bBodycamGadget"] = GadgetCommand,
        ["bodycamfreelook"] = ControlActionList.MercFreeLookId,
        ["bodycamaim"] = AimId,
        ["bodycamgadget"] = GadgetId,
        ["Action:bodycamfreelook"] = ControlActions.PadPrefix + ControlActionList.MercFreeLookId,
        ["Action:bodycamaim"] = ControlActions.PadPrefix + AimId,
        ["Action:bodycamgadget"] = PadGadget,
        ["Pad:mercbodycamtrigger"] = Trigger,
    };

    public static string UpgradeName(string value) => LegacyNames.GetValueOrDefault(value, value);

    public static string UpgradeCommand(string? value)
    {
        if (string.IsNullOrEmpty(value)) return "";
        string[] parts = value.Split('|');
        bool renamed = false;
        for (int i = 0; i < parts.Length; i++)
        {
            if (LegacyNames.TryGetValue(parts[i].Trim(), out string? current))
            {
                parts[i] = current;
                renamed = true;
            }
        }
        return renamed ? string.Join(" | ", parts.Select(part => part.Trim())) : value;
    }

    // Install once on older profiles, and on reset. An explicitly edited RMB keeps its command.
    // The spy ignores mercenary-specific commands on a shared keyboard layout.
    public static void UpgradeKeys(KeyLayout layout)
    {
        var keys = layout.Bindings;
        bool gadget = false;
        if (!keys.TryGetValue("V", out string? v) || v == "PrevStickyCam")
        {
            layout.Bind("V", v is null ? GadgetCommand : v + " | " + GadgetCommand);
            gadget = true;
        }
        if (!keys.ContainsKey("MouseX1"))
        {
            layout.Bind("MouseX1", GadgetCommand);
            gadget = true;
        }
        if (!gadget && !keys.ContainsKey("M"))
        {
            layout.Bind("M", GadgetCommand);
            gadget = true;
        }
        if (gadget && keys.GetValueOrDefault("RightMouse") == StockRight)
            layout.Bind("RightMouse", StockRight + " | " + AimCommand);
        else if (!keys.ContainsKey("MouseX2"))
            layout.Bind("MouseX2", AimCommand);
    }

    public static void UpgradePad(Dictionary<string, string> pad)
    {
        if (pad.GetValueOrDefault("LT") != ControlBindings.MercGadgetAction) return;
        string? spare = new[] { "LB+RB", "P1" }.FirstOrDefault(k => string.IsNullOrEmpty(pad.GetValueOrDefault(k)));
        if (spare is null) return;
        pad["LT"] = Trigger;
        pad[spare] = PadGadget;
    }

    public static IReadOnlyList<KeyAction> Resolve(IReadOnlyList<KeyAction> actions)
    {
        bool aim = false, gadget = false;
        for (int i = 0; i < actions.Count; i++)
        {
            aim |= actions[i].Name == AimButton;
            gadget |= actions[i].Name == GadgetButton;
        }
        if (!aim && !gadget) return actions;

        var active = new List<KeyAction>(actions.Count + 1);
        foreach (KeyAction action in actions)
        {
            if (action.Name == AimButton) active.Add(new KeyAction(KeyActionKind.Button, AimHeld));
            else if (action.Name == GadgetButton)
            {
                active.Add(new KeyAction(KeyActionKind.Button, "bAltFire"));
                active.Add(new KeyAction(KeyActionKind.Command, "AltFire"));
            }
            else if (!aim || action.Name is not ("bAltFire" or "AltFire" or "GadgetQuickChoice" or "bHoldBreath"))
            {
                active.Add(action);
            }
        }
        return active;
    }
}
