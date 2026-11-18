using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary><see cref="BridgeNadePreview.State"/>.</summary>
public static class NadePreviewState
{
    /// <summary>Nothing to draw: no grenade gadget ready in the local player's hands.</summary>
    public const int None = 0;

    /// <summary>The aimed throw of the selected grenade gadget, as it would fly if the button were let go now.</summary>
    public const int Aimed = 1;
}

/// <summary><see cref="BridgeNadePreview.Flags"/>.</summary>
public static class NadePreviewFlags
{
    /// <summary>The throw button is held: the charge value is the gauge's, not the full tap.</summary>
    public const int Charging = 1;

    /// <summary>The simulation would refuse the throw now (refire time, reload, muzzle against a wall).</summary>
    public const int Blocked = 2;

    /// <summary>The projectile catches on the first surface it hits (the flare).</summary>
    public const int Stuck = 4;

    /// <summary>The flight ends when the fuse runs out (the grenades), not at a hit.</summary>
    public const int EndedByFuse = 8;

    /// <summary>The projectile is in the air when it ends: there is no surface under the marker.</summary>
    public const int Airborne = 16;

    /// <summary><see cref="BridgeNadePreview.PipX"/> and the two after it are a camera with a clear view of the landing point.</summary>
    public const int PipValid = 32;
}

/// <summary><see cref="BridgeNadePreview.Kind"/>: which projectile, for the caption.</summary>
public static class NadePreviewKind
{
    public const int Other = 0;
    public const int Frag = 1;
    public const int Flare = 2;
    public const int Smoke = 3;
    public const int Chaff = 4;
    public const int FlashBang = 5;
    public const int Phosphorus = 6;
}

/// <summary>
/// The head of the grenade preview of this frame. Every field is 4 bytes wide, so the struct has no padding.
/// Positions are simulation units (Unreal centimetres). Source/OpenSCCT/Runtime/ScctNadePreviewApi.h mirrors it as
/// FScctNadePreview.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeNadePreview
{
    /// <summary><see cref="NadePreviewState"/>.</summary>
    public int State;

    /// <summary><see cref="NadePreviewFlags"/>.</summary>
    public int Flags;

    /// <summary><see cref="NadePreviewKind"/>.</summary>
    public int Kind;

    /// <summary>The local pawn (whose muzzle the line leaves).</summary>
    public int ActorId;

    /// <summary>The charge value 0..1 the throw would be made with (1 for a tap).</summary>
    public float Charge;

    /// <summary>Seconds from the launch to the end of the flight.</summary>
    public float FlightSeconds;

    public int Bounces;

    /// <summary>Changes whenever the path was worked out again (the points are otherwise those of the last call).</summary>
    public int Revision;

    /// <summary>Where the simulation spawns the projectile.</summary>
    public float StartX, StartY, StartZ;

    /// <summary>Where the projectile is when it goes off or catches.</summary>
    public float LandX, LandY, LandZ;

    /// <summary>The surface it rests on or caught on (unit; straight up when <see cref="NadePreviewFlags.Airborne"/>).</summary>
    public float NormalX, NormalY, NormalZ;

    /// <summary>The effect's reach in units (blast, cloud, flash), 0 for none.</summary>
    public float Radius;

    /// <summary>A camera place a little above and behind the landing point with a clear line to it.</summary>
    public float PipX, PipY, PipZ;
}

/// <summary>One point of the path. 16 bytes.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeNadePoint
{
    public float X, Y, Z;

    /// <summary>1 when the projectile bounced here.</summary>
    public int Flags;
}
