using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net.Discovery;
using OpenScct.Server;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of online entry points Unreal calls (Source/OpenSCCT/Runtime/ScctOnlineApi.h mirrors it). Unreal asks
/// for <see cref="OnlineExports.GetApi"/> once and keeps the table. Every entry catches its exceptions (none may
/// cross into native code). Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OnlineApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, char*, char*, void> SetIdentity;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int> Join;
    public delegate* unmanaged[Cdecl]<void> Leave;
    public delegate* unmanaged[Cdecl]<int> Update;
    public delegate* unmanaged[Cdecl]<OnlineStatus*, int> GetStatus;
    public delegate* unmanaged[Cdecl]<OnlinePlayer*, int, int> GetPlayers;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<int, void> SetTeam;
    public delegate* unmanaged[Cdecl]<int, void> SetReady;
    public delegate* unmanaged[Cdecl]<void> Launch;
    public delegate* unmanaged[Cdecl]<void> BackToLobby;
    public delegate* unmanaged[Cdecl]<void> ReportLoaded;
    public delegate* unmanaged[Cdecl]<char*, int, void> SendChat;
    public delegate* unmanaged[Cdecl]<OnlineChatLine*, int> TakeChat;
    public delegate* unmanaged[Cdecl]<OnlineResults*, OnlineResultRow*, int, int> GetResults;
    public delegate* unmanaged[Cdecl]<char*, void> AdminLogin;
    public delegate* unmanaged[Cdecl]<char*, void> AdminCommand;
    public delegate* unmanaged[Cdecl]<char*, char*, void> SetMaster;
    public delegate* unmanaged[Cdecl]<void> RequestServerList;
    public delegate* unmanaged[Cdecl]<OnlineServerEntry*, int, int> GetServers;
    public delegate* unmanaged[Cdecl]<int, char*, int, void> JoinListed;
    public delegate* unmanaged[Cdecl]<char*, void> SetAccountToken;
    public delegate* unmanaged[Cdecl]<void> BeginSignIn;
    public delegate* unmanaged[Cdecl]<void> SignOut;
}

public static unsafe class OnlineExports
{
    private static OnlineBridge Online => OnlineBridge.Instance;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(OnlineApi* api)
    {
        api->Version = OnlineBridge.ApiVersion;
        api->SetIdentity = &SetIdentity;
        api->Join = &Join;
        api->Leave = &Leave;
        api->Update = &Update;
        api->GetStatus = &GetStatus;
        api->GetPlayers = &GetPlayers;
        api->GetText = &GetText;
        api->SetTeam = &SetTeam;
        api->SetReady = &SetReady;
        api->Launch = &Launch;
        api->BackToLobby = &BackToLobby;
        api->ReportLoaded = &ReportLoaded;
        api->SendChat = &SendChat;
        api->TakeChat = &TakeChat;
        api->GetResults = &GetResults;
        api->AdminLogin = &AdminLogin;
        api->AdminCommand = &AdminCommand;
        api->SetMaster = &SetMaster;
        api->RequestServerList = &RequestServerList;
        api->GetServers = &GetServers;
        api->JoinListed = &JoinListed;
        api->SetAccountToken = &SetAccountToken;
        api->BeginSignIn = &BeginSignIn;
        api->SignOut = &SignOut;
        return OnlineBridge.ApiVersion;
    }

    /// <summary>
    /// Who joins: the guest name, and the install id and private player key saved from an earlier run (null or
    /// empty for a first run: new ones are made; read them with GetText and save them).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetIdentity(char* playerName, char* installId, char* privateKey)
    {
        string? name = Text(playerName), id = Text(installId), key = Text(privateKey);
        Guard(() => Online.SetIdentity(name, id, key));
    }

