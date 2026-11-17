using System;
using System.Collections.Generic;
using OpenScct.Game.SBase;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// A command of a binding that is not the pawn's: the interface's (ShowMenu, Talk, DisplaySessionStatus,
/// ShowConnectedPlayers, ChangeGear, PushToTalk, TeamPing...). Unreal takes these and acts on them; held ones
/// (push to talk, the held scoreboard, the ping wheel) get their release too.
/// </summary>
public readonly record struct LocalCommand(string Name, int Arg, bool Pressed);

/// <summary>
/// Turns the keyboard and mouse into the local player's per-tick input (<see cref="LocalInput"/>) through the side's
/// key layout, exactly as OpenSCCT's game/Scripts/Input/PlayInput.cs does (with the physical-press ownership of its
/// BindingInput): a key's buttons are held while it is down (a press shorter than a tick still counts for one tick),
/// its exec commands run once on the press, its axes feed while it is down, and the mouse counts of a tick are
/// summed with no smoothing or clamp (Reloaded's modern mouse). Unreal reports the device state once a frame
/// (<see cref="Frame"/>); the session takes the gathered input before each 60 Hz tick (<see cref="FillTick"/>).
/// </summary>
public sealed class InputMapper
{
    private static readonly HashSet<string> s_pawnCommands = new(PlayerCommands.Known, StringComparer.OrdinalIgnoreCase);

    /// <summary>The most local commands kept when nothing takes them.</summary>
    private const int LocalCommandLimit = 256;

    private readonly LocalInput _target;
    private readonly HashSet<string> _down = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<int> _pressedSinceTick = new();
    private readonly List<(string Ik, bool Pressed)> _pending = new();
    private readonly HashSet<string> _physicalSources = new(StringComparer.OrdinalIgnoreCase);
    private readonly InputActionState _actionState = new();
    private readonly BindingInputState _state = new();
    private readonly List<BindingInputEvent> _events = new();
    private readonly bool[] _was = new bool[BridgeKeys.Count];
    private object? _actionPawn;
    private KeyLayout _layout;
    private int _layoutRevision;
    private float _mouseX;
    private float _mouseY;
    private bool? _padWalk;
    private bool _playing;
    private double _clockMsec;

    public InputMapper(LocalInput target, KeyLayout layout, bool merc, ControlSettings settings)
    {
        _target = target ?? throw new ArgumentNullException(nameof(target));
        _layout = layout ?? throw new ArgumentNullException(nameof(layout));
        _layoutRevision = layout.Revision;
        Merc = merc;
        Settings = settings ?? throw new ArgumentNullException(nameof(settings));
    }

    /// <summary>The side whose rules apply (the spy's cloak and first person, the merc's scope).</summary>
    public bool Merc { get; private set; }

    public ControlSettings Settings { get; set; }

    public KeyLayout Layout => _layout;

    /// <summary>The interface's commands since they were last taken, in order.</summary>
    public List<LocalCommand> LocalCommands { get; } = new();

    /// <summary>The auto-hack setting was flipped by its binding (the caller keeps and applies it).</summary>
    public event Action<bool>? AutoHackToggled;

    /// <summary>The mouse counts gathered since the last tick took them (the drawn view turns by them ahead of the tick).</summary>
    public (float X, float Y) PendingMouse => (_mouseX, _mouseY);

    /// <summary>True while game input is read (the last frame had the play flag).</summary>
    public bool Playing => _playing;

    /// <summary>A key or mouse button (binding name) is physically down and counts (not held over an interruption).</summary>
    public bool PhysicalHeld(string key) => _state.Held(key);

    /// <summary>
    /// Plays another layout or side. A layout with other bindings lets go of everything first, so no button stays
    /// down under a binding that no longer exists.
    /// </summary>
    public void SetLayout(KeyLayout layout, bool merc)
    {
        ArgumentNullException.ThrowIfNull(layout);
        if (ReferenceEquals(layout, _layout) && layout.Revision == _layoutRevision && merc == Merc)
        {
            return;
        }

        // The side alone changing keeps what is held (both sides may play one layout); other bindings do not.
        bool same = ReferenceEquals(layout, _layout) ? layout.Revision == _layoutRevision : layout.SameAs(_layout);
        if (!same)
        {
            ClearHeld();
        }

        _layout = layout;
        _layoutRevision = layout.Revision;
        Merc = merc;
    }

