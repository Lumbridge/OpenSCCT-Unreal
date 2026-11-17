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

    /// <summary>
    /// The upper-body aim the pose turns "B Spine2" by (the mesh's AAFULL pair), in degrees: yaw from the body's
    /// facing, pitch up. Already in the pose of <c>GetPawnPose</c>; here for whatever else follows the aim.
    /// </summary>
    public float AimYaw, AimPitch;
}

public static class BridgePawnFlags
{
    public const int Visible = 1;
    public const int Local = 2;
}

/// <summary>One bone of a character's skeleton (<c>GetSkeleton</c>), in the order of the character file's skin joints.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeBoneInfo
{
    public const int NameCapacity = 64;

    /// <summary>Index of the parent bone, -1 for a root.</summary>
    public int Parent;

    /// <summary>UTF-16, zero terminated.</summary>
    public fixed char Name[NameCapacity];
}

/// <summary>
/// One bone of a drawn pose (<c>GetPawnPose</c>): its transform relative to its parent, in the character file's own
/// space (metres, Y up, right-handed, as the glb stores its nodes). A bone's transform in the skeleton is its
/// parent's times its own, on column vectors: scale, then rotation, then translation.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeBone
{
    public float Px, Py, Pz;

    /// <summary>Unit quaternion.</summary>
    public float Qx, Qy, Qz, Qw;

    public float Sx, Sy, Sz;
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
