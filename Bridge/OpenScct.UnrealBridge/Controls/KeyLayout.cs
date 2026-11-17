using System;
using System.Collections.Generic;
using System.Globalization;

namespace OpenScct.UnrealBridge.Controls;

// Ported from OpenSCCT game/Scripts/Input/KeyLayout.cs and BindingKey.cs (both Godot-free there). The ini file
// format and the alias table are left out: the bridge keeps a layout as a plain key-to-command table (JSON, see
// ControlBindings), with the commands written out as the original's layouts expand them.

/// <summary>What one part of a binding does: hold an input byte, call an exec command on the press, or feed an axis.</summary>
public enum KeyActionKind
{
    /// <summary><c>Button bX</c> or <c>InvButton bX</c>: the byte is 1 while the key is down.</summary>
    Button,

    /// <summary>An exec command run once on the press (<c>Function Fire</c>, <c>SelectGadget 1</c>, <c>Jump</c>).</summary>
    Command,

    /// <summary><c>Axis aX Speed=n</c>: the axis receives n while the key is down.</summary>
    Axis,
}

/// <summary>One resolved part of a key's binding.</summary>
public readonly record struct KeyAction(KeyActionKind Kind, string Name, int Arg = 0, float Speed = 0f);

/// <summary>
/// The key layout of one side: binding keys (the original's key names without <c>IK_</c>, optionally with held
/// modifiers or a double tap: <c>W</c>, <c>Ctrl+F</c>, <c>Double+H</c>) to commands in the original's binding
/// grammar, parts separated by <c>|</c> and run left to right as UE2 runs them
/// (<c>Button bFire | Function Fire | GadgetQuickChoice 0</c>).
/// </summary>
public sealed class KeyLayout
{
    private readonly SortedDictionary<string, string> _bindings = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, List<KeyAction>> _resolved = new(StringComparer.OrdinalIgnoreCase);
    private static readonly List<KeyAction> s_none = new();

    public KeyLayout()
    {
    }

    public KeyLayout(IEnumerable<KeyValuePair<string, string>> bindings)
    {
        ArgumentNullException.ThrowIfNull(bindings);
        foreach ((string key, string command) in bindings)
        {
            Bind(key, command);
        }
    }

    /// <summary>Binding keys to their raw commands, sorted by key.</summary>
    public IReadOnlyDictionary<string, string> Bindings => _bindings;

    /// <summary>Counts up on every change, so a reader can tell its copy is stale.</summary>
    public int Revision { get; private set; }

    /// <summary>Sets (or with an empty command, clears) a key's raw binding. False for a key name that cannot be one.</summary>
    public bool Bind(string key, string? command)
    {
        if (!BindingKey.TryParse(key, out BindingKey binding))
        {
            return false;
        }

        key = binding.Canonical;
        if (string.IsNullOrWhiteSpace(command))
        {
            _bindings.Remove(key);
        }
        else
        {
            _bindings[key] = command.Trim();
        }

        _resolved.Clear();
        Revision++;
        return true;
    }

    /// <summary>
    /// The order the bindings editor shows an action's keys in (primary first), by action id. Only the editor reads
    /// it; play does not. A key that is no longer the action's is skipped (KeyLayout.InputOrder in the Godot client).
    /// </summary>
    public IReadOnlyDictionary<string, string[]> InputOrders => _inputOrder;

    private readonly SortedDictionary<string, string[]> _inputOrder = new(StringComparer.Ordinal);

    /// <summary><paramref name="inputs"/> in the order kept for <paramref name="action"/>, then the rest as given.</summary>
    public IReadOnlyList<string> InputOrder(string action, IEnumerable<string> inputs)
    {
        ArgumentNullException.ThrowIfNull(inputs);
        var available = new List<string>();
        foreach (string key in inputs)
        {
            if (!available.Exists(k => k.Equals(key, StringComparison.OrdinalIgnoreCase)))
            {
                available.Add(key);
            }
        }

        var ordered = new List<string>();
        if (_inputOrder.TryGetValue(action, out string[]? saved))
        {
            foreach (string key in saved)
            {
                int index = available.FindIndex(k => k.Equals(key, StringComparison.OrdinalIgnoreCase));
                if (index >= 0)
                {
                    ordered.Add(available[index]);
                    available.RemoveAt(index);
                }
            }
        }

        ordered.AddRange(available);
        return ordered;
    }