    /// <summary>
    /// One rendered frame of device state. Without <paramref name="play"/> (a menu is open, the window lost focus,
    /// the chat line has the keys) nothing is read and everything held is released; keys still down when play
    /// resumes are ignored until they are let go.
    /// </summary>
    /// <param name="keys">
    /// One byte per <see cref="BridgeKey"/>: bit 0 the key is down now, bit 1 it went down since the last frame (so
    /// a tap shorter than a frame is not lost). For the wheel codes: the notches turned since the last frame.
    /// </param>
    /// <param name="mouseX">Mouse counts since the last frame, right positive.</param>
    /// <param name="mouseY">Mouse counts since the last frame, up positive.</param>
    public void Frame(ReadOnlySpan<byte> keys, float mouseX, float mouseY, double deltaSeconds, bool play)
    {
        if (double.IsFinite(deltaSeconds) && deltaSeconds > 0)
        {
            _clockMsec += deltaSeconds * 1000.0;
        }

        if (_layout.Revision != _layoutRevision)
        {
            // The layout was edited in place.
            ClearHeld();
            _layoutRevision = _layout.Revision;
        }

        int count = Math.Min(keys.Length, BridgeKeys.FirstPad);
        if (!play)
        {
            if (_playing)
            {
                _playing = false;
                ClearHeld();
            }

            return;
        }

        if (!_playing)
        {
            // Play starts or resumes: what is held now was pressed for something else.
            _playing = true;
            for (int code = 0; code < count; code++)
            {
                if (BridgeKeys.IsWheel(code) || BridgeKeys.NameOf(code) is not { } name)
                {
                    continue;
                }

                bool down = (keys[code] & 1) != 0;
                _was[code] = down;
                if (down)
                {
                    _state.BlockHeld(name);
                }
                else
                {
                    _state.Forget(name);
                }
            }
        }
        else
        {
            // Releases first, then presses with the modifiers ahead of the keys they modify: the state of a frame
            // has no order of its own.
            for (int code = 0; code < count; code++)
            {
                if (_was[code] && (keys[code] & 1) == 0 && !BridgeKeys.IsWheel(code) && BridgeKeys.NameOf(code) is { } name)
                {
                    _was[code] = false;
                    Physical(name, false);
                }
            }

            for (int code = (int)BridgeKey.Shift; code <= (int)BridgeKey.Alt; code++)
            {
                Press(keys, code);
            }

            for (int code = 0; code < count; code++)
            {
                if (code is < (int)BridgeKey.Shift or > (int)BridgeKey.Alt)
                {
                    Press(keys, code);
                }
            }

            if (keys.Length > (int)BridgeKey.MouseWheelDown)
            {
                for (int n = keys[(int)BridgeKey.MouseWheelUp]; n > 0; n--)
                {
                    Physical("MouseWheelUp", true);
                }

                for (int n = keys[(int)BridgeKey.MouseWheelDown]; n > 0; n--)
                {
                    Physical("MouseWheelDown", true);
                }
            }
        }

        _mouseX += mouseX;
        _mouseY += mouseY;
    }

    private void Press(ReadOnlySpan<byte> keys, int code)
    {
        if (code >= keys.Length || _was[code] || BridgeKeys.IsWheel(code) || BridgeKeys.NameOf(code) is not { } name)
        {
            return;
        }

        byte state = keys[code];
        if ((state & 3) == 0)
        {
            return;
        }

        Physical(name, true);
        if ((state & 1) != 0)
        {
            _was[code] = true;
        }
        else
        {
            // Down and up again within the frame.
            Physical(name, false);
        }
    }

    /// <summary>A physical key or button changed: the binding it owns (plain, chord or double tap) goes down or up.</summary>
    private void Physical(string name, bool pressed)
    {
        _events.Clear();
        _state.Update(_layout, name, pressed, _state.HeldModifiers, (long)_clockMsec, _events);
        for (int i = 0; i < _events.Count; i++)
        {
            BindingInputEvent ev = _events[i];
            if (ev.Pressed)
            {
                _physicalSources.Add(ev.Key);
                Key(ev.Key, true);
            }
            else if (_physicalSources.Remove(ev.Key))
            {
                Key(ev.Key, false);
            }
        }
    }

