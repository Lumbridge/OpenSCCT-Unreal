using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Controls;

// The control settings of OpenSCCT's game/Scripts/Options/GameSettings.cs (LookSettings, PadSettings and the
// mouse, wheel and widescreen keys of GameSettings), with the same JSON names and the same defaults, which are
// Reloaded's (the original's patch: its sensitivity groups, controller deadzones and widescreen FOV).

/// <summary>Look sensitivities and hold-or-toggle choices of one side and device, as Reloaded's control groups hold them.</summary>
public sealed class LookSettings
{
    /// <summary>"Toggle" or "Hold": the walk key.</summary>
    [JsonPropertyName("walk_behavior")]
    public string WalkBehavior { get; set; } = "Toggle";

    /// <summary>"Toggle" or "Hold": the merc's sniper scope key.</summary>
    [JsonPropertyName("scope_behavior")]
    public string ScopeBehavior { get; set; } = "Toggle";

    /// <summary>"Toggle" or "Hold": the spy's first-person key. The keyboard group's choice also applies to the pad.</summary>
    [JsonPropertyName("perspective_behavior")]
    public string PerspectiveBehavior { get; set; } = "Toggle";

    /// <summary>"Toggle" or "Hold": the spy's cloak. The spy keyboard group's choice is the one used, for every device.</summary>
    [JsonPropertyName("cloak_behavior")]
    public string CloakBehavior { get; set; } = "Toggle";

    /// <summary>Spy: free look with the gun holstered (merc: unused).</summary>
    [JsonPropertyName("free_look")]
    public float FreeLook { get; set; } = 1.4f;

    /// <summary>Spy: look with the gun drawn. Merc: the look.</summary>
    [JsonPropertyName("look")]
    public float Look { get; set; } = 1f;

    /// <summary>Binoculars (spy) or sniper scope (merc), times the look.</summary>
    [JsonPropertyName("scope")]
    public float Scope { get; set; } = 0.75f;

    /// <summary>Merc: the sniper scope's vertical scale (2.45 restores 1:1 from the stock 40.8 %).</summary>
    [JsonPropertyName("scope_vertical")]
    public float ScopeVertical { get; set; } = 2.45f;

    /// <summary>Kept for the settings file; neither Reloaded's mouse nor its pad code reads it for the look.</summary>
    [JsonPropertyName("vertical")]
    public float Vertical { get; set; } = 1f;

    /// <summary>Sticky camera and camera network.</summary>
    [JsonPropertyName("cam")]
    public float Cam { get; set; } = 1f;

    /// <summary>Linear, CurveSoft, CurveMedium or CurveAggressive.</summary>
    [JsonPropertyName("look_response")]
    public string LookResponse { get; set; } = "Linear";

    /// <summary>The spy controller's response with its weapon drawn, independent of free look.</summary>
    [JsonPropertyName("gun_look_response")]
    public string GunLookResponse { get; set; } = "Linear";

    /// <summary>The first sensitivity clutch: a held button multiplies the look by this.</summary>
    [JsonPropertyName("clutch_one")]
    public float ClutchOne { get; set; }

    [JsonPropertyName("clutch_one_enabled")]
    public bool ClutchOneEnabled { get; set; }

    /// <summary>The second clutch (Reloaded gives it to the merc only).</summary>
    [JsonPropertyName("clutch_two")]
    public float ClutchTwo { get; set; }

    [JsonPropertyName("clutch_two_enabled")]
    public bool ClutchTwoEnabled { get; set; }

    /// <summary>Controller groups: the left stick click and the left bumper trade places.</summary>
    [JsonPropertyName("swap_ls_lb")]
    public bool SwapLsLb { get; set; }

    /// <summary>Controller groups: the right stick click and the right bumper trade places.</summary>
    [JsonPropertyName("swap_rs_rb")]
    public bool SwapRsRb { get; set; }

