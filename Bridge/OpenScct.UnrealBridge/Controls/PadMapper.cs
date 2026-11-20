using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Game.SBase;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// Controller play, ported from OpenSCCT game/Scripts/Options/GamepadInput.cs (Reloaded's native controller support):
/// the left stick (radial deadzone, optional eight-way snap) is the analog move (<see cref="LocalInput.PadForward"/>:
/// full circle, a part push walks slower) and also holds the direction buttons past <see cref="MovePress"/>; the right
/// stick adds look counts (deadzone, radial response curve, vertical scale, the side's pad look multipliers);
/// <see cref="PadBindingState"/> and <see cref="PadActionState"/> resolve the side's buttons, held actions and
/// combinations into actions, each held as a virtual key of the <see cref="InputMapper"/>. The spy runs above the run
/// threshold. Stick axes come in with up positive; inside, as in the original code, down is positive.
/// </summary>
public sealed class PadMapper
{
    /// <summary>How far the left stick leans before a direction button goes down.</summary>
    public const float MovePress = 0.38f;

    /// <summary>
    /// The look stick's rate at full deflection with a multiplier of 1, in mouse counts a second. Reloaded's pad code
    /// leaves the engine <c>aMouseX = 62 x stick x multiplier x clutch</c> at any frame rate; a mouse count gives
    /// <c>0.1 x multiplier / 10 x 2 / DeltaTime</c> (<see cref="MouseLook"/>), so the stick is worth
    /// 62 / (0.1 / 10 x 2) = 3100 counts a second: 9300 rotation units (51.1 degrees) a second per unit of multiplier.
    /// </summary>
    public const float StickCountsPerSecond = 62f / (MouseLook.AxisEventScale / MouseLook.MultiplierDivisor * MouseLook.ReadInputScale);

    /// <summary>The look multiplier Reloaded's pad code uses in a camera view (sticky camera, camera network), before the side's cam setting.</summary>
    public const float CameraStickMultiplier = 3.2f;

    private static readonly Dictionary<string, BridgeKey> s_buttons = new(StringComparer.Ordinal)
    {
        ["A"] = BridgeKey.PadA, ["B"] = BridgeKey.PadB, ["X"] = BridgeKey.PadX, ["Y"] = BridgeKey.PadY,
        ["LB"] = BridgeKey.PadLB, ["RB"] = BridgeKey.PadRB, ["LS"] = BridgeKey.PadLS, ["RS"] = BridgeKey.PadRS,
        ["DpadUp"] = BridgeKey.PadDpadUp, ["DpadDown"] = BridgeKey.PadDpadDown,
        ["DpadLeft"] = BridgeKey.PadDpadLeft, ["DpadRight"] = BridgeKey.PadDpadRight,
        ["Start"] = BridgeKey.PadStart, ["Back"] = BridgeKey.PadBack,
        ["P1"] = BridgeKey.PadP1, ["P2"] = BridgeKey.PadP2, ["P3"] = BridgeKey.PadP3, ["P4"] = BridgeKey.PadP4,
    };

    private static readonly string[] s_freeCamControls = ["LT", "RT", "LB", "RB"];

    private readonly InputMapper _keys;
    private readonly PlaySession _session;

    /// <summary>The key each held control (a pad control or a stick direction) holds, so the release lifts that key.</summary>
    private readonly Dictionary<string, string> _held = new(StringComparer.Ordinal);
    private readonly HashSet<string> _down = new(StringComparer.Ordinal);
    private readonly PadBindingState _buttonsState = new();
    private readonly PadActionState _actionState = new();
    private readonly HashSet<string> _clutchOne = new(StringComparer.Ordinal);
    private readonly HashSet<string> _clutchTwo = new(StringComparer.Ordinal);
    private readonly List<string> _scratch = new();
    private Dictionary<string, string>? _padFor;
    private ControlSettings? _settingsFor;
    private MouseLook _padLook = new();
    private bool _wasMerc;
    private bool _stickSet;
    private bool _wasFlying;
    private bool _lbWas;
    private bool _rbWas;

    /// <summary>A button held when play starts is not a press: nothing fires until the pad is let go once.</summary>
    private bool _suppressButtonsUntilRelease = true;

    public PadMapper(InputMapper keys, PlaySession session)
    {
        _keys = keys ?? throw new ArgumentNullException(nameof(keys));
        _session = session ?? throw new ArgumentNullException(nameof(session));
    }

    /// <summary>True while the controller moved, looked or pressed something in the last frame (for the interface's button prompts).</summary>
    public bool Active { get; private set; }