    /// <summary>
    /// Joins the server at "host", "host:port" or "[v6]:port" (port 28888 when none is given). preferredTeam: 0
    /// spies, 1 mercs, 255 watch, -1 the smaller side. Returns 1 when the visit started; follow it with GetStatus.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Join(char* address, char* password, int preferredTeam)
    {
        string? where = Text(address), pass = Text(password);
        try
        {
            if (string.IsNullOrWhiteSpace(where))
            {
                Online.FailJoin("Enter the server's address.");
                return 0;
            }

            Online.Join(where, pass, preferredTeam);
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Leaves the server and clears the visit (also clears a failed one). The state is Idle afterwards.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Leave() => Guard(() => Online.Leave());

    /// <summary>
    /// Once a frame, before the main table's Update: runs the visit's 60 Hz ticks that are due and follows the visit
    /// into and out of rounds. Returns the ticks run.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Update()
    {
        try
        {
            return Online.Update();
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Writes the visit's status. Returns its state.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetStatus(OnlineStatus* status)
    {
        try
        {
            *status = Status(Online);
            return status->State;
        }
        catch (Exception e)
        {
            *status = default;
            Failed(e);
            return 0;
        }
    }

    /// <summary>Writes the session's players. Returns their count (only <paramref name="capacity"/> are written).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPlayers(OnlinePlayer* players, int capacity)
    {
        try
        {
            return Players(Online, players == null || capacity <= 0 ? default : new Span<OnlinePlayer>(players, capacity));
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Copies a text (<see cref="OnlineText"/>) into <paramref name="buffer"/>. Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, TextOf(Online, (OnlineText)kind));
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>Picks a side in the lobby: 0 spies, 1 mercs, 255 spectators.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetTeam(int team) => Guard(() => Online.SetTeam(team));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetReady(int ready) => Guard(() => Online.SetReady(ready != 0));

    /// <summary>Asks for the launch (again: its cancel), or for the next round from the results.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Launch() => Guard(() => Online.Launch());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void BackToLobby() => Guard(() => Online.BackToLobby());

    /// <summary>Unreal's level for the round's map is up: the server may start the round with this player.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ReportLoaded() => Guard(() => Online.ReportLoaded());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SendChat(char* text, int teamOnly)
    {
        string? line = Text(text);
        Guard(() => Online.Chat(line ?? string.Empty, teamOnly != 0));
    }

    /// <summary>Writes the next waiting chat or system line. Returns 1 when one was written, 0 when none waits.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeChat(OnlineChatLine* line)
    {
        try
        {
            if (line == null || !Online.TakeChat(out SessionMessage message))
            {
                return 0;
            }

            Write(message, ref *line);
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>
    /// Writes the last round's results and its players' lines. Returns the number of lines (only
    /// <paramref name="capacity"/> are written), or -1 when there are no results.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetResults(OnlineResults* results, OnlineResultRow* rows, int capacity)
    {
        try
        {
            if (Online.Client?.Results is not { } r)
            {
                return -1;
            }

            if (results != null)
            {
                *results = ResultsOf(r);
            }

            return Rows(r, rows == null || capacity <= 0 ? default : new Span<OnlineResultRow>(rows, capacity));
        }
        catch (Exception e)
        {
            Failed(e);
            return -1;
        }
    }

    /// <summary>Logs in as an admin of a dedicated server.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void AdminLogin(char* password)
    {
        string? pass = Text(password);
        Guard(() => Online.Admin(pass ?? string.Empty));
    }

    /// <summary>An admin console command (status, map, set...); the answers arrive as system chat lines.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void AdminCommand(char* command)
    {
        string? line = Text(command);
        Guard(() => Online.Command(line ?? string.Empty));
    }

    /// <summary>Sets the master server's address and invite key (null or empty for none).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetMaster(char* url, char* inviteKey)
    {
        string? address = Text(url), key = Text(inviteKey);
        Guard(() => Online.Master.Configure(address ?? string.Empty, key));
    }

    /// <summary>Asks the master for its servers; the status's MasterState is Busy until the answer is in.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void RequestServerList() => Guard(() => Online.Master.RequestList());

    /// <summary>Writes the last list. Returns the number of servers (only <paramref name="capacity"/> are written).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetServers(OnlineServerEntry* servers, int capacity)
    {
        try
        {
            return Servers(Online, servers == null || capacity <= 0 ? default : new Span<OnlineServerEntry>(servers, capacity));
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Joins the server at <paramref name="index"/> of the last list (GetServers' order).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void JoinListed(int index, char* password, int preferredTeam)
    {
        string? pass = Text(password);
        Guard(() => Online.Master.JoinListed(index, pass, preferredTeam));
    }

    /// <summary>Signs in with a token saved from an earlier sign-in (null or empty: signed out).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetAccountToken(char* token)
    {
        string? saved = Text(token);
        Guard(() => Online.Master.SetAccountToken(saved));
    }

    /// <summary>Starts a sign-in: open the SignInUrl text in the player's browser once it is there.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void BeginSignIn() => Guard(() => Online.Master.BeginSignIn());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SignOut() => Guard(() => Online.Master.SignOut());

    // ---- the managed side of the reads (the probe calls these too) ----------------------------------------------

    /// <summary>The visit's status as <c>GetStatus</c> writes it.</summary>
    public static OnlineStatus Status(OnlineBridge online)
    {
        OnlineClient? client = online.Client;
        LobbyState? lobby = client?.Lobby;
        LobbySetup? setup = lobby?.Setup;
        LobbyPlayer? local = lobby?.Local;
        int flags = 0;
        if (lobby is not null)
        {
            flags |= lobby.Dedicated ? OnlineStatusFlags.Dedicated : 0;
            flags |= lobby.PreparingBots ? OnlineStatusFlags.PreparingBots : 0;
            flags |= lobby.CanControl ? OnlineStatusFlags.CanControl : 0;
            flags |= lobby.CanLaunch ? OnlineStatusFlags.CanLaunch : 0;
            flags |= lobby.CanStart ? OnlineStatusFlags.CanStart : 0;
            flags |= local?.Ready == true ? OnlineStatusFlags.LocalReady : 0;
            flags |= setup!.Practice ? OnlineStatusFlags.Practice : 0;
            flags |= setup.Revenge ? OnlineStatusFlags.Revenge : 0;
        }

        if (client is not null)
        {
            flags |= client.Kicked ? OnlineStatusFlags.Kicked : 0;
            flags |= client.Results is not null ? OnlineStatusFlags.ResultsReady : 0;
        }

        flags |= online.Round is not null ? OnlineStatusFlags.RoundActive : 0;
        flags |= online.NeedsLoadReport ? OnlineStatusFlags.NeedsLoadReport : 0;
        flags |= online.Round?.Client.ReportedWaiting == true ? OnlineStatusFlags.ReportedWaiting : 0;
        flags |= online.RoundRefused ? OnlineStatusFlags.RoundRefused : 0;

        return new OnlineStatus
        {
            State = (int)online.State,
            Phase = lobby is null ? 0 : (int)lobby.Phase,
            Flags = flags,
            Revision = online.Revision,
            LocalId = lobby?.LocalId ?? -1,
            LocalTeam = local?.Team ?? -1,
            PlayerCount = lobby?.Players.Count ?? 0,
            MaxPlayers = lobby?.MaxPlayers ?? 0,
            Mode = setup is null ? 0 : (int)setup.Mode,
            TimeLimit = setup?.TimeLimit ?? 0,
            SpyLives = setup?.SpyLives ?? 0,
            MercLives = setup?.MercLives ?? 0,
            RespawnSeconds = setup?.RespawnSeconds ?? 0,
            ScoreLimit = setup?.ScoreLimit ?? 0,
            GamesInARow = setup?.GamesInARow ?? 0,
            Bots = setup is null ? 0 : (int)setup.Bots,
            Countdown = lobby?.Countdown ?? 0f,
            LoadingPictureSeed = client?.LoadingPictureSeed ?? -1,
            ChatWaiting = online.ChatWaiting,
            MasterState = (int)online.Master.State,
            ServerCount = online.Master.Servers.Count,
            AccountState = (int)online.Master.AccountState,
        };
    }

    /// <summary>Writes the session's players as <c>GetPlayers</c> does. Returns their count.</summary>
    public static int Players(OnlineBridge online, Span<OnlinePlayer> players)
    {
        if (online.Client is not { } client)
        {
            return 0;
        }

        IReadOnlyList<LobbyPlayer> list = client.Lobby.Players;
        for (int i = 0; i < list.Count && i < players.Length; i++)
        {
            LobbyPlayer p = list[i];
            ref OnlinePlayer target = ref players[i];
            target.Id = p.Id;
            target.Team = p.Team;
            target.Flags = (int)p.Flags;
            target.PingMs = p.PingMs;
            target.MapState = (int)p.Map;
            target.Rank = p.Rank;
            fixed (char* name = target.Name)
            {
                OnlineTextCopy.Fixed(name, OnlinePlayer.NameCapacity, p.Name);
            }
        }

        return list.Count;
    }

    /// <summary>A text as <c>GetText</c> copies it.</summary>
    public static string TextOf(OnlineBridge online, OnlineText kind)
    {
        OnlineClient? client = online.Client;
        return kind switch
        {
            OnlineText.Error => online.Error,
            OnlineText.ServerName => client is null ? null : client.Lobby.ServerName.Length > 0 ? client.Lobby.ServerName : client.Target.Name,
            OnlineText.SetupMap => client?.Lobby.Setup.Map,
            OnlineText.CurrentMap => client?.CurrentMap,
            OnlineText.MapToLoad => online.MapToLoad,
            OnlineText.ModeName => client is null ? null : LobbySetup.ModeName(client.Lobby.Setup.Mode),
            OnlineText.SetupDescription => client?.Lobby.Setup.Describe(),
            OnlineText.ConnectLog => client is null ? null : string.Join('\n', client.ConnectLog),
            OnlineText.PlayerName => online.PlayerName,
            OnlineText.InstallId => online.InstallId,
            OnlineText.PrivateKey => online.Key.PrivateKey,
            OnlineText.MasterError => online.Master.Error,
            OnlineText.SignInUrl => online.Master.SignInUrl,
            OnlineText.SignInCode => online.Master.SignInCode,
            OnlineText.AccountToken => online.Master.AccountToken,
            OnlineText.AccountName => online.Master.AccountName,
            OnlineText.AccountError => online.Master.AccountError,
            OnlineText.ResultsMap => client?.Results?.Map,
            OnlineText.MasterMotd => online.Master.Motd,
            _ => null,
        } ?? string.Empty;
    }

    /// <summary>Writes a chat or system line as <c>TakeChat</c> does.</summary>
    public static void Write(SessionMessage message, ref OnlineChatLine line)
    {
        line.System = message.IsSystem ? 1 : 0;
        line.Team = message.Team;
        line.TeamOnly = message.TeamOnly ? 1 : 0;
        fixed (char* sender = line.Sender, text = line.Text)
        {
            OnlineTextCopy.Fixed(sender, OnlineChatLine.SenderCapacity, message.Sender);
            OnlineTextCopy.Fixed(text, OnlineChatLine.TextCapacity, message.Text);
        }
    }

    public static OnlineResults ResultsOf(MatchResults results) => new()
    {
        WinningTeam = results.WinningTeam,
        WinningReason = results.WinningReason,
        ObjectivesTaken = results.ObjectivesTaken,
        ObjectivesNeeded = results.ObjectivesNeeded,
        GamesLeft = results.GamesLeft,
        RowCount = results.Rows.Count,
    };

    /// <summary>Writes the results' lines as <c>GetResults</c> does. Returns their count.</summary>
    public static int Rows(MatchResults results, Span<OnlineResultRow> rows)
    {
        for (int i = 0; i < results.Rows.Count && i < rows.Length; i++)
        {
            ResultRow r = results.Rows[i];
            ref OnlineResultRow target = ref rows[i];
            target.Id = r.Id;
            target.Team = r.Team;
            target.Score = r.Score;
            target.Kills = r.Kills;
            target.Deaths = r.Deaths;
            target.Lives = r.Lives;
            fixed (char* name = target.Name)
            {
                OnlineTextCopy.Fixed(name, OnlinePlayer.NameCapacity, r.Name);
            }
        }

        return results.Rows.Count;
    }

    /// <summary>
    /// Writes the master's last list as <c>GetServers</c> does, with what the server browser greys a server out for:
    /// another build (protocol hash), a missing map, another version of the map (content hash), full.
    /// </summary>
    public static int Servers(OnlineBridge online, Span<OnlineServerEntry> servers)
    {
        IReadOnlyList<ServerEntry> list = online.Master.Servers;
        if (list.Count == 0)
        {
            return 0;
        }

        string protocol = MasterApi.Hex(online.Classes.ComputeProtocolHash());
        MapCatalog maps = online.Catalog.Maps;
        for (int i = 0; i < list.Count && i < servers.Length; i++)
        {
            ServerEntry s = list[i];
            MapListEntry? map = maps.FindMap(s.Map);
            bool sameMap = map is not null && (!MasterApi.TryParseHex(s.Content, out ulong content) || content == 0 || content == map.ContentHash);
            ref OnlineServerEntry target = ref servers[i];
            target.Players = s.Players;
            target.MaxPlayers = s.MaxPlayers;
            target.Mode = s.Mode;
            target.Phase = (int)s.Phase;
            target.AgeSeconds = s.AgeSeconds;
            target.Flags =
                (s.Password ? OnlineServerFlags.Password : 0) |
                (s.Dedicated ? OnlineServerFlags.Dedicated : 0) |
                (s.RequireAccounts ? OnlineServerFlags.RequireAccounts : 0) |
                (s.Trusted ? OnlineServerFlags.Trusted : 0) |
                (s.Practice ? OnlineServerFlags.Practice : 0) |
                (string.Equals(s.Protocol, protocol, StringComparison.OrdinalIgnoreCase) ? OnlineServerFlags.SameBuild : 0) |
                (map is not null ? OnlineServerFlags.HasMap : 0) |
                (sameMap ? OnlineServerFlags.SameMapVersion : 0) |
                (s.MaxPlayers > 0 && s.Players >= s.MaxPlayers ? OnlineServerFlags.Full : 0);
            fixed (char* id = target.Id, name = target.Name, mapName = target.Map, region = target.Region, version = target.Version)
            {
                OnlineTextCopy.Fixed(id, OnlineServerEntry.IdCapacity, s.Id);
                OnlineTextCopy.Fixed(name, OnlineServerEntry.NameCapacity, s.Name);
                OnlineTextCopy.Fixed(mapName, OnlineServerEntry.MapCapacity, s.Map);
                OnlineTextCopy.Fixed(region, OnlineServerEntry.RegionCapacity, s.Region);
                OnlineTextCopy.Fixed(version, OnlineServerEntry.VersionCapacity, s.Version);
            }
        }

        return list.Count;
    }

    // ---- guards -------------------------------------------------------------------------------------------------

    private static string? Text(char* text) => text == null ? null : new string(text);

    private static void Guard(Action action)
    {
        try
        {
            action();
        }
        catch (Exception e)
        {
            Failed(e);
        }
    }

    /// <summary>
    /// An exception nobody expected: the visit ends as failed with its message (so the menus leave the round), and
    /// the whole text goes to the main table's LastError.
    /// </summary>
    private static void Failed(Exception e)
    {
        try
        {
            Online.FailJoin("The online session stopped on an internal error: " + e.Message);
        }
        catch (Exception)
        {
            // Ending the visit failed too; the report below still reaches Unreal.
        }

        Exports.ReportFailure(e.ToString());
    }
}