    /// <summary>The pad control that is read for <paramref name="control"/> with the swaps applied.</summary>
    public string PhysicalControl(string control) => control switch
    {
        "LB" when SwapLsLb => "LS",
        "LS" when SwapLsLb => "LB",
        "RB" when SwapRsRb => "RS",
        "RS" when SwapRsRb => "RB",
        _ => control,
    };

    /// <summary>The multiplier of the clutches held (1 with none): each enabled clutch with a value above 0 counts.</summary>
    public float ClutchOf(bool one, bool two) =>
        (one && ClutchOneEnabled && ClutchOne > 0f ? ClutchOne : 1f) * (two && ClutchTwoEnabled && ClutchTwo > 0f ? ClutchTwo : 1f);

    internal void Clamp()
    {
        WalkBehavior = WalkBehavior == "Hold" ? "Hold" : "Toggle";
        ScopeBehavior = ScopeBehavior == "Hold" ? "Hold" : "Toggle";
        PerspectiveBehavior = PerspectiveBehavior == "Hold" ? "Hold" : "Toggle";
        CloakBehavior = CloakBehavior == "Hold" ? "Hold" : "Toggle";
        ClutchOne = Range(ClutchOne, 0f, 10f);
        ClutchTwo = Range(ClutchTwo, 0f, 10f);
        FreeLook = Range(FreeLook, 0.05f, 10f);
        Look = Range(Look, 0.05f, 10f);
        Scope = Range(Scope, 0.05f, 10f);
        ScopeVertical = Range(ScopeVertical, 0.05f, 10f);
        Vertical = Range(Vertical, 0.05f, 10f);
        Cam = Range(Cam, 0.05f, 10f);
        if (Array.IndexOf(ControlSettings.Responses, LookResponse) < 0)
        {
            LookResponse = "Linear";
        }

        if (Array.IndexOf(ControlSettings.Responses, GunLookResponse) < 0)
        {
            GunLookResponse = LookResponse;
        }
    }

    internal static float Range(float v, float min, float max) => float.IsFinite(v) ? Math.Clamp(v, min, max) : min;
}

/// <summary>The controller's tuning (Reloaded's controller_* keys). Its buttons are in <see cref="ControlBindings"/>.</summary>
public sealed class PadTuning
{
    /// <summary>
    /// The controller plays. Reloaded starts with it off and asks on the first run; this client has no such prompt
    /// yet, so it starts on (the deadzones keep an idle controller quiet).
    /// </summary>
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = true;

    /// <summary>The left stick moves.</summary>
    [JsonPropertyName("move")]
    public bool Move { get; set; } = true;

    /// <summary>The right stick looks.</summary>
    [JsonPropertyName("aim")]
    public bool Aim { get; set; } = true;

    /// <summary>The buttons act.</summary>
    [JsonPropertyName("binds")]
    public bool Binds { get; set; } = true;

    /// <summary>controller_left_thumb_deadzone, 0 to 1 of the stick's travel (radial).</summary>
    [JsonPropertyName("deadzone_left")]
    public float DeadzoneLeft { get; set; } = 0.25f;

    /// <summary>controller_right_thumb_deadzone.</summary>
    [JsonPropertyName("deadzone_right")]
    public float DeadzoneRight { get; set; } = 0.15f;

    /// <summary>controller_right_thumb_vertical_sensitivty: the look stick's up and down against its sideways speed.</summary>
    [JsonPropertyName("vertical_scale")]
    public float VerticalScale { get; set; } = 0.75f;

    /// <summary>controller_walk_threshold: the spy runs with the move stick pushed at least this far.</summary>
    [JsonPropertyName("run_threshold")]
    public float RunThreshold { get; set; } = 0.95f;

    /// <summary>How far a trigger goes down (0 to 1) before it counts as pressed: Reloaded's 7680 of 32767.</summary>
    [JsonPropertyName("trigger_threshold")]
    public float TriggerThreshold { get; set; } = 7680f / 32767f;

    /// <summary>A part push of the left stick walks slower (Reloaded's analog slow walk); off, only its direction counts.</summary>
    [JsonPropertyName("slow_walk")]
    public bool SlowWalk { get; set; } = true;

