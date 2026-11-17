using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctControlsApi.h mirrors them field for
// field; change both together and bump ControlsExports.ApiVersion.

/// <summary>The devices as Unreal reads them in one rendered frame.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeRawInput
{
    public const int KeyCount = Controls.BridgeKeys.Count;

    /// <summary>
    /// One byte per <see cref="Controls.BridgeKey"/>. Bit 0: down now. Bit 1: went down since the last frame (a tap
    /// shorter than a frame still counts). For MouseWheelUp and MouseWheelDown: the notches turned since the last frame.
    /// </summary>
    public fixed byte Keys[KeyCount];

    /// <summary>Raw mouse counts since the last frame (no smoothing, no acceleration): right positive.</summary>
    public float MouseX;

    /// <summary>Raw mouse counts since the last frame: up (the mouse pushed away) positive.</summary>
    public float MouseY;

    /// <summary>The controller's sticks, -1 to 1, right and up positive, with no deadzone applied.</summary>
    public float LeftX, LeftY, RightX, RightY;

    /// <summary>The controller's triggers, 0 (released) to 1.</summary>
    public float LeftTrigger, RightTrigger;

    /// <summary>The length of the frame, in seconds.</summary>
    public float DeltaSeconds;

    /// <summary><see cref="BridgeRawInputFlags"/>.</summary>
    public int Flags;
}

public static class BridgeRawInputFlags
{
    /// <summary>
    /// The game has the input: the window has focus, the pointer is captured and no menu or text field takes the
    /// keys. Without it nothing is read and everything held is released.
    /// </summary>
    public const int Play = 1;
}

/// <summary>
/// The local player's camera as drawn this frame: the view between the last two ticks, turned by the mouse counts the
/// simulation has not taken yet. Use it in place of <see cref="BridgeView"/>.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLookView
{
    /// <summary>The camera, in the simulation's units (1 unit = 1 Unreal cm).</summary>
    public float X, Y, Z;

    /// <summary>Degrees.</summary>
    public float Pitch, Yaw, Roll;

    /// <summary>The simulation's FovAngle in degrees: horizontal over a 4:3 picture, with the merc's aim zoom in it.</summary>
    public float Fov;

    /// <summary>The horizontal angle on a screen of the aspect asked for (Reloaded's widescreen rule and FOV cap), in degrees.</summary>
    public float HorizontalFov;

    /// <summary>The vertical angle on that screen, in degrees. The one to give the camera: it does not depend on how wide the window is.</summary>
    public float VerticalFov;

    /// <summary>
    /// The player's aim as drawn: the controller's rotation plus the mouse turn not yet simulated, in degrees. The
    /// local spy's arm and pistol point along it (it is the view itself in first person).
    /// </summary>
    public float AimPitch, AimYaw;

    /// <summary>1 when there is a view to draw.</summary>
    public int Valid;

    /// <summary>1 when the spy is seen through his own eyes.</summary>
    public int SpyFirstPerson;

    /// <summary>1 when this frame's view is not a continuation of the last one (a respawn, a change of camera): no motion blur or smoothing across it.</summary>
    public int Cut;

    /// <summary>1 when no collision-free eye position exists: cover the world for this frame.</summary>
    public int Occluded;

    /// <summary>1 while the mouse turn is drawn ahead of the simulation.</summary>
    public int LookAhead;

    /// <summary>The merc's rifle aim zoom, 0 (hip) to 1 (aimed), eased as the view's FOV is.</summary>
    public float RifleAimBlend;
}

/// <summary>An interface command a binding raised (ShowMenu, Talk, PushToTalk...). The name is behind <c>GetName</c>.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLocalCommand
{
    /// <summary>The command's name id (stable for the life of the process).</summary>
    public int NameId;

    /// <summary>The number after the command in the binding, 0 when none. For ToggleAutoHack: the new setting.</summary>
    public int Arg;

    /// <summary>1 on the press, 0 on the release (held commands: push to talk, the held scoreboard, the team ping).</summary>
    public int Pressed;
}

/// <summary>Look settings of one side and device (<see cref="Controls.LookSettings"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLookSettings
{
    /// <summary>1 hold, 0 toggle: the walk key, the merc's scope key, the spy's first-person key, the spy's cloak.</summary>
    public int WalkHold, ScopeHold, PerspectiveHold, CloakHold;

    /// <summary>Look multipliers (1 = the original's speed): spy free look, look (spy gun drawn; merc), scope or binoculars, the merc scope's vertical, unused vertical, camera views.</summary>
    public float FreeLook, Look, Scope, ScopeVertical, Vertical, Cam;

    /// <summary>Response curves: 0 Linear, 1 CurveSoft, 2 CurveMedium, 3 CurveAggressive.</summary>
    public int LookResponse, GunLookResponse;

    /// <summary>The sensitivity clutches: multiplier and 1 when enabled.</summary>
    public float ClutchOne;
    public int ClutchOneEnabled;
    public float ClutchTwo;
    public int ClutchTwoEnabled;

    /// <summary>Controller groups: 1 when the stick click and the bumper of that side trade places.</summary>
    public int SwapLsLb, SwapRsRb;
}

/// <summary>The control settings (<see cref="Controls.ControlSettings"/>). Booleans are 0 or 1.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeControlSettings
{
    public int InvertY;
    public int InvertXSpy;
    public int SpyRollFreeLook;
    public int AutoHackComms;

    /// <summary>0 the wheel changes gadgets, 1 it sets walk and run.</summary>
    public int MouseWheelSpeed;

    public int Widescreen;

    /// <summary>The cap on the horizontal field of view, degrees (95 to 112).</summary>
    public float MercFovCap;

    /// <summary>A multiplier over every mouse count (1 = the original's turn per count).</summary>
    public float MouseScale;

    public BridgeLookSettings SpyKbm, MercKbm, SpyPad, MercPad;

    public int PadEnabled, PadMove, PadAim, PadBinds, PadSlowWalk, PadSpyMove8, PadMercMove8;

    /// <summary>Stick deadzones, 0 to 0.9 of the travel.</summary>
    public float PadDeadzoneLeft, PadDeadzoneRight;

    /// <summary>The look stick's vertical speed against its sideways speed.</summary>
    public float PadVerticalScale;

    /// <summary>The spy runs with the move stick at least this far out (0.1 to 1).</summary>
    public float PadRunThreshold;

    /// <summary>How far a trigger goes down before it is pressed (0 to 0.99).</summary>
    public float PadTriggerThreshold;

    /// <summary>A multiplier over the look stick's speed.</summary>
    public float PadLookScale;
}
