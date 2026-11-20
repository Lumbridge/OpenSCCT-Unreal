using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Animation;
using OpenScct.Core;
using OpenScct.Game.Camera;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
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

    /// <summary>Default aim and zoom response per second (PlayRoot.DrawViews).</summary>
    private const float AimBlendRate = 18f;

    /// <summary>How much of the field of view the merc's aim takes away at full blend (PlayCamera.Apply).</summary>
    private const float AimFovScale = 0.2f;
    private const float MercAimedSensitivity = 0.7f;

    private readonly PlaySession _session;
    private readonly LookAhead _look = new();
    private ControlSettings? _applied;
    private bool _merc;
    private bool _placed;
    private bool _wasCinematic;
    private float _aimBlend;
    private float _aimPhase;
    private SWeaponDefense? _presentedWeapon;
    private float _aimZoomBlend;
    private float _opticZoomExtra;
    private bool _mercLookHeldLastFrame;
    private SWeaponDefense? _shouldered;
    private SWeaponDefense? _menuSight;
    private bool _shoulderHeldLastFrame;
    private bool _ownsRifleAim;
    internal float RifleAimBlend => _aimBlend;

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

    /// <summary>
    /// The aim the last <c>GetView</c> handed Unreal (the controller's rotation plus the mouse turn not yet
    /// simulated), or null before the first. The local player's laser is aimed along it each frame
    /// (Equipment/EquipmentFeature.cs).
    /// </summary>
    public FRotator? DrawnAim { get; private set; }

    /// <summary>This frame's calibrated mercenary aim input, in degrees (yaw, pitch).</summary>
    public Vector2 MercAimTurn { get; private set; }
    public bool MercWeaponView { get; private set; }

    private (float X, float Y) PendingLook
    {
        get
        {
            var counts = Keys.PendingMouse;
            return (counts.X + _session.Input.MouseX, counts.Y + _session.Input.MouseY);
        }
    }

    /// <summary>One rendered frame of device state, before the session's update.</summary>
    public unsafe bool Frame(in BridgeRawInput raw)
    {
        if (!ReferenceEquals(_applied, ControlsStore.Settings))
        {
            _mercLookHeldLastFrame = false;
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
        SPlayerController? pc = _session.LocalController;
        bool localWeaponView = _merc && _session.ViewOverride is null && !_wasCinematic &&
            pc is { bBehindView: false, bBinocularVision: false, FreeCameraFlying: 0, SBPawn: SPAWNDEFENSE { Health: > 0 } } &&
            ReferenceEquals(pc.ViewTarget, pc.SBPawn) && PlayerCalcView.SourceOf(pc) == PlayerCalcView.EViewSource.Default;
        bool allowFreeLook = play && localWeaponView && (raw.Flags & BridgeRawInputFlags.AllowMercFreeLook) != 0;
        // UE batches all axis samples in this frame, including the ones collected before the key-up.
        bool reserveReleaseFrame = allowFreeLook && _mercLookHeldLastFrame && Keys.MercFreeLookHeld;
        bool freeLook;
        double delta = float.IsFinite(raw.DeltaSeconds) && raw.DeltaSeconds > 0f ? raw.DeltaSeconds : 0.0;
        var weapon = _session.LocalPawn?.MyWeapon as SWeaponDefense;
        bool menuView = (raw.Flags & BridgeRawInputFlags.MercMenuView) != 0;
        MercWeaponView = localWeaponView && weapon is not null;
        Keys.MercControls = play && MercWeaponView;
        var before = allowFreeLook ? PendingLook : default;
        fixed (byte* keys = raw.Keys)
        {
            var state = new ReadOnlySpan<byte>(keys, BridgeRawInput.KeyCount);
            // A value that is not a number (a device glitch) must not reach the simulation's rotation.
            // Resolve this frame's keyboard and controller presses before directing its look input.
            Keys.Frame(state, 0f, 0f, delta, play,
                new ReadOnlySpan<BridgeKeyEdge>(raw.Edges, raw.EdgeCount), raw.NowMsec);
            freeLook = Pad.Frame(state, new Vector2(Finite(raw.LeftX), Finite(raw.LeftY)), new Vector2(Finite(raw.RightX), Finite(raw.RightY)),
                Finite(raw.LeftTrigger), Finite(raw.RightTrigger), settings, side.Pad, _merc, play, delta, out var stick, allowFreeLook, reserveReleaseFrame);
            _mercLookHeldLastFrame = allowFreeLook && Keys.MercFreeLookHeld;
            UpdateShoulder(pc, weapon, (float)delta, menuView);
            if (play && !freeLook)
            {
                float aimSensitivity = _opticZoomExtra > 0f ? 1f / (1f + _opticZoomExtra) :
                    Keys.MercControls ? 1f + (MercAimedSensitivity - 1f) * _aimBlend : 1f;
                Keys.AddMouse(Finite(raw.MouseX) * aimSensitivity, Finite(raw.MouseY) * aimSensitivity);
                _session.Input.MouseX += stick.X * aimSensitivity;
                _session.Input.MouseY += stick.Y * aimSensitivity;
            }
        }

        UpdateClutch(settings);
        MercAimTurn = Vector2.Zero;
        if (allowFreeLook && !freeLook && pc is not null)
        {
            var after = PendingLook;
            if (after != before)
            {
                // Both projections start from the same simulation pose. Subtracting also accounts for earlier
                // render frames within this tick at the pitch stop and in the integer rotation rounding.
                float dt = pc.Level?.m_dT ?? Sim.DeltaTime;
                var oldTurn = LookAhead.Turn(pc, _session.Mouse, before.X, before.Y, dt);
                var newTurn = LookAhead.Turn(pc, _session.Mouse, after.X, after.Y, dt);
                MercAimTurn = new Vector2(Degrees(newTurn.Yaw - oldTurn.Yaw), Degrees(newTurn.Pitch - oldTurn.Pitch));
            }
        }
        return freeLook;
    }

    private void UpdateShoulder(SPlayerController? pc, SWeaponDefense? weapon, float delta, bool menuView)
    {
        if (!ReferenceEquals(weapon, _presentedWeapon))
        {
            _presentedWeapon = weapon;
            _aimBlend = _aimPhase = _aimZoomBlend = _opticZoomExtra = 0f;
        }
        bool held = Keys.MercAimHeld;
        bool canShowRifle = MercWeaponView && pc is { bIsInInventoryMode: false, iDontUpdatePadInput: 0,
            SBPawn: SPAWNDEFENSE { Health: > 0, bStickyShocking: false, bWeaponTransit: false } };
        bool ready = canShowRifle && (weapon is MercSidearm sidearm
            ? sidearm.ReloadUntil <= 0f && sidearm.IsInState("s_Selected") &&
                pc?.SBPawn is { bLyingOnGround: false } pawn && pawn.Level.TimeSeconds >= pawn.WeaponSwitchUntil
            : weapon?.StateNode is SWeaponDefense.s_Selected or SWeaponDefense.s_Firing);
        // The match keeps running behind the menu. Release the gameplay aim input below, but
        // keep the current sight picture until play resumes or this rifle can no longer be held.
        _menuSight = menuView && ready && ReferenceEquals(weapon, _menuSight ?? _shouldered) ? weapon : null;
        if (_shouldered is not null && (!held || !ready || !ReferenceEquals(weapon, _shouldered)))
        {
            if (_ownsRifleAim && _shouldered is { AimRequested: true } owned && pc is not null)
                owned.SetAiming(pc, false);
            // Reload cancels aim immediately. The same visible rifle still has to travel out of its sights.
            bool reloading = weapon is MercSidearm { ReloadUntil: > 0f }
                || weapon?.StateNode is SWeaponDefense.s_Reloading or SWeaponDefense.s_FullReloading;
            if (!ReferenceEquals(weapon, _shouldered) || !canShowRifle || (!ready && !reloading)) _aimBlend = _aimPhase = 0f;
            _shouldered = null;
            _ownsRifleAim = false;
        }
        if (ready && held && !_shoulderHeldLastFrame)
        {
            _shouldered = weapon;
            if (weapon is { AimRequested: false } rifle)
            {
                // The authority records which physical sight was raised when the shot landed.
                rifle.SetAiming(pc!, true);
                _ownsRifleAim = rifle.AimRequested;
            }
        }
        _shoulderHeldLastFrame = held;

        if (!MercWeaponView)
        {
            _aimBlend = _aimPhase = _aimZoomBlend = _opticZoomExtra = 0f;
            return;
        }

        bool aiming = weapon is MercRifle or MercSidearm ? weapon.AimRequested : _shouldered is not null;
        float step = 1f - MathF.Exp(-AimBlendRate * delta);
        float power = WeaponAttachments.OpticPower(weapon);
        if (_menuSight is null)
        {
            // Advance the raise/lower animation from its current position, including when the player
            // reverses direction before reaching the sight.
            float speed = weapon switch
            {
                M4A1S => 2.3f * 1.35f,
                BoltRifle => 4f,
                CompactSMG or MercSidearm or SWeaponDefense_Uzi => 3f,
                _ => 2.3f
            };
            _aimPhase = Math.Clamp(_aimPhase + (aiming ? speed : -speed) * delta, 0f, 1f);
            _aimBlend = weapon is BoltRifle ? _aimPhase : .5f * (1f - MathF.Cos(MathF.PI * _aimPhase));
            _opticZoomExtra = (power - 1f) * _aimBlend;
        }
        // The shoulder binding raises the sight in the existing image. The independent scope binding
        // retains its original zoom and eases back out when released.
        if (_shouldered is not null || power > 1f) _aimZoomBlend = 0f;
        else _aimZoomBlend += ((weapon is MercRifle { AimRequested: true } ? 1f : 0f) - _aimZoomBlend) * step;
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
        DrawnAim = null;
        if (!current.Valid)
        {
            return default;
        }

        ViewSample previous = _session.PreviousView;
        SPlayerController? pc = _session.LocalController;
        float alpha = _session.Alpha;
        bool overridden = _session.ViewOverride is not null;

        // The mouse turn not yet simulated is drawn now (no frame of delay, as the original's per-frame controller).
        (float pendingX, float pendingY) = PendingLook;
        FRotator look = overridden
            ? default
            : _look.Offset(pc, _session.Mouse, pendingX, pendingY, alpha);
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
            camera.Fov *= 1f - AimFovScale * _aimZoomBlend;
        }

        _wasCinematic = cinematic;
        _placed = true;

        // The uncapped views are the two camera states, by name (Reloaded's GotoState hook).
        ControlSettings settings = ControlsStore.Settings;
        Name state = pc?.GetStateName() ?? default;
        bool capped = pc is null || (state != NAME_StickyCam && state != NAME_CamNetwork);
        (float horizontal, float vertical) = ViewFov.Screen(camera.Fov, aspect > 0f ? aspect : ViewFov.WidestAspect, settings, capped);
        float scopeLensPower = 0f;
        if (!cinematic && !overridden && pc is { bBinocularVision: false, FreeCameraFlying: 0 } &&
            pc.SBPawn is SPAWNDEFENSE { Health: > 0, bWeaponTransit: false } pawn && ReferenceEquals(pc.ViewTarget, pawn) &&
            PlayerCalcView.SourceOf(pc) == PlayerCalcView.EViewSource.Default && pawn.MyWeapon is SWeaponDefense weapon)
        {
            // The lens has a fixed focal length. Eye alignment controls how much of its image can be seen.
            float power = WeaponAttachments.OpticPower(weapon);
            if (power > 1f) scopeLensPower = power;
        }

        FRotator pending = !overridden && _look.Active ? _look.Pending : default;
        FRotator aim = pc is null ? camera.Rotation : new FRotator(pc.Rotation.Pitch + pending.Pitch, pc.Rotation.Yaw + pending.Yaw, pc.Rotation.Roll);
        DrawnCamera = (camera.Location, camera.Rotation);
        DrawnAim = aim;
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
            OpticMagnification = 1f,
            ScopeLensPower = scopeLensPower,
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