    /// <summary>Snap spy movement to eight directions.</summary>
    [JsonPropertyName("spy_move8")]
    public bool SpyMove8 { get; set; }

    [JsonPropertyName("merc_move8")]
    public bool MercMove8 { get; set; }

    /// <summary>A multiplier over the look stick's whole speed: 1 is the original's rate.</summary>
    [JsonPropertyName("look_scale")]
    public float LookScale { get; set; } = 1f;

    /// <summary>Controller vibration (Reloaded's controller_rumble_enabled, off on a first run). Not in the controls struct: PadExports.</summary>
    [JsonPropertyName("rumble")]
    public bool Rumble { get; set; }

    /// <summary>The vibration's strength, 0 to 1 (controller_rumble_strength).</summary>
    [JsonPropertyName("rumble_strength")]
    public float RumbleStrength { get; set; } = 1f;

    internal void Clamp()
    {
        RumbleStrength = LookSettings.Range(RumbleStrength, 0f, 1f);
        DeadzoneLeft = LookSettings.Range(DeadzoneLeft, 0f, 0.9f);
        DeadzoneRight = LookSettings.Range(DeadzoneRight, 0f, 0.9f);
        VerticalScale = LookSettings.Range(VerticalScale, 0.1f, 3f);
        RunThreshold = LookSettings.Range(RunThreshold, 0.1f, 1f);
        TriggerThreshold = LookSettings.Range(TriggerThreshold, 0f, 0.99f);
        LookScale = LookSettings.Range(LookScale, 0.1f, 10f);
    }
}

/// <summary>
/// The player's control settings: sensitivities per side and device, inversions, the wheel's job, the controller's
/// tuning and the widescreen field of view. Saved as JSON where the caller says.
/// </summary>
public sealed class ControlSettings
{
    /// <summary>The look stick's and the spy mouse free look's response curves, in the order of the settings' indices.</summary>
    public static readonly string[] Responses = ["Linear", "CurveSoft", "CurveMedium", "CurveAggressive"];

    public static readonly string[] WheelModes = ["Gadget", "Speed"];

    public const float MinMercFov = 95f;
    public const float MaxMercFov = 112f;

    /// <summary>Up and down look the other way round.</summary>
    [JsonPropertyName("invert_y")]
    public bool InvertY { get; set; }

    /// <summary>The spy's free look turns the other way left and right (the profile's InvertX).</summary>
    [JsonPropertyName("invert_x_spy")]
    public bool InvertXSpy { get; set; }

    /// <summary>
    /// A multiplier over every mouse count, 1 for the original's turn per count (3 x the look multiplier in rotation
    /// units, 65536 to a turn). Not in the original; here for a device whose counts differ.
    /// </summary>
    [JsonPropertyName("mouse_scale")]
    public float MouseScale { get; set; } = 1f;

    /// <summary>controls_spy_config_kbm: free_look 1.4, gun look 1.0, scope 0.75, vertical 3.5, cam 1.0.</summary>
    [JsonPropertyName("controls_spy_kbm")]
    public LookSettings SpyKbm { get; set; } = new() { FreeLook = 1.4f, Look = 1f, Scope = 0.75f, Vertical = 3.5f, Cam = 1f };

    /// <summary>controls_merc_config_kbm: look 1.0, scope 0.75, scope_vertical 2.45, vertical 1.0, cam 1.0.</summary>
    [JsonPropertyName("controls_merc_kbm")]
    public LookSettings MercKbm { get; set; } = new() { FreeLook = 1f, Look = 1f, Scope = 0.75f, ScopeVertical = 2.45f, Vertical = 1f, Cam = 1f };

    /// <summary>controls_spy_config_controller: free_look 1.6, gun look 1.2, scope 0.75, cam 1.8, clutch 2.0.</summary>
    [JsonPropertyName("controls_spy_pad")]
    public LookSettings SpyPad { get; set; } = new()
    {
        FreeLook = 1.6f, Look = 1.2f, Scope = 0.75f, Vertical = 3.5f, Cam = 1.8f,
        LookResponse = "CurveAggressive", GunLookResponse = "CurveSoft", ClutchOne = 2f, ClutchOneEnabled = true,
    };

