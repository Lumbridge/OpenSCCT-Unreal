using System;
using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctOnlineApi.h mirrors them field for
// field; change both together and bump OnlineBridge.ApiVersion. Text fields are UTF-16, zero terminated, and cut
// to their capacity.

/// <summary>
/// Where the online visit is. The values from Connecting on are OpenSCCT's <c>OnlineClientState</c> plus one, so
/// 0 can mean "no visit".
/// </summary>
public enum OnlineState
{
    /// <summary>Not online: no visit was started, or the last one was cleared with Leave.</summary>
    Idle = 0,

    /// <summary>Asking the master how to reach a listed server, or trying the join attempts.</summary>
    Connecting = 1,

    Lobby = 2,

    /// <summary>Between maps: the old connection closed, the next one starts after the quiet time.</summary>
    Travelling = 3,

    /// <summary>Connected to the round's map; the round has not started.</summary>
    Loading = 4,

    Match = 5,

    Results = 6,

    /// <summary>The visit ended with an error (text <see cref="OnlineText.Error"/>).</summary>
    Failed = 7,

    /// <summary>The player left.</summary>
    Left = 8,
}

public static class OnlineStatusFlags
{
    /// <summary>A dedicated server (no player is its host).</summary>
    public const int Dedicated = 1;

    /// <summary>The server kicked or banned this player (the state is Failed).</summary>
    public const int Kicked = 2;

    /// <summary>A round's play session runs: pawns, the view and input work as in a local match.</summary>
    public const int RoundActive = 4;

    /// <summary>The launch waits for the server to prepare bot navigation.</summary>
    public const int PreparingBots = 8;

    /// <summary>The local player may change the setup (the host, an admin, anyone on an open dedicated server).</summary>
    public const int CanControl = 16;

    /// <summary>The local player may ask for the launch.</summary>
    public const int CanLaunch = 32;

    /// <summary>Everyone on a side is ready and has the map: a launch would start.</summary>
    public const int CanStart = 64;

    /// <summary>The round waits for <c>ReportLoaded</c>: Unreal has not said yet that its level is up.</summary>
    public const int NeedsLoadReport = 128;

    /// <summary>The server knows this player is in the map and waiting for the round to start.</summary>
    public const int ReportedWaiting = 256;

    /// <summary>The last round's results are in (<c>GetResults</c>).</summary>
    public const int ResultsReady = 512;

    /// <summary>The local player is ready (lobby and results).</summary>
    public const int LocalReady = 1024;

    /// <summary>The round's map has no converted content here to play it on; the visit goes on without a round.</summary>
    public const int RoundRefused = 2048;

    /// <summary>A practice server.</summary>
    public const int Practice = 4096;

    /// <summary>A revenge pair: two games with the sides swapped for the second.</summary>
    public const int Revenge = 8192;
}

/// <summary>The visit in one read (<c>GetStatus</c>), cheap enough for every frame.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct OnlineStatus
{
    /// <summary><see cref="OnlineState"/>.</summary>
    public int State;

    /// <summary>The server's session phase: 0 lobby, 1 loading, 2 match, 3 results.</summary>
    public int Phase;

    /// <summary><see cref="OnlineStatusFlags"/>.</summary>
    public int Flags;

    /// <summary>
    /// Goes up whenever the lobby, the phase, the results or a text changed: read the players and texts again only
    /// when it differs from the last one seen.
    /// </summary>
    public int Revision;

    /// <summary>This player's id in the session, -1 before the server said hello.</summary>
    public int LocalId;

    /// <summary>This player's side: 0 spies, 1 mercs, 255 spectator, -1 not known yet.</summary>
    public int LocalTeam;

    public int PlayerCount;
    public int MaxPlayers;

    /// <summary>The setup's mode (EGameMode: 4 Disk Hunt, 5 Story, 6 Deathmatch, 7 Merc Deathmatch...).</summary>
    public int Mode;

    /// <summary>Minutes, 0 for no limit.</summary>
    public int TimeLimit;

    /// <summary>Lives of each spy and each merc, 0 for unlimited.</summary>
    public int SpyLives, MercLives;

    public int RespawnSeconds;

    /// <summary>Kill limit of Merc Deathmatch, disks to take in Disk Hunt, else the map's score limit.</summary>
    public int ScoreLimit;

    public int GamesInARow;

    /// <summary>Server bots: 0 off, 1 a number per side, 2 fill the empty places, 3 custom.</summary>
    public int Bots;

    /// <summary>Seconds left of the launch countdown or of the results, or 0.</summary>
    public float Countdown;

    /// <summary>The server's pick among the map's loading pictures (the same for every player), -1 for none.</summary>
    public int LoadingPictureSeed;

    /// <summary>Chat and system lines waiting for <c>TakeChat</c>.</summary>
    public int ChatWaiting;

    /// <summary><see cref="OnlineMasterState"/> of the last master request (the server list or a listed join).</summary>
    public int MasterState;

    /// <summary>Servers of the last list (<c>GetServers</c>).</summary>
    public int ServerCount;

    /// <summary><see cref="OnlineAccountState"/>.</summary>
    public int AccountState;
}

