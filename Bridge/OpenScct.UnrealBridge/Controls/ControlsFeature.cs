using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Animation;
using OpenScct.Core;
using OpenScct.Game.Camera;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// The bindings and settings in use. They outlive a session (the menus edit them with no match running), so they are
/// kept here; a session's <see cref="ControlsFeature"/> picks changes up on its next frame.
/// </summary>
public static class ControlsStore
{
    private static ControlSettings s_settings = new();
    private static ControlBindings s_bindings = new();

    /// <summary>The settings. Set a changed copy (the object is not edited in place), so readers see the change.</summary>
    public static ControlSettings Settings
    {
        get => s_settings;
        set
        {
            ArgumentNullException.ThrowIfNull(value);
            value.Clamp();
            bool wheelChanged = value.MouseWheel != s_settings.MouseWheel;
            s_settings = value;
            if (wheelChanged)
            {
                RebindWheel(s_bindings.Spy.Keys, value.MouseWheel);
                RebindWheel(s_bindings.Merc.Keys, value.MouseWheel);
            }
        }
    }

    /// <summary>
    /// The wheel's job is written into the layouts: a wheel direction still on the other mode's command takes the
    /// new mode's (next and previous gadget, or run and walk). A direction the player bound to something else stays.
    /// </summary>
    private static void RebindWheel(KeyLayout layout, string mode)
    {
        bool speed = mode == "Speed";
        Swap("MouseWheelUp", speed ? "NextGadget" : "Run", speed ? "Run" : "NextGadget");
        Swap("MouseWheelDown", speed ? "PreviousGadget" : "Walk", speed ? "Walk" : "PreviousGadget");
        layout.UpgradeWheelBindings(mode);

        void Swap(string key, string from, string to)
        {
            if (layout.Bindings.TryGetValue(key, out string? command) && string.Equals(command, from, StringComparison.OrdinalIgnoreCase))
            {
                layout.Bind(key, to);
            }
        }
    }

    public static ControlBindings Bindings
    {
        get => s_bindings;
        set => s_bindings = value ?? throw new ArgumentNullException(nameof(value));
    }

    /// <summary>
    /// The bindings as the file has them (read or written last), as the JSON a save writes; null before either. The
    /// bindings editor compares with it and goes back to it when the player leaves without saving.
    /// </summary>
    public static string? SavedBindingsJson { get; set; }
}

/// <summary>
/// The controls of one play session: Unreal hands over the devices' state every frame (<see cref="Frame"/>), the
/// input mapper turns it into the simulation's input before each tick, and the drawn view is given back with the
/// mouse turn the simulation has not taken yet (<see cref="GetView"/>). The wiring is the Godot client's
/// (game/Scripts/Play/PlayRoot.cs BeforeTick, AfterTick and DrawViews; game/Scripts/Cameras/PlayCamera.cs Apply;
/// game/Scripts/Options/OptionsRuntime.cs for the settings and the clutch).
/// </summary>
public sealed class ControlsFeature
{
    private static readonly Name NAME_StickyCam = new("s_UseObject_StickyCam");
    private static readonly Name NAME_CamNetwork = new("s_UseObject_CamNetwork");

    /// <summary>How fast the merc's aim zoom eases in and out (PlayRoot.DrawViews).</summary>
    private const float AimBlendRate = 18f;

    /// <summary>How much of the field of view the merc's aim takes away at full blend (PlayCamera.Apply).</summary>
    private const float AimFovScale = 0.2f;

    private readonly PlaySession _session;
    private readonly LookAhead _look = new();
    private ControlSettings? _applied;
    private bool _merc;
    private bool _placed;
    private bool _wasCinematic;
    private float _aimBlend;

    private ControlsFeature(BridgeSession bridge)
    {
        _session = bridge.Session;
        _merc = _session.Settings.Side == PlaySide.Merc;
        Keys = new InputMapper(_session.Input, ControlsStore.Bindings.KeysOf(_merc), _merc, ControlsStore.Settings);
        Pad = new PadMapper(Keys, _session);
        Keys.AutoHackToggled += on =>
        {
            ControlSettings changed = ControlSettings.FromJson(ControlsStore.Settings.ToJson());
            changed.AutoHackComms = on;
            ControlsStore.Settings = changed;
            ApplySettings();
        };
        ApplySettings();
        bridge.BeforeTick += OnBeforeTick;
        bridge.Ticked += OnTicked;
    }

    public static ControlsFeature Of(BridgeSession bridge) => bridge.Feature(static b => new ControlsFeature(b));

    public InputMapper Keys { get; }

    public PadMapper Pad { get; }

    /// <summary>The side whose bindings play: the local pawn's, or the side asked for while there is none.</summary>
    public bool Merc => _merc;

    /// <summary>True while the look-ahead draws the mouse turn ahead of the simulation.</summary>
    public bool LookAheadActive => _look.Active;

