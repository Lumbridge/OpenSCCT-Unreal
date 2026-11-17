using System;
using System.Collections.Generic;
using System.Linq;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// The bindings menu's view of a side: which keys or pad controls do an action, what a key does now, and binding an
/// action to a new key in place of the old ones. The menu table's GetActionKeys, GetKeyActions and BindAction.
/// </summary>
public static class ActionBindings
{
    /// <summary>
    /// The default controller's contextual bindings (PadState, PadMapper) and the actions each one stands for in the
    /// menu: the spy's right trigger fires, the left trigger uses the gadget, B crouches, D-pad down is back to wall
    /// or the pistol's laser. Without them those defaults read as unbound and a new button only added to them.
    /// </summary>
    private static readonly (string Binding, string[] Actions)[] s_padContext =
    [
        (ControlBindings.SpyFireAction, ["fire"]),
        (ControlBindings.SpyGadgetAction, ["altfire"]),
        (ControlBindings.MercGadgetAction, ["altfire"]),
        (ControlBindings.CrouchAction, ["crouch"]),
        (ControlBindings.LongRollAction, ["longroll"]),
        (ControlBindings.WallLaserAction, ["wall", "weaponlaser"]),
    ];

    public static ControlAction? Find(string id) =>
        ControlActions.All.FirstOrDefault(a => string.Equals(a.Id, id, StringComparison.OrdinalIgnoreCase));

    /// <summary>
    /// The keys (or, with <paramref name="pad"/>, the pad controls) of a side that do an action. A key counts when
    /// its binding holds every part of the action's command; a pad control when it is bound to the action or to a
    /// contextual binding that does it.
    /// </summary>
    public static List<string> KeysOf(SideBindings side, bool pad, ControlAction action)
    {
        ArgumentNullException.ThrowIfNull(side);
        ArgumentNullException.ThrowIfNull(action);
        var keys = new List<string>();
        if (pad)
        {
            foreach (string control in ControlBindings.PadControls)
            {
                if (side.Pad.TryGetValue(control, out string? bound) && PadDoes(bound, action.Id))
                {
                    keys.Add(control);
                }
            }

            return keys;
        }

        string[] parts = Parts(action.Command);
        foreach (KeyValuePair<string, string> binding in side.Keys.Bindings)
        {
            string[] held = Parts(binding.Value);
            if (parts.Length > 0 && parts.All(p => held.Contains(p, StringComparer.OrdinalIgnoreCase)))
            {
                keys.Add(binding.Key);
            }
        }

        return keys;
    }

    /// <summary>The labels of the side's actions a key or pad control does now: what binding it elsewhere takes away.</summary>
    public static List<string> LabelsOf(SideBindings side, bool merc, bool pad, string key)
    {
        string wanted = Canonical(key, pad);
        var labels = new List<string>();
        foreach (ControlAction candidate in ControlActions.All)
        {
            if ((merc ? candidate.Merc : candidate.Spy) && KeysOf(side, pad, candidate).Contains(wanted, StringComparer.OrdinalIgnoreCase))
            {
                labels.Add(candidate.Label);
            }
        }

        return labels;
    }

    /// <summary>
    /// Binds an action of one side to <paramref name="key"/> in place of what did it before: unless
    /// <paramref name="keepOthers"/>, the keys that did it stop doing it (their other actions stay), then
    /// <paramref name="key"/> does it and nothing else. A null or empty key only clears. With <paramref name="pad"/>
    /// the same for pad controls. False for a name that is not a key or a pad control.
    /// </summary>
    public static bool Bind(SideBindings side, bool pad, ControlAction action, string? key, bool keepOthers)
    {
        ArgumentNullException.ThrowIfNull(side);
        ArgumentNullException.ThrowIfNull(action);
        string name = key?.Trim() ?? "";
        if (pad)
        {
            if (name.Length > 0 && Array.IndexOf(ControlBindings.PadControls, name) < 0)
            {
                return false;
            }

            if (!keepOthers)
            {
                foreach (string control in KeysOf(side, true, action))
                {
                    side.SetPad(control, PadWithout(side.Pad[control], action.Id));
                }
            }

            if (name.Length > 0)
            {
                side.SetPad(name, ControlActions.PadPrefix + action.Id);
            }

            return true;
        }

        if (name.Length > 0 && !BindingKey.TryParse(name, out _))
        {
            return false;
        }

        string[] parts = Parts(action.Command);
        if (!keepOthers)
        {
            foreach (string held in KeysOf(side, false, action))
            {
                string rest = string.Join(" | ", Parts(side.Keys.Bindings[held]).Where(p => !parts.Contains(p, StringComparer.OrdinalIgnoreCase)));
                side.Keys.Bind(held, rest);
            }
        }

        return name.Length == 0 || side.Keys.Bind(name, action.Command);
    }

    public static string Canonical(string key, bool pad) =>
        !pad && BindingKey.TryParse(key, out BindingKey parsed) ? parsed.Canonical : key;

    private static bool PadDoes(string bound, string id)
    {
        if (string.Equals(bound, ControlActions.PadPrefix + id, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        foreach ((string binding, string[] actions) in s_padContext)
        {
            if (string.Equals(bound, binding, StringComparison.Ordinal) && actions.Contains(id, StringComparer.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>What a pad control does once it no longer does <paramref name="id"/>: a contextual binding of two actions keeps the other.</summary>
    private static string PadWithout(string bound, string id)
    {
        foreach ((string binding, string[] actions) in s_padContext)
        {
            if (string.Equals(bound, binding, StringComparison.Ordinal))
            {
                string[] rest = actions.Where(a => !string.Equals(a, id, StringComparison.OrdinalIgnoreCase)).ToArray();
                return rest.Length == 1 ? ControlActions.PadPrefix + rest[0] : "";
            }
        }

        return "";
    }

    private static string[] Parts(string command) =>
        command.Split('|', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
}
