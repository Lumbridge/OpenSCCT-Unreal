using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Test;

/// <summary>What <c>TestExports.GetCounters</c> writes: the running session's tick and netcode counters.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeTestCounters
{
    public int SessionId;

    /// <summary><see cref="BridgeTestFlags"/>.</summary>
    public int Flags;

    /// <summary>Client simulation ticks the session has run.</summary>
    public long ClientTicks;

    /// <summary>
    /// The local server's world ticks: exact once the server is stopped (or with an inline host), read from the
    /// running server thread otherwise (a moment old then); -1 for a remote round.
    /// </summary>
    public long ServerTicks;

    /// <summary>Server corrections the local client applied to its own pawn (prediction errors).</summary>
    public long CorrectionsApplied;

    /// <summary>Authoritative corrections the local client received.</summary>
    public long CorrectionsReceived;

    /// <summary>Reinitialisations the server sent the local player; -1 while the server thread runs (not readable then).</summary>
    public long Reinits;

    /// <summary>The local client's connection state (OpenScct.Server.EClientState: 0 connecting ... ).</summary>
    public int ClientState;

    /// <summary>Pawns the local client has with replicated state.</summary>
    public int ClientPawns;
}

public static class BridgeTestFlags
{
    public const int Remote = 1;

    /// <summary>The local server is stopped (<c>StopServer</c>), so its state can be read exactly.</summary>
    public const int ServerStopped = 2;

    /// <summary>The local server failed on its thread.</summary>
    public const int ServerFailed = 4;
}

/// <summary>
/// One pawn of a local match as the test checks see it: where the client has it (this tick's snapshot and the one
/// before, between which the drawn body lies) and, once the server is readable, where the server has it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeTestPawn
{
    /// <summary>The server's actor id (the client's channel NetGUID); 0 when not known.</summary>
    public int ServerActorId;

    /// <summary>The client's actor id (the id GetPawns and the proxies use); 0 when the client does not have the pawn.</summary>
    public int ClientActorId;

    /// <summary><see cref="BridgeTestPawnFlags"/>.</summary>
    public int Flags;

    /// <summary>Moves the server applied from this pawn's player; -1 for a bot or when the server is not readable.</summary>
    public int MovesApplied;

    public float ServerX, ServerY, ServerZ;

    /// <summary>The client's replicated location of the last tick (as the drawn snapshot takes it).</summary>
    public float ClientX, ClientY, ClientZ;

    /// <summary>The same one tick earlier; the same as this tick's when the pawn was placed, reset or is new.</summary>
    public float PreviousX, PreviousY, PreviousZ;
}

public static class BridgeTestPawnFlags
{
    public const int Local = 1;
    public const int OnClient = 2;
    public const int OnServer = 4;
    public const int Bot = 8;
}

/// <summary>What <c>TestExports.GetNetStats</c> writes: the link and netcode figures of a netcode check's match.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeNetStats
{
    public int SessionId;

    /// <summary><see cref="BridgeNetFlags"/>.</summary>
    public int Flags;

    /// <summary>The link profile's round trip, jitter (ms) and loss (% of datagrams each way).</summary>
    public int LatencyMs;

    public int JitterMs;

    public float LossPercent;

    /// <summary>The local client's smoothed round trip to the server as its connection measures it (ms), 0 before one.</summary>
    public float RttMs;

    public float MinRttMs;

    /// <summary>The scenario's stage (grab or co-op), 0 for the walk.</summary>
    public int Stage;

    public long ClientTicks;

    /// <summary>The server's ticks (the dedicated server's or the inline listen host's).</summary>
    public long ServerTicks;

    /// <summary>The client tick the local player first had a pawn on; -1 before.</summary>
    public long JoinedTick;

    /// <summary>Server corrections the local client applied to its own pawn.</summary>
    public long CorrectionsApplied;

    public long CorrectionsReceived;

    /// <summary>Reinitialisations the server sent the local player.</summary>
    public long ServerReinits;

    /// <summary>Resets of any pawn the client saw since the join (reinitialisations, placements).</summary>
    public long ClientResets;

    /// <summary>The simulated network's datagrams: sent, lost (including queue drops) and delivered.</summary>
    public long LinkSent;

    public long LinkLost;

    public long LinkDelivered;

    /// <summary>Bot clients joined, and those that left or resynchronised their moves (dedicated only).</summary>
    public int BotsJoined;

    public int BotProblems;

    /// <summary>The grab scenario's merc health lost so far.</summary>
    public int HealthDrop;

    /// <summary>The server actor id the scenario is about (the grabbed merc, the climber), or 0.</summary>
    public int SubjectActorId;
}

public static class BridgeNetFlags
{
    /// <summary>The session runs over a simulated link set by SetNextMatchLink.</summary>
    public const int Linked = 1;

    public const int Dedicated = 2;

    /// <summary>The original's net profile (Stock), not OpenSCCT's tuned one.</summary>
    public const int Stock = 4;

    public const int ScenarioDone = 8;

    public const int ScenarioFailed = 16;
}

/// <summary>One pawn of a netcode check's match: the server's location now and the client's, as drawn.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeNetPawn
{
    public int ServerActorId;

    /// <summary>The client's actor id (the proxies' id); 0 when the client does not have the pawn.</summary>
    public int ClientActorId;

    /// <summary><see cref="BridgeNetPawnFlags"/>.</summary>
    public int Flags;

    /// <summary>Resets the client has seen of this pawn (a drawn body may jump only on one).</summary>
    public int Resets;

    public float ServerX, ServerY, ServerZ;

    public float ClientX, ClientY, ClientZ;

    /// <summary>
    /// The distance from the client's location to the nearest place the server had the pawn over the last 1.5 s
    /// (a remote pawn is drawn behind the server); -1 when not known.
    /// </summary>
    public float Drift;
}

public static class BridgeNetPawnFlags
{
    public const int Local = 1;
    public const int OnClient = 2;
    public const int OnServer = 4;
    public const int Bot = 8;
}