/// <summary>One player of the session (<c>GetPlayers</c>), as the lobby and the scoreboard list them.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OnlinePlayer
{
    public const int NameCapacity = 48;

    public int Id;

    /// <summary>0 spies, 1 mercs, 255 spectator.</summary>
    public int Team;

    /// <summary>
    /// OpenSCCT's <c>SessionPlayerFlags</c>: 1 ready, 2 host, 4 admin, 8 spectator, 16 in game, 32 loading,
    /// 64 may change the setup, 128 mic restricted to team, 256 bot, 512 signed in, 1024 voice banned.
    /// </summary>
    public int Flags;

    public int PingMs;

    /// <summary>Whether the player has the setup's map: 0 unknown, 1 yes, 2 missing, 3 another version.</summary>
    public int MapState;

    /// <summary>The player's rank with its title (OpenSCCT's PlayerRank packed), 0 for guests and bots.</summary>
    public int Rank;

    public fixed char Name[NameCapacity];
}

/// <summary>One chat or system line (<c>TakeChat</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OnlineChatLine
{
    public const int SenderCapacity = 48;
    public const int TextCapacity = 256;

    /// <summary>1 for a line of the server itself (joins, leaves, command answers): it has no sender.</summary>
    public int System;

    /// <summary>The sender's side (0 spies, 1 mercs), 255 for none.</summary>
    public int Team;

    /// <summary>1 when only the sender's side got it.</summary>
    public int TeamOnly;

    public fixed char Sender[SenderCapacity];
    public fixed char Text[TextCapacity];
}

/// <summary>The results of the last round (<c>GetResults</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct OnlineResults
{
    /// <summary>0 spies, 1 mercs, 255 none (the round was stopped).</summary>
    public int WinningTeam;

    /// <summary>0 objectives or kill limit, 1 time out, 2 a side eliminated, 255 the round was stopped.</summary>
    public int WinningReason;

    public int ObjectivesTaken, ObjectivesNeeded;

    /// <summary>Games of the series still to play (0: the lobby is next).</summary>
    public int GamesLeft;

    public int RowCount;
}

/// <summary>One player's line of the results.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OnlineResultRow
{
    public int Id;
    public int Team;
    public int Score, Kills, Deaths, Lives;
    public fixed char Name[OnlinePlayer.NameCapacity];
}

public static class OnlineServerFlags
{
    public const int Password = 1;
    public const int Dedicated = 2;

    /// <summary>Only players signed in on the master may join.</summary>
    public const int RequireAccounts = 4;

    /// <summary>One of the master's own servers (achievements count there).</summary>
    public const int Trusted = 8;

    public const int Practice = 16;

    /// <summary>The server runs this build's network protocol; without it the join is refused.</summary>
    public const int SameBuild = 32;

    /// <summary>The server's map is installed here.</summary>
    public const int HasMap = 64;

    /// <summary>The installed map is the server's version of it.</summary>
    public const int SameMapVersion = 128;

    public const int Full = 256;
}

