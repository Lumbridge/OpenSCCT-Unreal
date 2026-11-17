using System;
using System.Collections.Generic;
using System.Linq;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// Captures one controller gesture for a binding: a single button or trigger, Hold B (B held for
/// <see cref="PadBindingState.HoldSeconds"/>), or one of the supported combinations (LS+Y, LB+DpadDown...). The
/// opener must be released before capture starts; it commits on a release, which leaves time to recognize a
/// combination or B's long press. Ported from OpenSCCT game/Scripts/Options/PadBindingCapture.cs.
/// </summary>
public sealed class PadBindingCapture
{
    private static readonly string[] s_physical = ControlBindings.PadControls[..ControlBindings.PhysicalPadControls];

    private readonly HashSet<string> _previous = new(StringComparer.Ordinal);
    private readonly Dictionary<string, double> _seconds = new(StringComparer.Ordinal);
    private string? _candidate;
    private bool _complete;

    /// <summary>Waiting for every control to be let go, or (once they were) for the gesture.</summary>
    public bool Armed { get; private set; }

    /// <summary>Why the last gesture was refused (an unsupported combination); null when none was.</summary>
    public string? Error { get; private set; }

    /// <summary>The gesture as held so far ("LS+Y", "B:Hold"), null when none.</summary>
    public string? Preview => _candidate;

    /// <summary>The gesture has been returned.</summary>
    public bool Complete => _complete;

    /// <summary>
    /// Returns the captured gesture once, when one of its controls is released. Only the controller's own buttons and
    /// triggers count; stick motion cannot become a binding. An unsupported combination waits for release and starts
    /// over.
    /// </summary>
    public string? Update(IReadOnlySet<string> down, double delta)
    {
        ArgumentNullException.ThrowIfNull(down);
        if (_complete)
        {
            return null;
        }

        var physical = new HashSet<string>(down.Where(control => Array.IndexOf(s_physical, control) >= 0), StringComparer.Ordinal);
        if (!Armed)
        {
            if (physical.Count == 0)
            {
                Armed = true;
                _previous.Clear();
                _seconds.Clear();
                _candidate = null;
            }

            return null;
        }

        double elapsed = double.IsFinite(delta) && delta > 0 ? delta : 0;
        foreach (string control in _previous)
        {
            _seconds[control] = _seconds.GetValueOrDefault(control) + elapsed;
        }

        // A chord is already identified before either control is released. A released chord does not become the
        // remaining control's own binding, and a long B press does not become B's tap.
        if (_candidate is { } candidate && GestureControls(candidate).Any(control => !physical.Contains(control)))
        {
            if (candidate == "B" && _seconds.GetValueOrDefault("B") >= PadBindingState.HoldSeconds)
            {
                candidate = "B:Hold";
            }

            _candidate = candidate;
            _complete = true;
            Armed = false;
            return candidate;
        }

        foreach (string control in physical)
        {
            if (!_previous.Contains(control))
            {
                _seconds[control] = 0;
            }
        }

        if (physical.Count == 1)
        {
            string single = physical.Single();
            _candidate = single == "B" && _seconds.GetValueOrDefault("B") >= PadBindingState.HoldSeconds ? "B:Hold" : single;
            Error = null;
        }
        else if (physical.Count > 1)
        {
            string? chord = physical.Count == 2
                ? ControlBindings.PadControls.FirstOrDefault(control => control.Contains('+') && physical.SetEquals(GestureControls(control)))
                : null;
            if (chord is null)
            {
                Error = "That combination cannot be bound. Let go, then press one button, hold B, or a left stick or left bumper combination.";
                _candidate = null;
                Armed = false;
            }
            else
            {
                _candidate = chord;
                Error = null;
            }
        }

        _previous.Clear();
        _previous.UnionWith(physical);
        return null;
    }

    private static IEnumerable<string> GestureControls(string gesture) => gesture.EndsWith(":Hold", StringComparison.Ordinal)
        ? new[] { gesture[..^5] } : gesture.Split('+');
}