    /// <summary>One rendered frame of the controller's state.</summary>
    public bool Frame(ReadOnlySpan<byte> keys, Vector2 leftStick, Vector2 rightStick, float leftTrigger, float rightTrigger,
        ControlSettings settings, Dictionary<string, string> buttons, bool merc, bool play, double delta,
        out Vector2 lookCounts, bool allowMercFreeLook = false, bool reserveReleaseFrame = false)
    {
        ArgumentNullException.ThrowIfNull(settings);
        lookCounts = Vector2.Zero;
        ArgumentNullException.ThrowIfNull(buttons);
        PadTuning pad = settings.Pad;
        LookSettings swaps = settings.LookOf(merc, pad: true);
        _down.Clear();
        for (int i = 0; i < ControlBindings.PhysicalPadControls; i++)
        {
            string control = ControlBindings.PadControls[i];
            if (IsDown(keys, swaps.PhysicalControl(control), leftTrigger, rightTrigger, pad.TriggerThreshold))
            {
                _down.Add(control);
            }
        }

        Active = false;
        if (!pad.Enabled || !play)
        {
            ReleaseAll();
            _buttonsState.Reset(_down);
            _suppressButtonsUntilRelease = true;
            return play && allowMercFreeLook && (reserveReleaseFrame || _keys.MercFreeLookHeld);
        }

        if (!ReferenceEquals(_padFor, buttons) || merc != _wasMerc || !ReferenceEquals(_settingsFor, settings))
        {
            bool hadLayout = _padFor is not null;
            ReleaseAll();
            if (hadLayout)
            {
                _buttonsState.Reset(_down);
            }

            _padFor = buttons;
            _settingsFor = settings;
            _wasMerc = merc;
            _padLook = settings.MouseLookOf(pad: true);
        }

        if (pad.Move)
        {
            Vector2 move = Radial(new Vector2(leftStick.X, -leftStick.Y), pad.DeadzoneLeft);
            bool snap = merc ? pad.MercMove8 : pad.SpyMove8;
            if (snap && move != Vector2.Zero)
            {
                float a = MathF.Round(MathF.Atan2(move.Y, move.X) / (MathF.PI / 4f)) * (MathF.PI / 4f);
                move = new Vector2(MathF.Cos(a), MathF.Sin(a)) * move.Length();
            }

            // The direction buttons still go down past MovePress (every reader of the button bytes sees them), but
            // the move itself is the analog stick: any direction, and a part push walks slower unless slow walk is
            // off, when only the direction counts.
            Hold("MoveUp", "Action:forward", move.Y < -MovePress);
            Hold("MoveDown", "Action:back", move.Y > MovePress);
            Hold("MoveLeft", "Action:left", move.X < -MovePress);
            Hold("MoveRight", "Action:right", move.X > MovePress);
            float length = move.Length();
            Vector2 analog = pad.SlowWalk || length <= 0f ? move : move / length;
            _session.Input.SetPadStick(-analog.Y, analog.X);
            _stickSet = true;
            _keys.SetPadWalk(!merc && pad.SlowWalk && length > 0f ? length < pad.RunThreshold : null);
            Active |= length > 0f;
        }
        else
        {
            ClearStick();
        }

        bool flying = FreeCamera(keys, leftTrigger, rightTrigger, pad.TriggerThreshold);
        if (flying != _wasFlying)
        {
            ReleaseButtons();
            _buttonsState.Reset(_down);
            _wasFlying = flying;
        }

        if (flying)
        {
            // The free camera takes the triggers and the bumpers.
            foreach (string control in ControlBindings.PadControls.AsSpan(0, ControlBindings.PhysicalPadControls))
            {
                if (Array.IndexOf(s_freeCamControls, swaps.PhysicalControl(control)) >= 0)
                {
                    _down.Remove(control);
                }
            }
        }

        if (_suppressButtonsUntilRelease && _down.Count == 0)
        {
            _suppressButtonsUntilRelease = false;
        }

        if (pad.Binds && !_suppressButtonsUntilRelease)
        {
            Active |= _down.Count > 0;
            UpdateButtons(buttons, merc, delta);
        }
        else
        {
            ReleaseButtons();
            _buttonsState.Reset(_down);
        }

        bool freeLook = allowMercFreeLook && (reserveReleaseFrame || _keys.MercFreeLookHeld);
        if (pad.Aim && !freeLook)
        {
            Vector2 look = Radial(new Vector2(rightStick.X, -rightStick.Y), pad.DeadzoneRight);
            if (look != Vector2.Zero)
            {
                LookSettings group = settings.LookOf(merc, pad: true);
                float k = StickCountsPerSecond * pad.LookScale * (float)delta;
                float clutch = group.ClutchOf(_clutchOne.Count > 0, _clutchTwo.Count > 0);
                (float h, float v) = PadMultipliers();
                string response = !merc && _session.LocalPawn?.WeaponLoaded == 2 ? group.GunLookResponse : group.LookResponse;
                bool camera = _session.LocalController is { } viewer && MouseLook.InCameraView(viewer);
                Vector2 shaped = Shape(look, response) * clutch * CurveGain(response, camera, merc);
                lookCounts = new Vector2(shaped.X * k * h, -shaped.Y * k * v * pad.VerticalScale);
                Active = true;
            }
        }
        return freeLook;
    }