    public void SetInputOrder(string action, IEnumerable<string> inputs)
    {
        ArgumentNullException.ThrowIfNull(inputs);
        var order = new List<string>();
        foreach (string key in inputs)
        {
            if (!string.IsNullOrWhiteSpace(key) && !order.Exists(k => k.Equals(key, StringComparison.OrdinalIgnoreCase)))
            {
                order.Add(key);
            }
        }

        if (order.Count == 0)
        {
            _inputOrder.Remove(action);
        }
        else
        {
            _inputOrder[action] = order.ToArray();
        }
    }

    public KeyLayout Clone()
    {
        var copy = new KeyLayout(_bindings);
        foreach ((string action, string[] order) in _inputOrder)
        {
            copy._inputOrder[action] = (string[])order.Clone();
        }

        return copy;
    }

    /// <summary>The same bindings (the text a save would write).</summary>
    public bool SameAs(KeyLayout other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (_bindings.Count != other._bindings.Count)
        {
            return false;
        }

        foreach ((string key, string command) in _bindings)
        {
            if (!other._bindings.TryGetValue(key, out string? theirs) || !string.Equals(command, theirs, StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>
    /// The original's wheel commands as the player's wheel setting plays them (KeyLayout.UpgradeWheelBindings):
    /// WheelUp and WheelDown become NextGadget and PreviousGadget ("Gadget") or Run and Walk ("Speed"), and
    /// ChangeSpeed becomes Walk. The view states that take the wheel themselves still get it (InputMapper.FillTick).
    /// </summary>
    public void UpgradeWheelBindings(string wheelMode)
    {
        foreach (KeyValuePair<string, string> binding in new List<KeyValuePair<string, string>>(_bindings))
        {
            string[] parts = binding.Value.Split('|', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
            bool changed = false;
            for (int i = 0; i < parts.Length; i++)
            {
                string replacement = parts[i].ToUpperInvariant() switch
                {
                    "WHEELUP" => wheelMode == "Speed" ? "Run" : "NextGadget",
                    "WHEELDOWN" => wheelMode == "Speed" ? "Walk" : "PreviousGadget",
                    "CHANGESPEED" => "Walk",
                    _ => parts[i],
                };
                changed |= replacement != parts[i];
                parts[i] = replacement;
            }

            if (changed)
            {
                Bind(binding.Key, string.Join(" | ", parts));
            }
        }
    }

    /// <summary>The resolved parts of <paramref name="key"/>'s binding, empty when unbound. The list is shared: do not change it.</summary>
    public IReadOnlyList<KeyAction> Actions(string key)
    {
        if (_resolved.TryGetValue(key, out List<KeyAction>? actions))
        {
            return actions;
        }

        if (!_bindings.TryGetValue(key, out string? binding))
        {
            return s_none;
        }

        actions = Resolve(binding);
        _resolved[key] = actions;
        return actions;
    }

    /// <summary>The parts of a raw command.</summary>
    public static List<KeyAction> Resolve(string command)
    {
        ArgumentNullException.ThrowIfNull(command);
        var into = new List<KeyAction>();
        foreach (string part in command.Split('|'))
        {
            string p = part.Trim();
            if (p.Length == 0)
            {
                continue;
            }

            string[] words = p.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            string head = words[0];
            if ((string.Equals(head, "Button", StringComparison.OrdinalIgnoreCase) ||
                 string.Equals(head, "InvButton", StringComparison.OrdinalIgnoreCase)) && words.Length > 1)
            {
                into.Add(new KeyAction(KeyActionKind.Button, words[1]));
            }
            else if (string.Equals(head, "Axis", StringComparison.OrdinalIgnoreCase) && words.Length > 1)
            {
                float speed = 1f;
                foreach (string w in words[1..])
                {
                    if (w.StartsWith("Speed=", StringComparison.OrdinalIgnoreCase) &&
                        float.TryParse(w.AsSpan(6), NumberStyles.Float, CultureInfo.InvariantCulture, out float s))
                    {
                        speed = s;
                    }
                }

                into.Add(new KeyAction(KeyActionKind.Axis, words[1], 0, speed));
            }
            else if (string.Equals(head, "Function", StringComparison.OrdinalIgnoreCase) && words.Length > 1)
            {
                into.Add(new KeyAction(KeyActionKind.Command, words[1], Arg(words, 2)));
            }
            else
            {
                into.Add(new KeyAction(KeyActionKind.Command, head, Arg(words, 1)));
            }
        }

        return into;
    }

    private static int Arg(string[] words, int index) =>
        words.Length > index && int.TryParse(words[index], NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) ? n : 0;
}

[Flags]
public enum BindingModifiers
{
    None = 0,
    Ctrl = 1,
    Shift = 2,
    Alt = 4,

    /// <summary>
    /// Not a held key: the input pressed twice in quick succession (<c>Double+H</c>). It fires on the second press,
    /// alongside the input's ordinary binding, and stays held until that press is released.
    /// </summary>
    Double = 8,
}

/// <summary>A physical input and its optional modifiers (<c>Ctrl+Shift+F</c>, <c>Double+H</c>).</summary>
public readonly record struct BindingKey(string Base, BindingModifiers Modifiers = BindingModifiers.None)
{
    public const string DoublePrefix = "Double";

    /// <summary>The wheel: a press with no release.</summary>
    public bool IsPulse => Base.StartsWith("MouseWheel", StringComparison.OrdinalIgnoreCase);

    public string Canonical
    {
        get
        {
            if (Modifiers == BindingModifiers.None)
            {
                return Base;
            }

            string text = Base;
            if (Modifiers.HasFlag(BindingModifiers.Alt)) text = "Alt+" + text;
            if (Modifiers.HasFlag(BindingModifiers.Shift)) text = "Shift+" + text;
            if (Modifiers.HasFlag(BindingModifiers.Ctrl)) text = "Ctrl+" + text;
            return Modifiers.HasFlag(BindingModifiers.Double) ? DoublePrefix + "+" + text : text;
        }
    }

    public static BindingModifiers ModifierOf(string key) =>
        key.Equals("Ctrl", StringComparison.OrdinalIgnoreCase) ? BindingModifiers.Ctrl
        : key.Equals("Shift", StringComparison.OrdinalIgnoreCase) ? BindingModifiers.Shift
        : key.Equals("Alt", StringComparison.OrdinalIgnoreCase) ? BindingModifiers.Alt
        : BindingModifiers.None;

    public static bool IsPulseName(string key) => TryParse(key, out BindingKey parsed) && parsed.IsPulse;

    public static bool TryParse(string? text, out BindingKey key)
    {
        key = default;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        if (text.IndexOf('+', StringComparison.Ordinal) < 0)
        {
            // The usual case, without the split.
            if (text.IndexOfAny([' ', '\r', '\n', '=']) >= 0)
            {
                return false;
            }

            key = new BindingKey(ModifierOf(text) is var plain && plain != BindingModifiers.None ? plain.ToString() : text);
            return true;
        }

        string[] parts = text.Split('+', StringSplitOptions.TrimEntries);
        string input = parts[^1];
        if (input.Length == 0 || input.IndexOfAny([' ', '\r', '\n', '=']) >= 0)
        {
            return false;
        }

        BindingModifiers modifiers = BindingModifiers.None;
        for (int i = 0; i < parts.Length - 1; i++)
        {
            BindingModifiers modifier = parts[i].Equals(DoublePrefix, StringComparison.OrdinalIgnoreCase)
                ? BindingModifiers.Double
                : ModifierOf(parts[i]);
            if (modifier == BindingModifiers.None || (modifiers & modifier) != 0)
            {
                return false;
            }

            modifiers |= modifier;
        }

        // A modifier key cannot be combined with held modifiers, but it can be double tapped (Double+Shift).
        BindingModifiers own = ModifierOf(input);
        if ((modifiers & ~BindingModifiers.Double) != BindingModifiers.None && own != BindingModifiers.None)
        {
            return false;
        }

        if (own != BindingModifiers.None)
        {
            input = own.ToString();
        }

        key = new BindingKey(input, modifiers);
        return true;
    }

    /// <summary>Only an exact modifier match overrides the ordinary input; extra modifiers fall back to plain.</summary>
    public static string? Select(KeyLayout layout, string input, BindingModifiers modifiers)
    {
        ArgumentNullException.ThrowIfNull(layout);
        if (ModifierOf(input) != BindingModifiers.None)
        {
            modifiers = BindingModifiers.None;
        }

        if (modifiers != BindingModifiers.None)
        {
            string candidate = new BindingKey(input, modifiers).Canonical;
            if (layout.Bindings.ContainsKey(candidate))
            {
                return candidate;
            }
        }

        return layout.Bindings.ContainsKey(input) ? input : null;
    }

    /// <summary>The double-tap binding of an input: the exact held modifiers first, then the plain double tap.</summary>
    public static string? SelectDouble(KeyLayout layout, string input, BindingModifiers modifiers)
    {
        ArgumentNullException.ThrowIfNull(layout);
        if (ModifierOf(input) != BindingModifiers.None)
        {
            modifiers = BindingModifiers.None;
        }

        string candidate = new BindingKey(input, modifiers | BindingModifiers.Double).Canonical;
        if (layout.Bindings.ContainsKey(candidate))
        {
            return candidate;
        }

        candidate = new BindingKey(input, BindingModifiers.Double).Canonical;
        return layout.Bindings.ContainsKey(candidate) ? candidate : null;
    }
}