    /// <summary>
    /// A binding key went down or up (PlayInput.Key): a layout key, or a controller's virtual key
    /// (<see cref="ControlActions.VirtualPrefix"/>).
    /// </summary>
    public void Key(string ik, bool pressed)
    {
        bool pulse = BindingKey.IsPulseName(ik);
        if (pressed)
        {
            if (!_down.Add(ik) && !pulse)
            {
                return;
            }

            _pending.Add((ik, true));
        }
        else if (_down.Remove(ik))
        {
            _pending.Add((ik, false));
        }

        // The wheel has no release.
        if (pressed && pulse)
        {
            _down.Remove(ik);
            _pending.Add((ik, false));
        }
    }

    /// <summary>While the controller's stick drives movement, its run threshold owns walk and run for that tick.</summary>
    public void SetPadWalk(bool? walk) => _padWalk = walk;

    /// <summary>
    /// Writes the input gathered since the last tick into <paramref name="input"/> (PlayInput.FillTick): presses run
    /// their commands and set their buttons, releases clear them after the tick has seen them, the mouse counts are
    /// handed over. Call it once per client tick, before the tick.
    /// </summary>
    public void FillTick(LocalInput input, SPlayerController? pc)
    {
        ArgumentNullException.ThrowIfNull(input);
        object? pawn = pc?.SBPawn;
        if (!ReferenceEquals(pawn, _actionPawn))
        {
            // A new pawn starts running, unscoped and in third person, whatever the last one was doing.
            _actionPawn = pawn;
            _actionState.Clear();
            input.Commands.Add(new InputCommand("SetWalk", 0));
            input.Commands.Add(new InputCommand("SetScope", 0));
            input.Commands.Add(new InputCommand("SetSpyFirstPerson", 0));
        }

        // Buttons released earlier, that were held for the last tick, go up now.
        foreach (int i in _pressedSinceTick)
        {
            input[i] = 0;
        }

        _pressedSinceTick.Clear();
        bool merc = Merc;
        LookSettings behavior = Settings.LookOf(merc, pad: false);
        bool wheelState = pc?.StateNode is SPlayerController.IWheel;
        for (int e = 0; e < _pending.Count; e++)
        {
            (string ik, bool pressed) = _pending[e];
            bool pulse = BindingKey.IsPulseName(ik);
            if (pressed && pulse)
            {
                string physical = BindingKey.TryParse(ik, out BindingKey binding) ? binding.Base : ik;
                if (string.Equals(physical, "MouseWheelUp", StringComparison.OrdinalIgnoreCase))
                {
                    input.Wheel++;
                }
                else if (string.Equals(physical, "MouseWheelDown", StringComparison.OrdinalIgnoreCase))
                {
                    input.Wheel--;
                }
            }

            IReadOnlyList<KeyAction> actions = ActionsOf(ik);
            bool targetedGadget = SuppressAlternateFire(actions, pc);
            for (int i = 0; i < actions.Count; i++)
            {
                KeyAction a = actions[i];
                bool command = a.Kind == KeyActionKind.Command;

                // A quick-slot binding may share the legacy mouse alias for alternate fire.
                // Only its explicit target should run; the currently displayed gadget must stay idle.
                if (targetedGadget && (Is(a.Name, "bAltFire") || Is(a.Name, "AltFire")))
                {
                    continue;
                }

                if (command && Is(a.Name, ControlActions.AutoHackCommand))
                {
                    // A setting, not a pawn command: the caller keeps it, the interface shows the message.
                    if (pressed)
                    {
                        bool on = !Settings.AutoHackComms;
                        AutoHackToggled?.Invoke(on);
                        AddLocal(a.Name, on ? 1 : 0, true);
                    }

                    continue;
                }

                if (!merc && a.Kind == KeyActionKind.Button && Is(a.Name, "bCloak") && pulse)
                {
                    // The wheel cannot hold the cloak button: each notch toggles the cloak.
                    if (pressed)
                    {
                        input.Commands.Add(new InputCommand("ToggleCloak"));
                    }

                    continue;
                }

                if (command && Is(a.Name, "SetSpyFirstPerson"))
                {
                    if (!merc && pressed)
                    {
                        _actionState.SetSpyFirstPerson(a.Arg != 0);
                        input.Commands.Add(new InputCommand("SetSpyFirstPerson", a.Arg != 0 ? 1 : 0));
                    }

                    continue;
                }

                if (command && Is(a.Name, "SwitchSpyPerspective"))
                {
                    if (!merc)
                    {
                        bool firstPerson = _actionState.SpyPerspective(ik, pressed, behavior.PerspectiveBehavior == "Hold", pulse);
                        input.Commands.Add(new InputCommand("SetSpyFirstPerson", firstPerson ? 1 : 0));
                    }

                    continue;
                }

                if (command && a.Name is "Walk" or "ChangeSpeed")
                {
                    if (pulse && wheelState)
                    {
                        // A view that zooms with the wheel (binoculars, a scope, a camera) takes the notch itself.
                        if (pressed)
                        {
                            input.Commands.Add(new InputCommand("WheelDown"));
                        }
                    }
                    else
                    {
                        bool walk = _actionState.Walk(ik, pressed, behavior.WalkBehavior == "Hold", pulse);
                        input.Commands.Add(new InputCommand("SetWalk", walk ? 1 : 0));
                    }

                    continue;
                }

                if (command && a.Name == "Run" && pressed && !wheelState)
                {
                    _actionState.Run();
                }

                if (merc && behavior.ScopeBehavior == "Hold" && command && a.Name == "SwitchBinocularVision" && !pulse)
                {
                    bool scope = _actionState.Scope(ik, pressed);
                    input.Commands.Add(new InputCommand("SetScope", scope ? 1 : 0));
                    continue;
                }

                switch (a.Kind)
                {
                    case KeyActionKind.Button when LocalInput.IndexOf(a.Name) is var b and >= 0:
                        if (pressed)
                        {
                            input[b] = 1;
                        }
                        else if (!IsButtonHeld(a.Name, pc))
                        {
                            // _down includes later events in this batch: a quick re-press of this key
                            // must keep holding its buttons after the delayed release would run.
                            // Keep it down for this tick if it was pressed since the last one.
                            _pressedSinceTick.Add(b);
                        }

                        break;
                    case KeyActionKind.Command when s_pawnCommands.Contains(a.Name):
                        if (pressed)
                        {
                            input.Commands.Add(new InputCommand(a.Name, a.Arg));
                        }

                        break;
                    case KeyActionKind.Command:
                        AddLocal(a.Name, a.Arg, pressed);
                        break;
                }
            }
        }

        _pending.Clear();
        if (_padWalk is { } padWalk)
        {
            input.Commands.Add(new InputCommand("SetWalk", padWalk ? 1 : 0));
        }

        if (_actionState.ScopeHeld)
        {
            input.Commands.Add(new InputCommand("SetScope", 1));
        }

        if (_actionState.SpyFirstPersonRequested && !merc)
        {
            input.Commands.Add(new InputCommand("SetSpyFirstPerson", 1));
        }

        // What is held now: the gadget keys and the axes (Jump's +280 on aUp while Space is down).
        float up = 0f;
        input.HeldGadgetBindings = 0;
        foreach (string ik in _down)
        {
            IReadOnlyList<KeyAction> actions = ActionsOf(ik);
            for (int i = 0; i < actions.Count; i++)
            {
                KeyAction a = actions[i];
                if (a.Kind == KeyActionKind.Command && Is(a.Name, "UseGadget"))
                {
                    input.HeldGadgetBindings |= LocalInput.GadgetBindingBit(a.Arg);
                }

                if (a.Kind == KeyActionKind.Axis && Is(a.Name, "aUp"))
                {
                    up += a.Speed;
                }
            }
        }

        input.Up = up;
        // The free camera flies fast with Shift and slowly with Alt, whatever they are bound to.
        input.Fast = _state.Held("Shift");
        input.Slow = _state.Held("Alt");
        input.MouseX += _mouseX;
        input.MouseY += _mouseY;
        _mouseX = 0f;
        _mouseY = 0f;
    }

