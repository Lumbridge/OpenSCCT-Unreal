using System;
using System.Collections.Generic;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>A binding key (plain, chord or double tap) went down or up.</summary>
public readonly record struct BindingInputEvent(string Key, bool Pressed);

/// <summary>
/// Which binding each physical press owns (ported from OpenSCCT game/Scripts/Input/BindingInputState.cs). A chord
/// never turns into its plain key: the binding chosen on the press is the one released, whatever happens to the
/// modifiers meanwhile. A double tap (<c>Double+H</c>) is extra: the second quick press of an input raises it after
/// the input's ordinary binding. Inputs held when play is interrupted stay ignored until they are let go.
/// </summary>
public sealed class BindingInputState
{
    /// <summary>The longest time between two presses of one input that still counts as a double tap.</summary>
    public const long DoubleTapMsec = 300;

    private readonly HashSet<string> _down = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> _blocked = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, string> _owners = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, string> _doubleOwners = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<string> _scratch = new();
    private string? _lastTap;
    private long _lastTapAt;

    public IReadOnlySet<string> PhysicalDown => _down;

    /// <summary>A physical input is down and was not held over an interruption.</summary>
    public bool Held(string input) => _down.Contains(input) && !_blocked.Contains(input);

    /// <summary>The modifiers held now (not the blocked ones).</summary>
    public BindingModifiers HeldModifiers =>
        (Held("Ctrl") ? BindingModifiers.Ctrl : BindingModifiers.None)
        | (Held("Shift") ? BindingModifiers.Shift : BindingModifiers.None)
        | (Held("Alt") ? BindingModifiers.Alt : BindingModifiers.None);

    /// <summary>
    /// One physical press or release. The binding events it causes are added to <paramref name="events"/>.
    /// </summary>
    /// <param name="nowMsec">The time of the event, for double taps.</param>
    public void Update(KeyLayout layout, string input, bool pressed, BindingModifiers modifiers, long nowMsec,
        List<BindingInputEvent> events)
    {
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(events);
        BindingModifiers ownModifier = BindingKey.ModifierOf(input);
        bool pulse = input.StartsWith("MouseWheel", StringComparison.OrdinalIgnoreCase);
        if (!pressed)
        {
            _down.Remove(input);
            _blocked.Remove(input);
            Release(input, events);
            if (ownModifier != BindingModifiers.None)
            {
                // A chord ends with its modifier.
                ReleaseChords(_owners, ownModifier, events);
                ReleaseChords(_doubleOwners, ownModifier, events);
            }

            return;
        }

        if (!pulse && !_down.Add(input))
        {
            return;
        }

        if (_blocked.Contains(input))
        {
            _lastTap = null;
            return;
        }

        // The wheel neither makes nor interrupts a double tap. A third quick press starts a new count.
        bool second = false;
        if (!pulse)
        {
            second = string.Equals(_lastTap, input, StringComparison.OrdinalIgnoreCase) && nowMsec - _lastTapAt <= DoubleTapMsec;
            _lastTap = second ? null : input;
            _lastTapAt = nowMsec;
        }

        if (BindingKey.Select(layout, input, modifiers) is { } selected)
        {
            events.Add(new BindingInputEvent(selected, true));
            if (pulse)
            {
                events.Add(new BindingInputEvent(selected, false));
            }
            else
            {
                _owners[input] = selected;
            }
        }

        if (second && BindingKey.SelectDouble(layout, input, modifiers) is { } doubled)
        {
            events.Add(new BindingInputEvent(doubled, true));
            _doubleOwners[input] = doubled;
        }
    }

    private void Release(string input, List<BindingInputEvent> events)
    {
        if (_owners.Remove(input, out string? owner))
        {
            events.Add(new BindingInputEvent(owner, false));
        }

        if (_doubleOwners.Remove(input, out string? doubled))
        {
            events.Add(new BindingInputEvent(doubled, false));
        }
    }

