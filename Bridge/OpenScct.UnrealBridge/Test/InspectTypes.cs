using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Test;

/// <summary>What <c>InspectExports.GetCounters</c> writes: the session's sound and local-snap figures for the play inspector.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeInspectCounters
{
    public int SessionId;

    /// <summary>Client ticks in which the local pawn moved this much farther than its velocity explains (a snap).</summary>
    public int Snaps;

    /// <summary>The largest unexplained one-tick move of the local pawn (uu).</summary>
    public float WorstSnap;

    /// <summary>1 when the content has the sound library (shared/audio/audio.json), else 0.</summary>
    public int HasSoundLibrary;

    /// <summary>Client ticks since the inspector's track began.</summary>
    public long ClientTicks;

    /// <summary>PlaySound events of the client world that reached the sound resolver (started, silent, duplicate, out of reach or unknown).</summary>
    public long SoundEvents;

    /// <summary>Of those, the ones resolved to a file and handed to Unreal as a sound.</summary>
    public long SoundsStarted;

    /// <summary>Of those, the ones naming a sound the library does not have.</summary>
    public long SoundsUnknown;

    /// <summary>Client ticks the local pawn moved faster than the inspector's moving speed.</summary>
    public long LocalMovingTicks;
}

/// <summary>One client pawn as the inspector reads it: its animation channel 0, the channels without a clip, physics and place.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeInspectPawn
{
    /// <summary>The client actor id (the id Unreal's pawn bodies use).</summary>
    public int ActorId;

    /// <summary><see cref="BridgeInspectPawnFlags"/>.</summary>
    public int Flags;

    /// <summary>Channel 0's sequence as a name id (<c>InspectExports.GetName</c>), 0 for none.</summary>
    public int Channel0Name;

    public float Channel0Frame;
    public float Channel0Rate;
    public int Channel0Frames;

    /// <summary>Bit c set: channel c plays a sequence (blended in, for c over 0) for which the simulation has no clip.</summary>
    public int MissingSimClips;

    /// <summary>Bit c set: channel c plays a sequence the drawn character file has no animation for.</summary>
    public int MissingMeshClips;

    /// <summary>The sequence of the lowest channel in either mask as a name id, 0 for none.</summary>
    public int MissingName;

    /// <summary>The pawn's location at the last tick (with its network correction offset), uu.</summary>
    public float X, Y, Z;

    public float VelocityX, VelocityY, VelocityZ;
    public float CollisionRadius, CollisionHeight;
}

public static class BridgeInspectPawnFlags
{
    public const int Local = 1;
    public const int Crouched = 2;

    /// <summary>The bridge has posed it (its character file exists and it has had a tick).</summary>
    public const int Posed = 4;

    /// <summary>The simulation shows it (not hidden, and a remote pawn's pose has arrived).</summary>
    public const int Visible = 8;

    public const int Walking = 16;
    public const int Falling = 32;

    /// <summary>Walking now but falling within the last few ticks: the drawn body still blends out of the landing.</summary>
    public const int JustLanded = 64;
}
