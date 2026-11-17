using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Hub;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctHubApi.h mirrors them field for field;
// change both together and bump HubBridge.ApiVersion.

/// <summary>Where the hub visit is.</summary>
public enum HubState
{
    /// <summary>Not in a hub: nothing was joined, or Leave cleared the last visit.</summary>
    Idle = 0,

    /// <summary>Waiting for the hub to answer and welcome the player.</summary>
    Connecting = 1,

    /// <summary>Welcomed: the hub map loads, the player's controller arrives. Open the hub level now.</summary>
    Loading = 2,

    /// <summary>The visit's play session is in the main session slot: pawns, poses, view and input work as in a match.</summary>
    InHub = 3,

    /// <summary>The visit ended with an error (text <see cref="HubText.Error"/>). Leave clears it.</summary>
    Failed = 4,
}

public static class HubStatusFlags
{
    /// <summary>The hub welcomed this player (its name and the player's id are known).</summary>
    public const int Welcomed = 1;

    /// <summary>The visit's play session runs in the main slot.</summary>
    public const int RoundActive = 2;

    /// <summary>The hub waits for <c>ReportLoaded</c>: Unreal has not said yet that its level is up.</summary>
    public const int NeedsLoadReport = 4;

    /// <summary>The hub knows this player is in the map (it spawns his pawn).</summary>
    public const int ReportedWaiting = 8;

    /// <summary>This PC hosts the hub (an in-process hub server).</summary>
    public const int Hosting = 16;

    /// <summary>The local player has a pawn.</summary>
    public const int LocalPawn = 32;
}

/// <summary>The visit in one read (GetStatus).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HubStatus
{
    public int State;
    public int Flags;
    public int Revision;
    public int PlayerId;
    public int Team;
    public int PlayerCount;
    public int MaxPlayers;
    public int HostPort;
    public float ConnectSeconds;
    public int LogWaiting;
}

/// <summary>The texts GetText reads.</summary>
public enum HubText
{
    /// <summary>Why the visit failed, as a player reads it.</summary>
    Error = 0,

    /// <summary>The hub's name (as the hub says it once it welcomed the player, else the address).</summary>
    HubName = 1,

    /// <summary>The map Unreal must have open while in the hub ("Hub"); empty while connecting and when not in a hub.</summary>
    MapToLoad = 2,

    /// <summary>The "host:port" the visit connects to.</summary>
    Address = 3,

    /// <summary>What is happening, as the hub page says it ("Connecting to ...", "In ...").</summary>
    Status = 4,

    /// <summary>The name the player joined under.</summary>
    PlayerName = 5,
}