    private void ReleaseChords(Dictionary<string, string> owners, BindingModifiers modifier, List<BindingInputEvent> events)
    {
        _scratch.Clear();
        foreach ((string source, string owner) in owners)
        {
            if (BindingKey.TryParse(owner, out BindingKey key) && (key.Modifiers & modifier) != 0)
            {
                _scratch.Add(source);
            }
        }

        foreach (string source in _scratch)
        {
            if (owners.Remove(source, out string? owner))
            {
                events.Add(new BindingInputEvent(owner, false));
            }
        }
    }

    /// <summary>
    /// Play was interrupted (focus lost, a menu opened): every binding is released into <paramref name="events"/>,
    /// and the inputs still down are ignored until they are physically released, so a key held through the
    /// interruption cannot fire when play resumes.
    /// </summary>
    public void Reset(List<BindingInputEvent> events)
    {
        ArgumentNullException.ThrowIfNull(events);
        foreach (string owner in _owners.Values)
        {
            events.Add(new BindingInputEvent(owner, false));
        }

        foreach (string owner in _doubleOwners.Values)
        {
            events.Add(new BindingInputEvent(owner, false));
        }

        _owners.Clear();
        _doubleOwners.Clear();
        _lastTap = null;
        _blocked.UnionWith(_down);
    }

    /// <summary>Play resumes with <paramref name="input"/> physically down: it is ignored until released.</summary>
    public void BlockHeld(string input)
    {
        _down.Add(input);
        _blocked.Add(input);
    }

    /// <summary>Play resumes with <paramref name="input"/> physically up (its release was not seen).</summary>
    public void Forget(string input)
    {
        _down.Remove(input);
        _blocked.Remove(input);
    }
}

/// <summary>
/// The hold-or-toggle actions (walk, the merc's scope, the spy's first person), ported from OpenSCCT
/// game/Scripts/Input/InputActionState.cs. Hold sources are tracked one by one, so releasing one key never cancels
/// another held source.
/// </summary>
public sealed class InputActionState
{
    private readonly HashSet<string> _walkHeld = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> _scopeHeld = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> _perspectiveHeld = new(StringComparer.OrdinalIgnoreCase);
    private bool _walkToggle;
    private bool _perspectiveToggle;

    public bool ScopeHeld => _scopeHeld.Count > 0;

    public bool SpyFirstPersonRequested => _perspectiveToggle || _perspectiveHeld.Count > 0;

    /// <summary>A walk key went down or up; true while the player should walk. The wheel always toggles.</summary>
    public bool Walk(string source, bool pressed, bool hold, bool pulse)
    {
        if (hold && !pulse)
        {
            if (pressed) _walkHeld.Add(source); else _walkHeld.Remove(source);
        }
        else if (pressed)
        {
            _walkToggle = !_walkToggle;
        }

        return _walkToggle || _walkHeld.Count > 0;
    }

    public bool Scope(string source, bool pressed)
    {
        if (pressed) _scopeHeld.Add(source); else _scopeHeld.Remove(source);
        return _scopeHeld.Count > 0;
    }

    public bool SpyPerspective(string source, bool pressed, bool hold, bool pulse)
    {
        if (hold && !pulse)
        {
            if (pressed) _perspectiveHeld.Add(source); else _perspectiveHeld.Remove(source);
        }
        else if (pressed)
        {
            _perspectiveToggle = !_perspectiveToggle;
        }

        return SpyFirstPersonRequested;
    }

    /// <summary>A direct choice supersedes both the toggle and any held perspective sources.</summary>
    public void SetSpyFirstPerson(bool firstPerson)
    {
        _perspectiveToggle = firstPerson;
        _perspectiveHeld.Clear();
    }

    public void Run()
    {
        _walkToggle = false;
        _walkHeld.Clear();
    }

    public void Clear()
    {
        Run();
        _scopeHeld.Clear();
        _perspectiveHeld.Clear();
        _perspectiveToggle = false;
    }
}
