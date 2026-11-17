using System;
using System.Collections.Generic;

namespace OpenScct.UnrealBridge.Controls;

// Ported from OpenSCCT game/Scripts/Options/PadBindingState.cs and PadActionState.cs (both Godot-free there).

/// <summary>A logical controller binding changed state; a tap emits its press and release together.</summary>
public readonly record struct PadBindingEvent(string Control, string Action, bool Pressed);

/// <summary>
/// Resolves controller buttons, modifier combinations (LS+Y) and held actions (B:Hold). Call Reset when changing
/// the bindings in place, the side or the input mode. Returned event lists are valid until the next call; callers
/// deliver releases before applying the new presses.
/// </summary>
public sealed class PadBindingState
{
    /// <summary>Reloaded 4.0d's held-input threshold: tick difference compared with 0xc8 ms at 0x100b5bd1.</summary>
    public const double HoldSeconds = 0.2;

    private readonly record struct Binding(string Control, string Action);

    private readonly record struct Chord(string Control, string Action, string Modifier, string Button);

    private sealed class Press
    {
        public double Seconds;
        public bool Consumed;
        public bool LongTriggered;
    }

    private readonly Dictionary<string, Press> _presses = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _active = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _desired = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Chord> _activeChords = new(StringComparer.Ordinal);
    private readonly HashSet<string> _blocked = new(StringComparer.Ordinal);
    private readonly HashSet<string> _modifiers = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Binding> _holds = new(StringComparer.Ordinal);
    private readonly List<Binding> _buttons = new();
    private readonly List<Chord> _chords = new();
    private readonly List<string> _remove = new();
    private readonly List<Binding> _taps = new();
    private readonly List<PadBindingEvent> _events = new();
    private readonly Stack<Press> _spare = new();
    private IReadOnlyDictionary<string, string>? _layout;

    public IReadOnlyList<PadBindingEvent> Update(
        IReadOnlyDictionary<string, string> bindings, HashSet<string> logicalDown, double delta)
    {
        ArgumentNullException.ThrowIfNull(bindings);
        ArgumentNullException.ThrowIfNull(logicalDown);
        ReadLayout(bindings);
        _events.Clear();
        _desired.Clear();
        _taps.Clear();
        _remove.Clear();
        foreach (string control in _blocked)
        {
            if (!logicalDown.Contains(control)) _remove.Add(control);
        }

        foreach (string control in _remove) _blocked.Remove(control);
        double elapsed = double.IsFinite(delta) && delta > 0 ? delta : 0;

        foreach (Press press in _presses.Values) press.Seconds += elapsed;
        foreach (string control in logicalDown)
        {
            if (_blocked.Contains(control)) continue;
            if (!_presses.ContainsKey(control)) _presses.Add(control, NewPress());
        }

        // A combination owns its face button until that button is physically released. Releasing only the
        // modifier must not turn the still-held face button into Jump/Use or start another combination.
        _remove.Clear();
        foreach ((string button, Chord chord) in _activeChords)
        {
            if (Down(logicalDown, chord.Modifier) && Down(logicalDown, button)) _desired[chord.Control] = chord.Action;
            else _remove.Add(button);
        }

        foreach (string button in _remove) _activeChords.Remove(button);

        // The sorted order gives LS selection priority over LB launch if both are held on the same press.
        foreach (Chord chord in _chords)
        {
            if (!Down(logicalDown, chord.Modifier) || !Down(logicalDown, chord.Button)) continue;
            // A losing modifier is still being used as a modifier; do not perform its deferred standalone
            // action later merely because a higher-priority combination won this face-button press.
            _presses[chord.Modifier].Consumed = true;
            if (_activeChords.ContainsKey(chord.Button)) continue;
            Press button = _presses[chord.Button];
            if (button.Consumed) continue;
            button.Consumed = true;
            _activeChords.Add(chord.Button, chord);
            _desired[chord.Control] = chord.Action;
        }

        foreach (Binding binding in _buttons)
        {
            if (!_presses.TryGetValue(binding.Control, out Press? press)) continue;
            bool down = Down(logicalDown, binding.Control);
            bool hold = _holds.ContainsKey(binding.Control);
            bool deferredModifier = _modifiers.Contains(binding.Control) && DeferModifier(binding.Action);
            if (!down)
            {
                if (!press.Consumed && (hold ? press.Seconds < HoldSeconds : deferredModifier)) _taps.Add(binding);
                continue;
            }

            if (!hold && !deferredModifier && (!press.Consumed || IsClutch(binding.Action)))
            {
                _desired[binding.Control] = binding.Action;
            }
        }

        // A player can clear B's tap binding while keeping B:Hold bound.
        foreach ((string control, Binding held) in _holds)
        {
            if (!_presses.TryGetValue(control, out Press? press)) continue;
            if (!Down(logicalDown, control))
            {
                if (!press.Consumed && press.Seconds >= HoldSeconds) _taps.Add(held);
                continue;
            }

            if (!press.Consumed && press.Seconds >= HoldSeconds)
            {
                press.LongTriggered = true;
                press.Consumed = true;
            }

            if (press.LongTriggered) _desired[held.Control] = held.Action;
        }

        foreach ((string control, string action) in _active)
        {
            if (!_desired.TryGetValue(control, out string? desired) || desired != action)
                _events.Add(new PadBindingEvent(control, action, false));
        }

        foreach ((string control, string action) in _desired)
        {
            if (!_active.TryGetValue(control, out string? active) || active != action)
                _events.Add(new PadBindingEvent(control, action, true));
        }

        foreach (Binding tap in _taps)
        {
            _events.Add(new PadBindingEvent(tap.Control, tap.Action, true));
            _events.Add(new PadBindingEvent(tap.Control, tap.Action, false));
        }

        _active.Clear();
        foreach ((string control, string action) in _desired) _active.Add(control, action);
        _remove.Clear();
        foreach (string control in _presses.Keys)
        {
            if (!Down(logicalDown, control)) _remove.Add(control);
        }

        foreach (string control in _remove)
        {
            if (_presses.Remove(control, out Press? press)) _spare.Push(press);
        }

        return _events;
    }