    /// <summary>
    /// The camera the last <c>GetView</c> handed Unreal (the eye of the frame, the look-ahead and the wall constraint
    /// in it), or null before the first. The spy's eye view traces from it to its sockets (Equipment/FirstPersonModels.cs).
    /// </summary>
    public (FVector Location, FRotator Rotation)? DrawnCamera { get; private set; }

    /// <summary>One rendered frame of device state, before the session's update.</summary>
    public unsafe void Frame(in BridgeRawInput raw)
    {
        if (!ReferenceEquals(_applied, ControlsStore.Settings))
        {
            ApplySettings();
        }

        if (_session.LocalPawn is { } pawn)
        {
            _merc = pawn is SPAWNDEFENSE;
        }

        ControlSettings settings = ControlsStore.Settings;
        SideBindings side = ControlsStore.Bindings.Of(_merc);
        // The bindings editor's Apply replaces the store's bindings and a key bound from the console changes a layout
        // in place: either way the mapper sees it here, so a rebind plays from this frame.
        Keys.SetLayout(ControlsStore.Bindings.KeysOf(_merc), _merc);
        bool play = (raw.Flags & BridgeRawInputFlags.Play) != 0;
        double delta = float.IsFinite(raw.DeltaSeconds) && raw.DeltaSeconds > 0f ? raw.DeltaSeconds : 0.0;
        fixed (byte* keys = raw.Keys)
        {
            var state = new ReadOnlySpan<byte>(keys, BridgeRawInput.KeyCount);
            // A value that is not a number (a device glitch) must not reach the simulation's rotation.
            Keys.Frame(state, Finite(raw.MouseX), Finite(raw.MouseY), delta, play);
            Pad.Frame(state, new Vector2(Finite(raw.LeftX), Finite(raw.LeftY)), new Vector2(Finite(raw.RightX), Finite(raw.RightY)),
                Finite(raw.LeftTrigger), Finite(raw.RightTrigger), settings, side.Pad, _merc, play, delta);
        }

        UpdateClutch(settings);

        // The merc's aim zoom eases by the frame, as the Godot client's first-person view does.
        float target = _session.LocalPawn?.MyWeapon is OpenScct.Game.Svm.MercRifle { AimRequested: true } ? 1f : 0f;
        _aimBlend += (target - _aimBlend) * (1f - MathF.Exp(-AimBlendRate * (float)delta));
    }

