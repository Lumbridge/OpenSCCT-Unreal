using System;
using System.Collections.Generic;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using OpenScct.Net;
using OpenScct.Net.Discovery;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Online;

/// <summary>Where a browser row came from: the master's list, a LAN answer, or a saved address that answered.</summary>
public enum BrowserSource
{
    Internet = 0,
    Lan = 1,
    Saved = 2,
}

/// <summary>How a row's server is reached: not known yet, its public address, a NAT punch, the relay, or the LAN.</summary>
public enum BrowserReach
{
    Checking = 0,
    Direct = 1,
    Punch = 2,
    Relay = 3,
    Lan = 4,
}

/// <summary>One server of the browser: what the master said, what the server itself answered, or both.</summary>
public sealed class BrowserRow
{
    public required string Key { get; init; }

    /// <summary>The master's list, the LAN (when the LAN search found it, saved or not), or a saved address only.</summary>
    public required BrowserSource Source { get; set; }

    /// <summary>The LAN search found the server.</summary>
    public bool OnLan { get; set; }

    public string Name { get; set; } = string.Empty;

    public string Map { get; set; } = string.Empty;

    public int Mode { get; set; }

    public SessionPhase Phase { get; set; }

    public int Players { get; set; }

    public int MaxPlayers { get; set; }

    /// <summary>The lowest of the last answers in milliseconds, or -1 while the server has not answered.</summary>
    public int PingMs { get; set; } = -1;

    public bool Password { get; set; }

    public bool Dedicated { get; set; }

    public bool RequireAccounts { get; set; }

    public bool Practice { get; set; }

    public bool Trusted { get; set; }

    public BrowserReach Reach { get; set; }

    public string Region { get; set; } = string.Empty;

    public ulong Protocol { get; set; }

    public ulong Content { get; set; }

    /// <summary>The master's entry for a listed server; null for a LAN or saved one.</summary>
    public ServerEntry? Entry { get; set; }

    /// <summary>The server's id on its master, from its own answer (a LAN or saved server can be listed too).</summary>
    public string MasterId { get; set; } = string.Empty;

    /// <summary>Where the server answered from (its game port), or null when it never answered.</summary>
    public IPEndPoint? Endpoint { get; set; }

    /// <summary>The saved address the server answered on (it is one of the player's saved servers), or empty.</summary>
    public string SavedAddress { get; set; } = string.Empty;

    /// <summary>The players and their sides: the server's own answer, else the master's list.</summary>
    public IReadOnlyList<ServerInfoPlayer> PlayerList { get; set; } = [];

    /// <summary>The answer last taken into the row (each new answer is a new result).</summary>
    internal ServerQueryResult? Seen { get; set; }
}

/// <summary>
/// The server browser (the Godot client's ServerBrowserScreen, rl:net.master and rl:net.serverlist): the master's list,
/// a LAN search on the LAN port and the player's saved addresses merged into one list, each server asked for its info
/// on its game port so the list shows a measured ping (the lowest of the last three answers, as ServerQuery times
/// them), the phase, the players by side and how it is reached. A listed server is pinged when the master says it can
/// be reached directly; one behind a NAT shows no ping, as in the Godot browser. Every three seconds the LAN, the saved
/// addresses and the directly reachable listed servers are asked again, so the pings stay current while it is open.
/// <para>Joining a row goes the way the row allows: a listed server through the master (its reachability and the
/// connect path, with the signed-in player's ticket); a LAN or saved server straight to its address, after asking the
/// master for a ticket when it is listed there too and the player is signed in.</para>
/// <para>Everything runs on the game thread: <see cref="Pump"/> is called from <see cref="OnlineBridge.Update"/>.</para>
/// </summary>
public sealed class ServerBrowser : IDisposable
{
    /// <summary>A player whose side the master did not say.</summary>
    private const byte NoSide = 254;

    private readonly OnlineBridge _bridge;
    private readonly List<BrowserRow> _rows = new();
    private readonly List<string> _saved = new();
    private readonly List<ServerQueryResult> _answers = new();
    private ServerQuery? _query;
    private long _lastQueryMicros;
    private int _seenMasterList = -1;
    private string _protocolHex = string.Empty;
    private ulong _protocol;