/// <summary>One server of the master's list (<c>GetServers</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OnlineServerEntry
{
    public const int IdCapacity = 64;
    public const int NameCapacity = 64;
    public const int MapCapacity = 32;
    public const int RegionCapacity = 16;
    public const int VersionCapacity = 24;

    public int Players, MaxPlayers;

    /// <summary>EGameMode, as <see cref="OnlineStatus.Mode"/>.</summary>
    public int Mode;

    /// <summary>0 lobby, 1 loading, 2 match, 3 results.</summary>
    public int Phase;

    /// <summary><see cref="OnlineServerFlags"/>.</summary>
    public int Flags;

    /// <summary>Seconds since the server's last heartbeat.</summary>
    public int AgeSeconds;

    public fixed char Id[IdCapacity];
    public fixed char Name[NameCapacity];
    public fixed char Map[MapCapacity];
    public fixed char Region[RegionCapacity];
    public fixed char Version[VersionCapacity];
}

/// <summary>Where the last master request is.</summary>
public enum OnlineMasterState
{
    Idle = 0,
    Busy = 1,
    Ready = 2,

    /// <summary>It failed (text <see cref="OnlineText.MasterError"/>).</summary>
    Failed = 3,
}

public enum OnlineAccountState
{
    SignedOut = 0,

    /// <summary>The sign-in page is open in the player's browser (texts SignInUrl and SignInCode); polling.</summary>
    SigningIn = 1,

    SignedIn = 2,

    /// <summary>The sign-in failed or the token was refused (text <see cref="OnlineText.AccountError"/>).</summary>
    Failed = 3,
}

/// <summary>The texts <c>GetText</c> reads.</summary>
public enum OnlineText
{
    /// <summary>Why the visit failed, as a player reads it.</summary>
    Error = 0,

    ServerName = 1,

    /// <summary>The map of the match the lobby sets up (its SVMap folder name, e.g. AquaE).</summary>
    SetupMap = 2,

    /// <summary>The map the connection is on now: "Lobby" in the lobby, a round's map otherwise.</summary>
    CurrentMap = 3,

    /// <summary>
    /// The map Unreal must have loaded for the round: the travel's target while travelling, the round's map while
    /// loading, playing and in the results; empty in the lobby and when not online.
    /// </summary>
    MapToLoad = 4,

    /// <summary>The mode as menus name it (Story, Disk Hunt...).</summary>
    ModeName = 5,

    /// <summary>The whole setup in a line (map, mode, time, lives, respawn, bots...).</summary>
    SetupDescription = 6,

    /// <summary>What the join tried, one attempt per line (the connecting screen).</summary>
    ConnectLog = 7,

    /// <summary>The name the player joins under (the account's when signed in).</summary>
    PlayerName = 8,

    /// <summary>The install's id; save it and give it back with SetIdentity (bans and returning players).</summary>
    InstallId = 9,

    /// <summary>The install's private player key; save it and give it back with SetIdentity. Never show it.</summary>
    PrivateKey = 10,

    MasterError = 11,

    /// <summary>The page to open in the player's browser to sign in.</summary>
    SignInUrl = 12,

    /// <summary>The short code that page shows too.</summary>
    SignInCode = 13,

    /// <summary>The signed-in token; save it and give it back with SetAccountToken. Never show it.</summary>
    AccountToken = 14,

    AccountName = 15,
    AccountError = 16,

    /// <summary>The map of the last results.</summary>
    ResultsMap = 17,

    /// <summary>The master's message of the day from the last list, or empty.</summary>
    MasterMotd = 18,

    /// <summary>
    /// The lobby's setup as an options line ("?MapName=AquaE?GameMode=5?TimeLimit=20..."): the names and values
    /// <c>SetSetupOptions</c> takes.
    /// </summary>
    SetupOptions = 19,

    /// <summary>The maps a round can be played on here (folder names such as AquaE), one per line.</summary>
    InstalledMaps = 20,
}

/// <summary>Copies of text into the fixed fields and buffers that cross to C++.</summary>
internal static unsafe class OnlineTextCopy
{
    /// <summary>Writes <paramref name="text"/> into a fixed field, cut to fit, zero terminated and zero filled.</summary>
    public static void Fixed(char* target, int capacity, string? text)
    {
        var span = new Span<char>(target, capacity);
        int length = Math.Min(text?.Length ?? 0, capacity - 1);
        text.AsSpan(0, length).CopyTo(span);
        span[length..].Clear();
    }

    /// <summary>Writes <paramref name="text"/> into a caller's buffer. Returns its full length in characters.</summary>
    public static int Buffer(char* buffer, int capacity, string? text)
    {
        text ??= string.Empty;
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return text.Length;
    }
}
