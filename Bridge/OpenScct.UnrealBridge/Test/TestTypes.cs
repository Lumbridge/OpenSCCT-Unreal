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