    /// <summary>Returns every outstanding release and discards pending taps, combinations and held actions.</summary>
    public IReadOnlyList<PadBindingEvent> ReleaseAll()
    {
        _events.Clear();
        foreach ((string control, string action) in _active) _events.Add(new PadBindingEvent(control, action, false));
        ClearState();
        return _events;
    }

    /// <summary>
    /// Discards state after the caller releases its gameplay inputs. Controls supplied in held are ignored until
    /// physically released, so returning from a menu or refocusing cannot fire a button still held from before.
    /// </summary>
    public void Reset(HashSet<string>? held = null)
    {
        _events.Clear();
        ClearState();
        if (held is not null) _blocked.UnionWith(held);
    }

    private bool Down(HashSet<string> logicalDown, string control) => logicalDown.Contains(control) && !_blocked.Contains(control);

    private Press NewPress()
    {
        if (!_spare.TryPop(out Press? press))
        {
            return new Press();
        }

        press.Seconds = 0;
        press.Consumed = false;
        press.LongTriggered = false;
        return press;
    }

    private void ClearState()
    {
        _presses.Clear();
        _active.Clear();
        _desired.Clear();
        _activeChords.Clear();
        _blocked.Clear();
        _taps.Clear();
        _layout = null;
    }

    private void ReadLayout(IReadOnlyDictionary<string, string> bindings)
    {
        if (ReferenceEquals(bindings, _layout)) return;
        _layout = bindings;
        _buttons.Clear();
        _holds.Clear();
        _chords.Clear();
        _modifiers.Clear();
        foreach ((string control, string action) in bindings)
        {
            if (string.IsNullOrEmpty(action)) continue;
            if (control.EndsWith(":Hold", StringComparison.Ordinal))
            {
                _holds[control[..^5]] = new Binding(control, action);
            }
            else if (control.IndexOf('+') is int split && split > 0 && split < control.Length - 1)
            {
                string modifier = control[..split];
                string button = control[(split + 1)..];
                _modifiers.Add(modifier);
                _chords.Add(new Chord(control, action, modifier, button));
            }
            else
            {
                _buttons.Add(new Binding(control, action));
            }
        }

        _buttons.Sort((left, right) => StringComparer.Ordinal.Compare(left.Control, right.Control));
        _chords.Sort((left, right) =>
        {
            int priority = ModifierPriority(left.Modifier).CompareTo(ModifierPriority(right.Modifier));
            return priority != 0 ? priority : StringComparer.Ordinal.Compare(left.Control, right.Control);
        });
    }

    private static int ModifierPriority(string control) => control switch { "LS" => 0, "LB" => 1, _ => 2 };

    private static bool DeferModifier(string action) => action.Equals("Action:coop", StringComparison.OrdinalIgnoreCase)
        || action.Equals("PerformCoopMvt", StringComparison.OrdinalIgnoreCase);

    private static bool IsClutch(string action) => action.Equals(ControlBindings.ClutchOneAction, StringComparison.OrdinalIgnoreCase)
        || action.Equals(ControlBindings.ClutchTwoAction, StringComparison.OrdinalIgnoreCase);
}

/// <summary>The current gameplay facts needed by Reloaded's contextual controller actions.</summary>
public readonly record struct PadActionContext(
    bool InCamera = false,
    bool IsSpy = true,
    bool WeaponDrawn = false,
    bool Scoped = false,
    bool CameraTransition = false);

