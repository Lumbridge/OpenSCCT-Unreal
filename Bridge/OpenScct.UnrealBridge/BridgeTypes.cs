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

    /// <summary>
    /// The pawn is seen from its own eyes this frame (the local player's pawn in a first-person view): take its body
    /// out of the main view and keep its shadow. The same rule as <see cref="BridgeView.HideLocalBody"/>.
    /// </summary>
    public const int FirstPerson = 4;
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

    /// <summary>
    /// Version 3. 1 when the local pawn is seen from its own eyes and its body must not be drawn in the main view (keep
    /// it casting its shadow): no free camera, and either no behind view or the spy's first-person view. A merc in a
    /// behind view or under the free camera keeps his body.
    /// </summary>
    public int HideLocalBody;
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

    /// <summary>The last start failed or the session stopped on an error; <c>LastError</c> says why. Ends with Stop or the next start.</summary>
    Failed = 2,
}

/// <summary>What <c>GetSession</c> says about the session in the main slot (version 3).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeSessionInfo
{
    public const int MapCapacity = 32;

    /// <summary>
    /// The session's id: a new number for every local match and every online round, never 0 and never reused in a
    /// process. 0 when no session plays. A change means a new match: drop everything kept from the old one.
    /// </summary>
    public int Id;

    /// <summary><see cref="BridgeState"/>.</summary>
    public int State;

    /// <summary><see cref="BridgeSessionFlags"/>.</summary>
    public int Flags;

    /// <summary>Client simulation ticks run so far.</summary>
    public int Ticks;

    /// <summary>The map's folder name ("AquaE"), UTF-16, zero terminated.</summary>
    public fixed char Map[MapCapacity];
}

public static class BridgeSessionFlags
{
    /// <summary>A round on a remote server (the online table joined it); clear for a local match.</summary>
    public const int Remote = 1;

    /// <summary>The local player is on the mercs' side (his pawn's side once he has one, else the side asked for).</summary>
    public const int Merc = 2;
}

/// <summary>What <c>StartLocalEx</c> starts (version 3). Zero the struct, set <see cref="Size"/>, then the fields.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeStartOptions
{
    public const int MapCapacity = 32;
    public const int ModeCapacity = 32;
    public const int NameCapacity = 48;

    /// <summary>The caller's size of this struct in bytes; fields past it are taken as zero.</summary>
    public int Size;

    /// <summary>1 to play a merc, 0 a spy.</summary>
    public int Merc;

    /// <summary>Server-side bots filling the other slots.</summary>
    public int Bots;

    /// <summary><see cref="BridgeStartFlags"/>.</summary>
    public int Flags;

    /// <summary>The bots' skill: 0 for the default (normal), 1 easy, 2 normal, 3 hard, 4 expert.</summary>
    public int Difficulty;

    /// <summary>The map's folder name, UTF-16, zero terminated.</summary>
    public fixed char Map[MapCapacity];

    /// <summary>Story, DiskHunt, Deathmatch or MercDeathMatch; empty for Story.</summary>
    public fixed char Mode[ModeCapacity];

    /// <summary>The local player's name; empty for "Player".</summary>
    public fixed char PlayerName[NameCapacity];
}

public static class BridgeStartFlags
{
    /// <summary>The mercs carry OpenSCCT's sidearms.</summary>
    public const int Sidearms = 1;

    /// <summary>The mercs may pick OpenSCCT's additional rifles.</summary>
    public const int Rifles = 2;

    /// <summary>Practice rules instead of the map's own: no time limit, unlimited lives, 5 s respawn.</summary>
    public const int OfflineRules = 4;
}
