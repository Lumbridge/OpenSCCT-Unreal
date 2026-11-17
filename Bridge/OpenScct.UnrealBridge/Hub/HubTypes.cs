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

    /// <summary>
    /// Away at a pad's match: the hub connection is closed, the online bridge plays the match, and the visit joins the
    /// hub again after its results (HubPadsExports reads the trip). Not a reason to leave the hub when its level closes.
    /// </summary>
    InMatch = 5,
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

    /// <summary>The hub this PC hosts lists itself in the master's hub list (with its NAT punch and relay).</summary>
    public const int HostListed = 64;

    /// <summary>The visit is a hub of the master's list (it came in through the master's join answer).</summary>
    public const int ListedHub = 128;

    /// <summary>The master is being asked the way to a listed hub (no connection yet).</summary>
    public const int AskingMaster = 256;
}

/// <summary>Where the master's hub list is.</summary>
public enum HubListState
{
    /// <summary>Not asked yet.</summary>
    Idle = 0,

    /// <summary>Asked; the answer is not in.</summary>
    Busy = 1,

    /// <summary>The list is in (it may be empty).</summary>
    Ready = 2,

    /// <summary>The master did not answer, or no master is set (text <see cref="HubText.ListMessage"/>).</summary>
    Failed = 3,
}

public static class HubListFlags
{
    /// <summary>One of the OpenSCCT servers' own hubs.</summary>
    public const int Official = 1;

    /// <summary>A dedicated hub server (not one a player hosts from his game).</summary>
    public const int Dedicated = 2;

    /// <summary>Only players signed in on the master may join.</summary>
    public const int RequireAccounts = 4;

    public const int Full = 8;

    /// <summary>It runs another build's network protocol: the join would be refused.</summary>
    public const int OtherBuild = 16;

    /// <summary>Its hub map is not the one installed here.</summary>
    public const int OtherMap = 32;

    /// <summary>A ping answer came (<see cref="HubListEntry.PingMs"/> is valid).</summary>
    public const int Pinged = 64;
}

/// <summary>One hub of the master's hub list (GetHubList), in the page's order: official hubs first, then by ping.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubListEntry
{
    public const int IdCapacity = 64;
    public const int NameCapacity = 64;
    public const int RegionCapacity = 16;
    public const int ProblemCapacity = 32;

    public int Players, MaxPlayers;

    /// <summary>The lowest of the last answers' round trips in ms, -1 before an answer.</summary>
    public int PingMs;

    /// <summary><see cref="HubListFlags"/>.</summary>
    public int Flags;

    public fixed char Id[IdCapacity];
    public fixed char Name[NameCapacity];
    public fixed char Region[RegionCapacity];

    /// <summary>Why it cannot be joined ("another build", "another hub map", "full"), empty when it can.</summary>
    public fixed char Problem[ProblemCapacity];
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

    /// <summary>What the master's hub list says ("3 hub(s) listed.", "Asking the master server for hubs...", its error).</summary>
    ListMessage = 6,
}