    /// <summary>controls_merc_config_controller: look 1.8, scope 0.75, scope_vertical 2.45, cam 1.0, clutches 0.5 and 2.0.</summary>
    [JsonPropertyName("controls_merc_pad")]
    public LookSettings MercPad { get; set; } = new()
    {
        FreeLook = 1f, Look = 1.8f, Scope = 0.75f, ScopeVertical = 2.45f, Vertical = 1f, Cam = 1f,
        LookResponse = "CurveMedium", ClutchOne = 0.5f, ClutchOneEnabled = true, ClutchTwo = 2f, ClutchTwoEnabled = true,
    };

    [JsonPropertyName("controller")]
    public PadTuning Pad { get; set; } = new();

    /// <summary>The key or mouse button (binding name: "Alt", "MiddleMouse") held for the first sensitivity clutch; empty for none.</summary>
    [JsonPropertyName("clutch_one_key")]
    public string ClutchOneKey { get; set; } = "";

    /// <summary>The key held for the second clutch (the merc's).</summary>
    [JsonPropertyName("clutch_two_key")]
    public string ClutchTwoKey { get; set; } = "";

    /// <summary>The spy's camera may turn during a roll while the roll keeps its starting direction.</summary>
    [JsonPropertyName("spy_roll_free_look")]
    public bool SpyRollFreeLook { get; set; } = true;

    /// <summary>The mouse wheel: "Gadget" (changes gadgets) or "Speed" (walk and run).</summary>
    [JsonPropertyName("mouse_wheel")]
    public string MouseWheel { get; set; } = "Gadget";

    /// <summary>Enemy communications are hacked whenever they can be, without holding the hack key (ToggleAutoHack flips it).</summary>
    [JsonPropertyName("auto_hack_comms")]
    public bool AutoHackComms { get; set; }

    /// <summary>Reloaded's widescreen: the 4:3 vertical view is kept and wider screens see more; off stretches the 4:3 picture.</summary>
    [JsonPropertyName("widescreen")]
    public bool Widescreen { get; set; } = true;

    /// <summary>ws_fov: the cap on the horizontal field of view in degrees (every view but the sticky camera and the camera network).</summary>
    [JsonPropertyName("ws_fov")]
    public float MercFovCap { get; set; } = 112f;

    public LookSettings LookOf(bool merc, bool pad) => merc ? (pad ? MercPad : MercKbm) : (pad ? SpyPad : SpyKbm);

    public void Clamp()
    {
        SpyKbm ??= new LookSettings();
        MercKbm ??= new LookSettings();
        SpyPad ??= new LookSettings();
        MercPad ??= new LookSettings();
        Pad ??= new PadTuning();
        SpyKbm.Clamp();
        MercKbm.Clamp();
        SpyPad.Clamp();
        MercPad.Clamp();
        Pad.Clamp();
        MouseScale = LookSettings.Range(MouseScale, 0.01f, 100f);
        ClutchOneKey = (ClutchOneKey ?? "").Trim();
        ClutchTwoKey = (ClutchTwoKey ?? "").Trim();
        MouseWheel = Array.IndexOf(WheelModes, MouseWheel) < 0 ? "Gadget" : MouseWheel;
        MercFovCap = LookSettings.Range(MercFovCap, MinMercFov, MaxMercFov);
    }