    /// <summary>Lets go of everything the controller holds (play interrupted, the controller switched off).</summary>
    public void ReleaseAll()
    {
        ReleaseButtons();
        ClearStick();
        _session.Input.PadUp = 0f;
        if (_held.Count == 0)
        {
            return;
        }

        foreach (string key in _held.Values)
        {
            if (!BindingKey.IsPulseName(key))
            {
                _keys.Key(key, false);
            }
        }

        _held.Clear();
    }

    /// <summary>The side's buttons after stick and bumper swaps, resolved into actions.</summary>
    private void UpdateButtons(Dictionary<string, string> buttons, bool merc, double delta)
    {
        SPlayerController? pc = _session.LocalController;
        SBasePawn? pawn = pc?.SBPawn;
        var context = new PadActionContext(
            InCamera: pc is not null && PlayerCommands.InCamera(pc),
            IsSpy: !merc,
            WeaponDrawn: pawn?.WeaponLoaded == 2,
            Scoped: pc?.bBinocularVision == true,
            CameraTransition: pc?.IsInState("s_UseObject_StickyCam_SmoothToPosition") == true,
            MercWeaponView: _keys.MercControls);
        IReadOnlyList<PadBindingEvent> events = _actionState.Update(_buttonsState.Update(buttons, _down, delta, context.MercWeaponView), delta, context);
        for (int i = 0; i < events.Count; i++)
        {
            PadBindingEvent ev = events[i];
            if (ev.Action is ControlBindings.ClutchOneAction or ControlBindings.ClutchTwoAction)
            {
                HashSet<string> held = ev.Action == ControlBindings.ClutchOneAction ? _clutchOne : _clutchTwo;
                if (ev.Pressed) held.Add(ev.Control); else held.Remove(ev.Control);
                continue;
            }

            string action = ev.Action;
            if (action == ControlBindings.WallLaserAction)
            {
                // The spy's D-pad down: the pistol's laser with the gun drawn, back to wall without.
                action = pawn?.WeaponLoaded == 2 ? "Action:weaponlaser" : "Action:wall";
            }

            Hold(ev.Control, action, ev.Pressed);
        }
    }

    private void ReleaseButtons()
    {
        _buttonsState.ReleaseAll();
        IReadOnlyList<PadBindingEvent> events = _actionState.ReleaseAll();
        for (int i = 0; i < events.Count; i++)
        {
            Hold(events[i].Control, events[i].Action, false);
        }

        _clutchOne.Clear();
        _clutchTwo.Clear();
    }

    /// <summary>
    /// The controller groups' look multipliers for the view the player is in (free or gun look, scope or binoculars,
    /// camera views), divided by the mouse's: the session's mouse look scales every count by the mouse groups, so
    /// the stick's counts end up with the controller groups' multipliers alone.
    /// </summary>
    private (float Horizontal, float Vertical) PadMultipliers()
    {
        if (_session.LocalController is not { } pc)
        {
            return (1f, 1f);
        }

        (float ph, float pv) = _padLook.Multipliers(pc);
        (float mh, float mv) = _session.Mouse.Multipliers(pc);
        float scale = Math.Max(1e-3f, _session.Mouse.Scale);
        return (ph / Math.Max(1e-3f, mh) / scale, pv / Math.Max(1e-3f, mv) / scale);
    }

    /// <summary>
    /// What the shaped stick is multiplied by besides the side's look multiplier: the look multiplier is divided by
    /// the curve's inner scale (so the knee turns at 1x and the rim at 2x, 2.5x or 3x); in a camera view the
    /// multiplier is <see cref="CameraStickMultiplier"/>, divided by the scale for the merc only.
    /// </summary>
    public static float CurveGain(string curve, bool cameraView, bool merc)
    {
        float scale = Curve(curve).Scale;
        return cameraView ? CameraStickMultiplier / (merc ? scale : 1f) : 1f / scale;
    }