/// <summary>
/// Expands contextual pad bindings into independent actions. Trigger presses act immediately; their short
/// releases can quick-use gadgets. ReleaseAll aborts without synthesizing a tap. Call Update every frame, including
/// frames without button edges. Returned lists are reused on the next call.
/// </summary>
public sealed class PadActionState
{
    private readonly Dictionary<string, (string Action, double At)> _presses = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _held = new(StringComparer.Ordinal);
    private readonly List<PadBindingEvent> _events = new();
    private double _clock;
    private double _cameraGadgetAt = double.NegativeInfinity;

    public IReadOnlyList<PadBindingEvent> Update(IReadOnlyList<PadBindingEvent> events, double delta, PadActionContext context)
    {
        ArgumentNullException.ThrowIfNull(events);
        _events.Clear();
        _clock += double.IsFinite(delta) && delta > 0 ? delta : 0;

        for (int i = 0; i < events.Count; i++)
        {
            PadBindingEvent ev = events[i];
            if (!IsContextual(ev.Action))
            {
                Hold(ev.Control, ev.Action, ev.Pressed);
                continue;
            }

            if (ev.Pressed)
            {
                if (!_presses.TryAdd(ev.Control, (ev.Action, _clock))) continue;
                switch (ev.Action)
                {
                    case ControlBindings.SpyFireAction:
                        if (context.IsSpy && (context.InCamera || context.CameraTransition)) Pulse(ev.Control + ":camera", "Action:cameragas");
                        else Hold(ev.Control, "Action:fire", true);
                        break;
                    case ControlBindings.SpyGadgetAction:
                        if (context.IsSpy && context.InCamera) _cameraGadgetAt = _clock;
                        Hold(ev.Control, "Action:altfire", true);
                        break;
                    case ControlBindings.MercGadgetAction:
                        Hold(ev.Control, "Action:altfire", true);
                        if (context.Scoped) Hold(ev.Control + ":breath", "Action:breath", true);
                        break;
                    case ControlBindings.CrouchAction:
                        // Reloaded clears the crouch pulse on its next callback and calls AltFire in camera
                        // views. Emit the exit directly so a delayed pulse cannot crouch the player after exit.
                        if (context.InCamera) Pulse(ev.Control + ":camera", "Action:cameraexit");
                        else Hold(ev.Control, "Action:crouch", true);
                        break;
                    case ControlBindings.LongRollAction:
                        Hold(ev.Control, "Action:longroll", true);
                        // The queued crouch-release callback is registered even when the crouch latch is off.
                        if (context.InCamera) Pulse(ev.Control + ":camera", "Action:cameraexit");
                        break;
                }
            }
            else if (_presses.Remove(ev.Control, out (string Action, double At) press))
            {
                Release(ev.Control);
                Release(ev.Control + ":breath");
                if (_clock - press.At >= PadBindingState.HoldSeconds) continue;
                if (press.Action == ControlBindings.SpyFireAction) Pulse(ev.Control + ":tap", "Action:quick1");
                else if (press.Action == ControlBindings.SpyGadgetAction) SpyGadgetTap(ev.Control, context);
            }
        }

        return _events;
    }

    /// <summary>Releases all emitted held actions, including ordinary bindings, without invoking release commands.</summary>
    public IReadOnlyList<PadBindingEvent> ReleaseAll()
    {
        _events.Clear();
        foreach ((string control, string action) in _held) _events.Add(new(control, action, false));
        _held.Clear();
        _presses.Clear();
        _clock = 0;
        _cameraGadgetAt = double.NegativeInfinity;
        return _events;
    }

    private void SpyGadgetTap(string control, PadActionContext context)
    {
        if (context.WeaponDrawn || _clock - _cameraGadgetAt <= PadBindingState.HoldSeconds) return;
        Pulse(control + ":tap", "Action:quick2");
    }

    private static bool IsContextual(string action) => action is ControlBindings.SpyFireAction or ControlBindings.SpyGadgetAction
        or ControlBindings.MercGadgetAction or ControlBindings.CrouchAction or ControlBindings.LongRollAction;

    private void Hold(string control, string action, bool down)
    {
        if (!down)
        {
            Release(control);
            return;
        }

        if (_held.TryGetValue(control, out string? previous))
        {
            if (previous == action) return;
            Release(control);
        }

        _held.Add(control, action);
        _events.Add(new(control, action, true));
    }

    private void Release(string control)
    {
        if (_held.Remove(control, out string? action)) _events.Add(new(control, action, false));
    }

    private void Pulse(string control, string action)
    {
        _events.Add(new(control, action, true));
        _events.Add(new(control, action, false));
    }
}