    /// <summary>
    /// The session's mouse look for these settings (OpenSCCT SettingsApplier.MouseLookOf): Reloaded's look
    /// multipliers and the inversions. The clutch of <paramref name="current"/> is kept.
    /// </summary>
    /// <param name="pad">The controller groups' multipliers instead of the mouse's.</param>
    public MouseLook MouseLookOf(MouseLook? current = null, bool pad = false)
    {
        MouseLook basis = current ?? new MouseLook();
        LookSettings spy = LookOf(merc: false, pad);
        LookSettings merc = LookOf(merc: true, pad);
        return basis with
        {
            Pad = pad,
            SpyFreeLookResponse = spy.LookResponse,
            SpyFreeLook = spy.FreeLook,
            SpyGunLook = spy.Look,
            SpyScope = spy.Scope,
            SpyVertical = spy.Vertical,
            SpyCam = spy.Cam,
            MercLook = merc.Look,
            MercScope = merc.Scope,
            MercScopeVertical = merc.ScopeVertical,
            MercVertical = merc.Vertical,
            MercCam = merc.Cam,
            InvertY = InvertY,
            InvertXSpy = InvertXSpy,
            Scale = pad ? 1f : MouseScale,
        };
    }

    internal static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    public string ToJson() => JsonSerializer.Serialize(this, Json);

    public static ControlSettings FromJson(string json)
    {
        ControlSettings settings = JsonSerializer.Deserialize<ControlSettings>(json, Json) ?? new ControlSettings();
        settings.Clamp();
        return settings;
    }

    /// <summary>Reads the settings file; the defaults when there is none.</summary>
    public static ControlSettings Load(string path) => File.Exists(path) ? FromJson(File.ReadAllText(path)) : new ControlSettings();

    public void Save(string path) => ControlBindings.WriteWhole(path, ToJson());
}

/// <summary>
/// The field of view on the screen from a camera's FovAngle (horizontal over a 4:3 picture), as Reloaded's widescreen
/// settings make it (ported from OpenSCCT game/Scripts/Options/ViewFov.cs): with widescreen on the 4:3 vertical view
/// is kept and wider screens add width up to 16:9, then every view but the sticky camera and the camera network is
/// capped at ws_fov; with it off the 4:3 horizontal angle is stretched over the whole screen (the unpatched game).
/// </summary>
public static class ViewFov
{
    /// <summary>The picture aspect FovAngle is horizontal for.</summary>
    public const float ReferenceAspect = 4f / 3f;

    /// <summary>The widest aspect the original lays the picture out for.</summary>
    public const float WidestAspect = 16f / 9f;

    /// <summary>The horizontal angle across a screen of <paramref name="aspect"/> (width over height, capped at 16:9), in degrees.</summary>
    public static float ScreenHorizontal(float fov43, float aspect, bool widescreen, float? cap)
    {
        if (!(aspect > 0f) || !(fov43 > 0f))
        {
            return fov43;
        }

        float h = fov43;
        if (widescreen)
        {
            double half = fov43 * Math.PI / 360.0;
            h = (float)(Math.Atan(Math.Tan(half) * Math.Min(aspect, WidestAspect) / ReferenceAspect) * 360.0 / Math.PI);
        }

        if (cap is { } c && c > 0f && h > c)
        {
            h = c;
        }

        return h;
    }

    /// <summary>The vertical angle of a screen whose horizontal angle is <paramref name="horizontal"/>, in degrees.</summary>
    public static float VerticalOf(float horizontal, float aspect)
    {
        if (!(aspect > 0f))
        {
            aspect = ReferenceAspect;
        }

        double half = horizontal * Math.PI / 360.0;
        return (float)(Math.Atan(Math.Tan(half) / aspect) * 360.0 / Math.PI);
    }

    /// <summary>
    /// The horizontal and vertical angles the camera gets for <paramref name="fov43"/> on a picture of
    /// <paramref name="aspect"/>. <paramref name="capped"/> is false only in the views Reloaded leaves uncapped (the
    /// sticky camera and the camera network).
    /// </summary>
    public static (float Horizontal, float Vertical) Screen(float fov43, float aspect, ControlSettings settings, bool capped)
    {
        ArgumentNullException.ThrowIfNull(settings);
        float h = ScreenHorizontal(fov43, aspect, settings.Widescreen, capped && settings.Widescreen ? settings.MercFovCap : null);
        return (h, VerticalOf(h, settings.Widescreen ? Math.Min(aspect, WidestAspect) : aspect));
    }
}