    /// <summary>
    /// Releases every button (PlayInput.ClearHeld: focus lost, a menu opened, another layout). The keys still down
    /// are ignored until they are physically released.
    /// </summary>
    public void ClearHeld()
    {
        // Held interface commands (push to talk, the held scoreboard) end too.
        foreach (string ik in _down)
        {
            IReadOnlyList<KeyAction> actions = ActionsOf(ik);
            for (int i = 0; i < actions.Count; i++)
            {
                KeyAction a = actions[i];
                if (a.Kind == KeyActionKind.Command && !s_pawnCommands.Contains(a.Name) && !Is(a.Name, ControlActions.AutoHackCommand))
                {
                    AddLocal(a.Name, a.Arg, false);
                }
            }
        }

        _events.Clear();
        _state.Reset(_events);
        _events.Clear();
        _physicalSources.Clear();
        _padWalk = null;
        _actionState.Clear();
        _down.Clear();
        _pending.Clear();
        _pressedSinceTick.Clear();
        _mouseX = 0f;
        _mouseY = 0f;
        _target.Clear();
        _target.Commands.Add(new InputCommand("SetWalk", 0));
        _target.Commands.Add(new InputCommand("SetScope", 0));
        _target.Commands.Add(new InputCommand("SetSpyFirstPerson", 0));
    }

