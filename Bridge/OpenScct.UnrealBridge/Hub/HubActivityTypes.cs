using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Hub;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctHubActivityApi.h mirrors them field for
// field; change both together and bump HubActivityExports.ApiVersion.

public static class HubActivityFlags
{
    // HubActivityState.Flags
    /// <summary>The visit's session is drawn.</summary>
    public const int InHub = 1;

    /// <summary>The player runs a course (the clock runs).</summary>
    public const int Running = 2;

    /// <summary>The player shoots a range round.</summary>
    public const int InRound = 4;

    /// <summary>The server has told where the plaza ball is.</summary>
    public const int HasBall = 8;

    /// <summary>The local player plays an emote now.</summary>
    public const int Emoting = 16;

    // HubActivityMark.Flags
    /// <summary>The mark is drawn (a checkpoint the run has passed is not).</summary>
    public const int Shown = 1;

    /// <summary>The next checkpoint of the player's run: it pulses.</summary>
    public const int Next = 2;

    // HubActivityTarget.Flags
    /// <summary>The target belongs to the player's own range round (red; else grey).</summary>
    public const int Mine = 1;

    /// <summary>The target is up or still flipping (drawn).</summary>
    public const int Up = 2;
}

/// <summary>What a mark is (HubMark.Kind).</summary>
public enum HubMarkKind
{
    Other = 0,
    Start = 1,
    Finish = 2,
    Checkpoint = 3,
    Travel = 4,
    FiringLine = 5,
    Jukebox = 6,
    Laptop = 7,
    Goal = 8,
}

/// <summary>The texts GetText reads.</summary>
public enum HubActivityText
{
    /// <summary>The clock of what the player does: the course run, the range round, or a finished run for 6 s; else empty.</summary>
    Activity = 0,

    /// <summary>The notices still up, one per line, a big one in "&gt; &lt;".</summary>
    Notices = 1,

    /// <summary>The title of the nearest score board within 2200 units of the eye that is not a world sign, else empty.</summary>
    BoardTitle = 2,

    /// <summary>That board's rows, one per line.</summary>
    BoardLines = 3,

    /// <summary>The jukebox's record ("Night Shift").</summary>
    TrackTitle = 4,

    /// <summary>The local player's emote and how long it has played ("cheer 1.2 s"), else empty.</summary>
    LocalEmote = 5,
}

/// <summary>What the hub's activities show every frame (GetState).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HubActivityState
{
    /// <summary>1 while in a hub (the rest is valid), else 0.</summary>
    public int Valid;

    /// <summary><see cref="HubActivityFlags"/> (the state's group).</summary>
    public int Flags;

    /// <summary>The jukebox's track (0 to 3), -1 before the hub said.</summary>
    public int Track;

    public int MarkCount;

    /// <summary>Changes when a mark was added, moved or replaced.</summary>
    public int MarkRevision;

    public int TargetCount;

    public int LocalPlayerId;

    /// <summary>Poses that carried an emote since the bridge started (each pawn each drawn frame): the overlay is applied.</summary>
    public int EmotePoses;

    /// <summary>The play clock, seconds.</summary>
    public float Now;

    /// <summary>The plaza ball now (run on from the server's last word), UE units.</summary>
    public float BallX;
    public float BallY;
    public float BallZ;
}

/// <summary>A spot the server marked (a gate, a checkpoint, the firing line, the jukebox, a laptop, a goal).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubActivityMark
{
    public const int IdCapacity = 48;

    /// <summary><see cref="HubMarkKind"/>.</summary>
    public int Kind;

    /// <summary><see cref="HubActivityFlags"/> (the mark's group).</summary>
    public int Flags;
    public float X, Y, Z;
    public float Radius;
    public fixed char Id[IdCapacity];
}

/// <summary>How one other player's voice would be played to the listener (<see cref="HubVoiceHook"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubVoiceMixRow
{
    public const int NameCapacity = 32;

    /// <summary>The world's collision stands between his mouth and the ear.</summary>
    public const int Occluded = 1;

    /// <summary>The mix's gain is above 0 (in hearing range).</summary>
    public const int Audible = 2;

    public int PlayerId;
    public int Flags;

    /// <summary>From the ear to his mouth, UE units.</summary>
    public float Distance;

    /// <summary>The smoothed gain (0 to 1 before the listener's own volume for him).</summary>
    public float Gain;

    /// <summary>The smoothed low-pass cut-off, Hz (20000 in the open, 1200 through a wall).</summary>
    public float LowPassHz;

    /// <summary>His mouth (his pawn's view point), UE units.</summary>
    public float X, Y, Z;
    public fixed char Name[NameCapacity];
}

/// <summary>A pop-up target of the aim range.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HubActivityTarget
{
    public int Id;

    /// <summary><see cref="HubActivityFlags"/> (the target's group).</summary>
    public int Flags;

    /// <summary>Its foot, UE units.</summary>
    public float X, Y, Z;

    /// <summary>How far it lies down, degrees (0 standing, 90 flat), from its flip over a fifth of a second.</summary>
    public float Angle;
}