    /// <summary>
    /// The cinematic free camera's pad controls (Enhanced 4.0): while it flies the triggers fly it up (right) and down
    /// (left) and the shoulder buttons step its speed (right faster, left slower, one step per press). True while it flies.
    /// </summary>
    private bool FreeCamera(ReadOnlySpan<byte> keys, float leftTrigger, float rightTrigger, float threshold)
    {
        if (_session.LocalController is not { FreeCameraFlying: not 0 })
        {
            _session.Input.PadUp = 0f;
            _lbWas = _rbWas = false;
            return false;
        }

        _session.Input.PadUp = rightTrigger - leftTrigger;
        bool lb = IsDown(keys, "LB", leftTrigger, rightTrigger, threshold);
        bool rb = IsDown(keys, "RB", leftTrigger, rightTrigger, threshold);
        if (rb && !_rbWas)
        {
            _session.Input.Wheel++;
        }

        if (lb && !_lbWas)
        {
            _session.Input.Wheel--;
        }

        _lbWas = lb;
        _rbWas = rb;
        return true;
    }

    /// <summary>
    /// A pad control is down: a trigger past the threshold, or a button down now or pressed since the last frame
    /// (a press shorter than a frame is down for that frame).
    /// </summary>
    private static bool IsDown(ReadOnlySpan<byte> keys, string control, float leftTrigger, float rightTrigger, float threshold) => control switch
    {
        "LT" => leftTrigger > threshold,
        "RT" => rightTrigger > threshold,
        _ => s_buttons.TryGetValue(control, out BridgeKey key) && (int)key < keys.Length && (keys[(int)key] & 3) != 0,
    };

    /// <summary>The radial deadzone with rescale (inputs inside the deadzone are 0, the rim stays 1).</summary>
    public static Vector2 Radial(Vector2 v, float deadzone)
    {
        float len = v.Length();
        if (len <= deadzone || len <= 0f)
        {
            return Vector2.Zero;
        }

        float scaled = Math.Min(1f, (len - deadzone) / Math.Max(1e-4f, 1f - deadzone));
        return v / len * scaled;
    }

    /// <summary>
    /// Reloaded's response curves of the look stick: the curve acts on the stick's length (after the deadzone), never
    /// on each axis, so a diagonal push turns as fast as a straight one. Each curve is a knee and an inner scale
    /// (Linear 1 and 1; Soft 0.75 and 0.5; Medium 0.75 and 0.4; Aggressive 0.75 and 1/3): up to the knee the length
    /// rises in a straight line to the scale, past it by the square of the way to the rim up to 1.
    /// </summary>
    public static Vector2 Shape(Vector2 stick, string curve)
    {
        float len = stick.Length();
        if (len <= 0f)
        {
            return Vector2.Zero;
        }

        (float knee, float scale) = Curve(curve);
        float a = Math.Min(1f, len);
        float shaped;
        if (a > knee)
        {
            float t = (a - knee) / (1f - knee);
            shaped = t * t * (1f - scale) + scale;
        }
        else
        {
            shaped = scale / knee * a;
        }

        return stick / len * shaped;
    }

    /// <summary>A response curve's knee and inner scale (Reloaded.Core table 0x103411b0).</summary>
    private static (float Knee, float Scale) Curve(string curve) => curve switch
    {
        "CurveSoft" => (0.75f, 0.5f),
        "CurveMedium" => (0.75f, 0.4f),
        "CurveAggressive" => (0.75f, 1f / 3f),
        _ => (1f, 1f),
    };

    /// <summary>
    /// Holds or releases what <paramref name="action"/> stands for, for one control: an action (<c>Action:jump</c>)
    /// as a virtual key, a key of the side's layout (<c>Key:F7</c>), or the first key the layout binds to a button
    /// or command name (<c>bCrouch</c>).
    /// </summary>
    private void Hold(string control, string action, bool down)
    {
        if (down)
        {
            if (_held.ContainsKey(control) || action.Length == 0)
            {
                return;
            }

            string? key = action.StartsWith(ControlActions.PadPrefix, StringComparison.Ordinal)
                ? ControlActions.VirtualPrefix + control + ":" + action[ControlActions.PadPrefix.Length..]
                : action.StartsWith(ControlBindings.KeyPrefix, StringComparison.OrdinalIgnoreCase)
                    ? action[ControlBindings.KeyPrefix.Length..]
                    : _keys.KeyFor(action);
            if (key is null)
            {
                return;
            }

            // The wheel has no hold: Key releases it itself, one notch per press.
            _keys.Key(key, true);
            _held[control] = key;
        }
        else if (_held.Remove(control, out string? key) && !BindingKey.IsPulseName(key))
        {
            _keys.Key(key, false);
        }
    }

    /// <summary>Lets go of the analog stick this mapper set.</summary>
    private void ClearStick()
    {
        _keys.SetPadWalk(null);
        if (_stickSet)
        {
            _stickSet = false;
            _session.Input.SetPadStick(0f, 0f);
        }
    }
}