    /// <summary>The first layout key whose binding has <paramref name="action"/> (a button or command name), or null.</summary>
    public string? KeyFor(string action)
    {
        foreach (string key in _layout.Bindings.Keys)
        {
            IReadOnlyList<KeyAction> actions = _layout.Actions(key);
            for (int i = 0; i < actions.Count; i++)
            {
                if (actions[i].Kind != KeyActionKind.Axis && Is(actions[i].Name, action))
                {
                    return key;
                }
            }
        }

        return null;
    }

    private IReadOnlyList<KeyAction> ActionsOf(string key) =>
        key.StartsWith(ControlActions.VirtualPrefix, StringComparison.Ordinal) ? ControlActions.VirtualActions(key) : _layout.Actions(key);

    private void AddLocal(string name, int arg, bool pressed)
    {
        if (LocalCommands.Count >= LocalCommandLimit)
        {
            LocalCommands.RemoveAt(0);
        }

        LocalCommands.Add(new LocalCommand(name, arg, pressed));
    }

    /// <summary>
    /// The key targets a gadget of its own, so its alternate fire parts must not also use the gadget in hand
    /// (PlayInput.SuppressAlternateFire).
    /// </summary>
    private static bool SuppressAlternateFire(IReadOnlyList<KeyAction> actions, SPlayerController? pc)
    {
        for (int i = 0; i < actions.Count; i++)
        {
            KeyAction action = actions[i];
            if (action.Kind != KeyActionKind.Command)
            {
                continue;
            }

            if (Is(action.Name, "LaunchGadget") || Is(action.Name, "UseGadget"))
            {
                return true;
            }

            // Retail mouse aliases also contain GadgetQuickChoice, but that command only
            // handles an unarmed spy. Merc gadgets, armed spy gadgets and camera exits need AltFire.
            if (Is(action.Name, "GadgetQuickChoice") &&
                pc?.SBPawn is SPawnAttaque { WeaponLoaded: 0 } &&
                pc.StateNode is SPlayerController.s_PlayerWalkingMario)
            {
                return true;
            }
        }

        return false;
    }

    private bool IsButtonHeld(string button, SPlayerController? pc)
    {
        bool altFire = Is(button, "bAltFire");
        foreach (string ik in _down)
        {
            IReadOnlyList<KeyAction> actions = ActionsOf(ik);
            if (altFire && SuppressAlternateFire(actions, pc))
            {
                continue;
            }

            for (int i = 0; i < actions.Count; i++)
            {
                if (actions[i].Kind == KeyActionKind.Button && Is(actions[i].Name, button))
                {
                    return true;
                }
            }
        }

        return false;
    }

    private static bool Is(string name, string other) => string.Equals(name, other, StringComparison.OrdinalIgnoreCase);
}