    internal ServerBrowser(OnlineBridge bridge)
    {
        _bridge = bridge;
    }

    /// <summary>The browser is searching (between <see cref="Open"/> and <see cref="Close"/>).</summary>
    public bool IsOpen => _query is not null;

    /// <summary>The LAN port searched.</summary>
    public int LanPort { get; private set; } = BuildInfo.DefaultLanPort;

    /// <summary>The query socket binds the loopback addresses only and the LAN search asks this machine only (test runs).</summary>
    public bool LoopbackOnly { get; private set; }

    /// <summary>Goes up whenever a row or the status changed.</summary>
    public int Revision { get; private set; }

    public IReadOnlyList<BrowserRow> Rows => _rows;

    /// <summary>Why the last <see cref="Join"/> was refused, as a player reads it, or empty.</summary>
    public string Refusal { get; private set; } = string.Empty;

    /// <summary>The saved addresses being asked.</summary>
    public IReadOnlyList<string> Saved => _saved;

    /// <summary>
    /// A new search: forgets the rows, asks the LAN on <paramref name="lanPort"/> and each saved address (separated by
    /// ';' or new lines), and asks the master for its list when one is set. False (with the reason in
    /// <see cref="Refusal"/>) when no UDP socket could be opened; the master's list still comes.
    /// </summary>
    public bool Open(int lanPort, string? saved, bool loopbackOnly)
    {
        Close();
        LanPort = lanPort is > 0 and < 65536 ? lanPort : BuildInfo.DefaultLanPort;
        LoopbackOnly = loopbackOnly;
        _saved.Clear();
        foreach (string address in (saved ?? string.Empty).Split([';', '\n', '\r', ','], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (!_saved.Exists(s => string.Equals(s, address, StringComparison.OrdinalIgnoreCase)))
            {
                _saved.Add(address);
            }
        }

        _protocol = _bridge.Classes.ComputeProtocolHash();
        _protocolHex = MasterApi.Hex(_protocol);
        _seenMasterList = -1;
        _rows.Clear();
        _answers.Clear();
        Refusal = string.Empty;
        bool ok = true;
        try
        {
            _query = new ServerQuery(_bridge.Clock, loopbackOnly);
            // Each answer as it is taken: ServerQuery's own list keeps one answer per game address, so a server found on
            // the LAN and saved too would show only whichever answered last in a frame.
            _query.Answered += _answers.Add;
            QueryAll(listed: false);
        }
        catch (InvalidOperationException e)
        {
            Refusal = e.Message;
            ok = false;
        }

        if (_bridge.Master.Url is not null)
        {
            _bridge.Master.RequestList();
        }

        // A list the master already gave (an earlier visit to the page) shows at once.
        MergeList();
        Touch();
        return ok;
    }

    /// <summary>Stops searching and closes the query socket. The rows stay until the next <see cref="Open"/>.</summary>
    public void Close()
    {
        _query?.Dispose();
        _query = null;
    }

    /// <summary>Takes the answers that arrived and the master's new list, and asks again every few seconds.</summary>
    internal void Pump()
    {
        bool changed = MergeList();
        if (_query is { } query)
        {
            long now = _bridge.Clock.NowMicroseconds;
            if (now - _lastQueryMicros >= (long)(ServerQuery.RequerySeconds * 1_000_000))
            {
                QueryAll(listed: true);
            }

            query.Poll();
            changed |= MergeAnswers();
        }

        if (changed)
        {
            Touch();
        }
    }

    // ---- the status line --------------------------------------------------------------------------------------------

    /// <summary>What the browser is doing, as one line: the master's state and what the LAN and saved addresses gave.</summary>
    public string Status
    {
        get
        {
            OnlineMaster master = _bridge.Master;
            int lan = 0, saved = 0, listed = 0;
            foreach (BrowserRow row in _rows)
            {
                listed += row.Source == BrowserSource.Internet ? 1 : 0;
                lan += row.OnLan ? 1 : 0;
                saved += row.SavedAddress.Length > 0 ? 1 : 0;
            }

            string masterText = master.Url is null
                ? "No master server set: searching the LAN and the saved addresses."
                : master.State switch
                {
                    OnlineMasterState.Busy => "Asking the master server...",
                    OnlineMasterState.Failed => master.Error ?? "The master server did not answer.",
                    OnlineMasterState.Ready => (master.Motd is { Length: > 0 } motd ? motd + "  " : string.Empty) +
                        string.Create(CultureInfo.InvariantCulture, $"{listed} server(s) listed."),
                    _ => string.Empty,
                };
            string search = _query is null
                ? (Refusal.Length > 0 ? "  " + Refusal : string.Empty)
                : string.Create(CultureInfo.InvariantCulture, $"  LAN: {lan} found on port {LanPort}.") +
                    (_saved.Count > 0 ? string.Create(CultureInfo.InvariantCulture, $"  Saved: {saved} of {_saved.Count} answered.") : string.Empty);
            return masterText + search;
        }
    }

    // ---- what a row says --------------------------------------------------------------------------------------------

    /// <summary>Why this install cannot join a row (the Godot browser's Problem), or null when it can.</summary>
    public string? Problem(BrowserRow row)
    {
        if (row.Protocol != 0 && row.Protocol != _protocol)
        {
            return "another build";
        }

        if (row.Map.Length > 0)
        {
            if (_bridge.Catalog.Maps.FindMap(row.Map) is not { } map)
            {
                return "missing map " + row.Map;
            }

            if (row.Content != 0 && map.ContentHash != row.Content)
            {
                return "different version of " + row.Map;
            }
        }

        if (row.RequireAccounts && _bridge.Master.AccountState != OnlineAccountState.SignedIn)
        {
            return "sign in to join";
        }

        return row.MaxPlayers > 0 && row.Players >= row.MaxPlayers ? "full" : null;
    }

    /// <summary>The row's game address ("ip:port"), or empty when it never answered and the master gave none.</summary>
    public static string AddressOf(BrowserRow row)
    {
        if (row.Endpoint is { } endpoint)
        {
            return LiteNetLibTransport.FormatEndPoint(endpoint);
        }

        return row.Entry is { Address.Length: > 0 } e ? e.Address + ":" + e.Port.ToString(CultureInfo.InvariantCulture) : row.SavedAddress;
    }

    /// <summary>The row's players by side: "Spies: a, b     Mercenaries: c     Spectators: d" (the Godot browser's preview).</summary>
    public static string PlayersText(BrowserRow row)
    {
        if (row.PlayerList.Count == 0)
        {
            return row.Players > 0 ? (row.Name.Length > 0 ? row.Name : row.Key) + ": the player names are not known yet." : string.Empty;
        }

        var spies = new List<string>();
        var mercs = new List<string>();
        var spectators = new List<string>();
        var others = new List<string>();
        foreach (ServerInfoPlayer p in row.PlayerList)
        {
            (p.Team switch { 0 => spies, 1 => mercs, SessionProtocol.SpectatorTeam => spectators, _ => others }).Add(p.Name);
        }

        var parts = new List<string>();
        void Part(string title, List<string> names)
        {
            if (names.Count > 0)
            {
                parts.Add(title + ": " + string.Join(", ", names));
            }
        }

        Part("Spies", spies);
        Part("Mercenaries", mercs);
        Part("Spectators", spectators);
        Part("Players", others);
        return string.Join("     ", parts);
    }

    // ---- joining ----------------------------------------------------------------------------------------------------

    /// <summary>
    /// Joins the row at <paramref name="index"/> with a password (or null) and a side (0 spies, 1 mercs, 255 watch, -1
    /// the smaller side). False, with the reason in <see cref="Refusal"/>, when it cannot be joined from here; a full
    /// server is still tried (a place may have come free).
    /// </summary>
    public bool Join(int index, string? password, int preferredTeam)
    {
        Refusal = string.Empty;
        if (index < 0 || index >= _rows.Count)
        {
            return Refuse("That server is no longer on the list.");
        }

        BrowserRow row = _rows[index];
        if (Problem(row) is { } problem && problem != "full")
        {
            return Refuse(problem == "sign in to join" ? "Sign in to join this server." : $"Cannot join {row.Name}: {problem}.");
        }

        string? pass = string.IsNullOrEmpty(password) ? null : password;
        OnlineMaster master = _bridge.Master;
        if (row.Entry is { } entry && master.Url is not null)
        {
            master.JoinEntry(entry, pass, preferredTeam);
            return true;
        }

        if (row.Endpoint is not { } endpoint)
        {
            return Refuse($"{row.Name} has not answered; its address is not known.");
        }

        string address = LiteNetLibTransport.FormatEndPoint(endpoint);
        bool signedIn = master.AccountState == OnlineAccountState.SignedIn;
        if (row.MasterId.Length > 0 && signedIn && master.Url is not null)
        {
            // A LAN or saved server listed on the master: the master's ticket for it first, then straight there.
            master.JoinDirect(row.MasterId, JoinTarget.FromAddress(address), pass, preferredTeam, row.RequireAccounts, LoopbackOnly);
            return true;
        }

        if (row.RequireAccounts)
        {
            return Refuse("This server is for signed-in players, and your master server cannot give a ticket for it.");
        }

        _bridge.Join(address, pass, preferredTeam, LoopbackOnly);
        return true;
    }

    private bool Refuse(string reason)
    {
        Refusal = reason;
        Touch();
        return false;
    }

    // ---- merging ----------------------------------------------------------------------------------------------------

    /// <summary>Asks the LAN, the saved addresses and (with <paramref name="listed"/>) the master's direct servers.</summary>
    private void QueryAll(bool listed)
    {
        if (_query is not { } query)
        {
            return;
        }

        _lastQueryMicros = _bridge.Clock.NowMicroseconds;
        query.SearchLan(LanPort);
        foreach (string saved in _saved)
        {
            if (TryEndpoint(saved, out IPEndPoint? endpoint))
            {
                query.QueryServer(endpoint, "saved:" + saved);
            }
        }

        if (!listed)
        {
            return;
        }

        foreach (BrowserRow row in _rows)
        {
            if (DirectEndpoint(row.Entry) is { } direct)
            {
                query.QueryServer(direct, row.Key);
            }
        }
    }

    /// <summary>Takes the master's list into the rows when a new one arrived. True when the rows changed.</summary>
    private bool MergeList()
    {
        OnlineMaster master = _bridge.Master;
        if (master.ListRevision == _seenMasterList)
        {
            return false;
        }

        _seenMasterList = master.ListRevision;
        var listed = new HashSet<string>(StringComparer.Ordinal);
        foreach (ServerEntry e in master.Servers)
        {
            if (e.Kind == ServerKind.Hub)
            {
                // Hubs are the social space, joined from their own page, not game sessions.
                continue;
            }

            string key = "master:" + e.Id;
            listed.Add(key);
            BrowserRow row = Find(key) ?? Add(key, BrowserSource.Internet);
            row.Entry = e;
            row.Name = e.Name;
            row.Map = e.Map;
            row.Mode = e.Mode;
            row.Phase = e.Phase;
            row.Players = e.Players;
            row.MaxPlayers = e.MaxPlayers;
            row.Password = e.Password;
            row.Dedicated = e.Dedicated;
            row.RequireAccounts = e.RequireAccounts;
            row.Practice = e.Practice;
            row.Trusted = e.Trusted;
            row.Region = e.Region;
            if (row.Endpoint is null || row.PlayerList.Count == 0)
            {
                row.PlayerList = PlayersOf(e);
            }

            row.Reach = e.Reachability switch
            {
                Reachability.Direct => BrowserReach.Direct,
                Reachability.NeedsIntro => BrowserReach.Punch,
                Reachability.RelayOnly => BrowserReach.Relay,
                _ => BrowserReach.Checking,
            };
            MasterApi.TryParseHex(e.Protocol, out ulong protocol);
            MasterApi.TryParseHex(e.Content, out ulong content);
            row.Protocol = protocol;
            row.Content = content;
            if (DirectEndpoint(e) is { } direct)
            {
                _query?.QueryServer(direct, key);
            }
        }

        // Servers that left the master's list leave the browser's.
        _rows.RemoveAll(r => r.Source == BrowserSource.Internet && !listed.Contains(r.Key));
        return true;
    }

    private bool MergeAnswers()
    {
        bool changed = false;
        foreach (ServerQueryResult r in _answers)
        {
            if (r.Info.IsHub)
            {
                continue;
            }

            // A server found on the LAN and saved too is one row, by its game address, which remembers both ways.
            bool listedTag = r.Tag is { } tag && tag.StartsWith("master:", StringComparison.Ordinal);
            string key = listedTag ? r.Tag! : "server:" + LiteNetLibTransport.FormatEndPoint(r.GameEndPoint);
            BrowserRow? row = Find(key);
            if (row is null)
            {
                if (listedTag)
                {
                    // An answer for a listed server the master no longer lists.
                    continue;
                }

                row = Add(key, r.FromLan ? BrowserSource.Lan : BrowserSource.Saved);
            }

            if (!listedTag)
            {
                row.OnLan |= r.FromLan;
                if (r.Tag is { } savedTag && savedTag.StartsWith("saved:", StringComparison.Ordinal))
                {
                    row.SavedAddress = savedTag["saved:".Length..];
                }

                row.Source = row.OnLan ? BrowserSource.Lan : BrowserSource.Saved;
            }

            row.Seen = r;
            row.PingMs = r.PingMs;
            row.Endpoint = r.GameEndPoint;
            row.Name = r.Info.Name;
            row.Map = r.Info.Map;
            row.Mode = r.Info.Mode;
            row.Phase = r.Info.Phase;
            row.Players = r.Info.Players;
            row.MaxPlayers = r.Info.MaxPlayers;
            row.Password = r.Info.HasPassword;
            row.Dedicated = r.Info.Dedicated;
            row.Protocol = r.Info.ProtocolHash;
            row.Content = r.Info.ContentHash;
            row.RequireAccounts = r.Info.RequireAccounts || (row.Entry?.RequireAccounts ?? false);
            row.Practice = r.Info.Practice;
            row.MasterId = r.Info.MasterId;
            row.PlayerList = r.Info.PlayerList;
            if (row.Source == BrowserSource.Lan)
            {
                row.Reach = BrowserReach.Lan;
            }
            else if (row.Reach == BrowserReach.Checking)
            {
                row.Reach = BrowserReach.Direct;
            }

            changed = true;
        }

        _answers.Clear();
        return changed;
    }

    private BrowserRow? Find(string key) => _rows.Find(r => r.Key == key);

    private BrowserRow Add(string key, BrowserSource source)
    {
        var row = new BrowserRow { Key = key, Source = source };
        _rows.Add(row);
        return row;
    }

    /// <summary>The game endpoint of a listed server the master says can be reached directly, or null.</summary>
    private static IPEndPoint? DirectEndpoint(ServerEntry? e) =>
        e is { Reachability: Reachability.Direct } && IPAddress.TryParse(e.Address, out IPAddress? ip) && e.Port is > 0 and < 65536
            ? new IPEndPoint(ip, e.Port)
            : null;

    /// <summary>The master's names with their sides (a host that sends no sides: every name without one).</summary>
    private static List<ServerInfoPlayer> PlayersOf(ServerEntry e)
    {
        var list = new List<ServerInfoPlayer>();
        for (int i = 0; i < e.PlayerNames.Count; i++)
        {
            list.Add(new ServerInfoPlayer(e.PlayerNames[i], i < e.PlayerTeams.Count ? e.PlayerTeams[i] : NoSide));
        }

        return list;
    }

    private static bool TryEndpoint(string address, [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out IPEndPoint? endpoint)
    {
        try
        {
            endpoint = LiteNetLibTransport.Resolve(JoinTarget.FromAddress(address).Attempts[0].Address);
            return true;
        }
        catch (Exception e) when (e is FormatException or SocketException or ArgumentException)
        {
            endpoint = null;
            return false;
        }
    }

    private void Touch()
    {
        Revision++;
    }

    /// <summary>The protocol hash of this build as the master writes it (for the probe).</summary>
    internal string ProtocolHex => _protocolHex;

    public void Dispose() => Close();
}
