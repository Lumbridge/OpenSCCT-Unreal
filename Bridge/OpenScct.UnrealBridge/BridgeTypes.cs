using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctBridgeApi.h mirrors them field for
// field; change both together and bump BridgeSession.ApiVersion.

/// <summary>One pawn as it should be drawn this frame, in the simulation's units (1 unit = 1 Unreal cm).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgePawn
{
    public int ActorId;

    /// <summary>0 spy (SPawnAttaque), 1 merc (SPawnDefense).</summary>
    public int Kind;

    public int Team;
    public int Health;

    /// <summary><see cref="BridgePawnFlags"/>.</summary>
    public int Flags;

    public float X, Y, Z;
    public float PrePivotX, PrePivotY, PrePivotZ;

    /// <summary>Body yaw in degrees.</summary>
    public float Yaw;

    public float CollisionRadius, CollisionHeight;
}

public static class BridgePawnFlags
{
    public const int Visible = 1;
    public const int Local = 2;
}

/// <summary>The local player's camera this frame.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeView
{
    public float X, Y, Z;

    /// <summary>Degrees.</summary>
    public float Pitch, Yaw, Roll;

    public float Fov;

    /// <summary>1 when there is a view to draw.</summary>
    public int Valid;

    /// <summary>1 when the spy is seen through his own eyes.</summary>
    public int SpyFirstPerson;
}

/// <summary>The local player's input for the frame. Buttons follow <c>LocalInput.ButtonNames</c>.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeInput
{
    public const int ButtonCount = 20;

    public fixed byte Buttons[ButtonCount];

    /// <summary>Mouse counts since the last call, Y positive up.</summary>
    public float MouseX, MouseY;

    /// <summary>The aUp axis.</summary>
    public float Up;
}

/// <summary>Where the session is.</summary>
public enum BridgeState
{
    Idle = 0,
    Playing = 1,
    Failed = 2,
}