    /// <summary>
    /// The camera to draw this frame (PlayCamera.Apply): the view between the last two ticks, plus the mouse turn not
    /// yet simulated; a jump (a respawn, a change of view source) is a cut. While a map's cinematic sequence has a
    /// camera, that camera is the view (a cut on its first frame and on the first frame after it).
    /// </summary>
    /// <param name="aspect">The picture's width over its height, for the screen's field of view; 0 for 16:9.</param>
    /// <param name="clearance">
    /// The radius of the sphere round the camera that holds its near plane, in units, for keeping the spy's
    /// first-person eye out of walls; 0 for the simulation's default.
    /// </param>
    public BridgeLookView GetView(float aspect, float clearance)
    {
        ViewSample current = _session.View;
        DrawnCamera = null;
        if (!current.Valid)
        {
            return default;
        }

        ViewSample previous = _session.PreviousView;
        SPlayerController? pc = _session.LocalController;
        float alpha = _session.Alpha;
        bool overridden = _session.ViewOverride is not null;

        // The mouse turn not yet simulated is drawn now (no frame of delay, as the original's per-frame controller).
        (float pendingX, float pendingY) = Keys.PendingMouse;
        FRotator look = overridden
            ? default
            : _look.Offset(pc, _session.Mouse, pendingX + _session.Input.MouseX, pendingY + _session.Input.MouseY, alpha);
        float orbit = _look.OrbitDistance(pc);

        bool cut = !_placed || _wasCinematic || ViewBlend.IsCut(previous, current);
        (FVector Location, FRotator Rotation, float Fov) camera = cut
            ? (current.Location, current.Rotation, current.Fov)
            : ViewBlend.Blend(previous, current, alpha);
        if (!cut)
        {
            // A cut draws the last tick as it is (the look-ahead's rest of a tick is already in it).
            FRotator turned = new(camera.Rotation.Pitch + look.Pitch, camera.Rotation.Yaw + look.Yaw, camera.Rotation.Roll + look.Roll);

            // A following view turned ahead moves round its target, the orbit distance in front of it.
            if (orbit > 0f && (look.Pitch != 0 || look.Yaw != 0))
            {
                camera.Location += (camera.Rotation.Vector() - turned.Vector()) * orbit;
            }

            camera.Rotation = turned;
        }

        // A playing sequence with a camera track takes the view (PlayCamera.Apply): its transform is in the
        // sequence editor's space (metres, Y up), and it brings its own field of view when the track has one.
        bool cinematic = false;
        if (!overridden && pc?.XLevel.GetService<AnimationWorld>() is not null)
        {
            foreach (AnimationController sequence in pc.XLevel.AllActors<AnimationController>())
            {
                if (sequence.Camera() is { } shot)
                {
                    camera = (StudioSpace.ToUe(shot.Transform.Position), StudioSpace.ToUe(shot.Transform.Rotation),
                        shot.FieldOfView ?? camera.Fov);
                    cinematic = true;
                    cut = true;
                    break;
                }
            }
        }

        // Recheck the final location: interpolation and reconciliation can cross a corner even when both simulation
        // samples were clear. Also protect the return to third person, including its pending mouse orbit.
        bool occluded = false;
        if (!cinematic && !overridden && (current.SpyFirstPerson || previous.SpyFirstPerson) && pc?.Pawn is SPawnAttaque spy
            && ReferenceEquals(pc.ViewTarget, spy)
            && PlayerCalcView.SourceOf(pc) == PlayerCalcView.EViewSource.Default
            && pc.FreeCameraFlying == 0)
        {
            occluded = !SpyPerspectiveCamera.TryConstrain(spy, camera.Location, out camera.Location,
                clearance > 0f ? MathF.Max(SpyPerspectiveCamera.DefaultClearance, clearance) : SpyPerspectiveCamera.DefaultClearance,
                withinBody: current.SpyFirstPerson);
        }

        if (!cinematic)
        {
            camera.Fov *= 1f - AimFovScale * _aimBlend;
        }

        _wasCinematic = cinematic;
        _placed = true;

        // The uncapped views are the two camera states, by name (Reloaded's GotoState hook).
        ControlSettings settings = ControlsStore.Settings;
        Name state = pc?.GetStateName() ?? default;
        bool capped = pc is null || (state != NAME_StickyCam && state != NAME_CamNetwork);
        (float horizontal, float vertical) = ViewFov.Screen(camera.Fov, aspect > 0f ? aspect : ViewFov.WidestAspect, settings, capped);

        FRotator pending = !overridden && _look.Active ? _look.Pending : default;
        FRotator aim = pc is null ? camera.Rotation : new FRotator(pc.Rotation.Pitch + pending.Pitch, pc.Rotation.Yaw + pending.Yaw, pc.Rotation.Roll);
        DrawnCamera = (camera.Location, camera.Rotation);
        return new BridgeLookView
        {
            X = camera.Location.X,
            Y = camera.Location.Y,
            Z = camera.Location.Z,
            Pitch = Degrees(camera.Rotation.Pitch),
            Yaw = Degrees(camera.Rotation.Yaw),
            Roll = Degrees(camera.Rotation.Roll),
            Fov = camera.Fov,
            HorizontalFov = horizontal,
            VerticalFov = vertical,
            AimPitch = Degrees(aim.Pitch),
            AimYaw = Degrees(aim.Yaw),
            Valid = 1,
            SpyFirstPerson = current.SpyFirstPerson ? 1 : 0,
            Cut = cut ? 1 : 0,
            Occluded = occluded ? 1 : 0,
            LookAhead = _look.Active ? 1 : 0,
            RifleAimBlend = _aimBlend,
        };
    }

    /// <summary>Puts the settings on the session (OptionsRuntime.Apply).</summary>
    private void ApplySettings()
    {
        ControlSettings s = ControlsStore.Settings;
        _applied = s;
        Keys.Settings = s;
        _session.Mouse = s.MouseLookOf(_session.Mouse);
        _session.SpyRollFreeLook = s.SpyRollFreeLook;
        _session.SpyCloakHold = s.SpyKbm.CloakBehavior == "Hold";
        _session.AutoHackComms = s.AutoHackComms;
    }

    /// <summary>
    /// The keyboard and mouse clutches (OptionsRuntime.UpdateClutch): while the key of one is held, the session's
    /// mouse look takes the side's clutch multiplier (the merc has two; the spy only the first).
    /// </summary>
    private void UpdateClutch(ControlSettings settings)
    {
        bool one = settings.ClutchOneKey.Length > 0 && Keys.PhysicalHeld(settings.ClutchOneKey);
        bool two = _merc && settings.ClutchTwoKey.Length > 0 && Keys.PhysicalHeld(settings.ClutchTwoKey);
        float clutch = settings.LookOf(_merc, pad: false).ClutchOf(one, two);
        if (_session.Mouse.Clutch != clutch)
        {
            _session.Mouse = _session.Mouse with { Clutch = clutch };
        }
    }

    private void OnBeforeTick()
    {
        Keys.FillTick(_session.Input, _session.LocalController);

        // The mouse counts this tick takes, for the drawn view's look-ahead.
        _look.BeforeTick(_session.LocalController, _session.Mouse, _session.Input.MouseX, _session.Input.MouseY);
    }

    private void OnTicked() => _look.AfterTick(_session.LocalController, _session.View);

    private static float Finite(float value) => float.IsFinite(value) ? value : 0f;

    /// <summary>Rotation units (65536 to a turn) as degrees, the short way round.</summary>
    private static float Degrees(int units) => FRotator.NormalizeAxis(units) * (360f / 65536f);
}
