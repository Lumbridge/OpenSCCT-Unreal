using System;
using System.Collections.Generic;
using System.Globalization;
using System.Net;
using System.Threading.Tasks;
using OpenScct.Net.Discovery;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>A hub of the master's hub list, with its ping and why it cannot be joined.</summary>
public sealed class ListedHub
{
    public required ServerEntry Entry { get; init; }

    /// <summary>The lowest of the last answers' round trips in ms, -1 before an answer.</summary>
    public int PingMs { get; set; } = -1;

    /// <summary>"another build", "another hub map" or "full"; null when it can be joined.</summary>
    public string? Problem { get; set; }
}

/// <summary>
/// The master's side of hubs (the Godot client's HubScreen list and HubService.JoinListed): the master's hub list
/// with each hub's ping, joining a listed hub through the master's join answer (its own network address when the
/// player shares its router, its public address, then the master's relay, tried in turn; a signed-in player's ticket
/// lets him in under his account), and the listing of the hub this PC hosts.
/// <para>It uses the online bridge's master client (<see cref="OnlineMaster.Client"/>), so the hub list, the server
/// list and the account share one master and one sign-in. The requests run on worker threads; their answers are
/// picked up at the start of <see cref="Update"/>.</para>
/// </summary>
public sealed partial class HubBridge
{
    private readonly List<ListedHub> _hubs = new();
    private readonly Queue<JoinAttempt> _attempts = new();
    private Task<ServerListResponse>? _list;
    private HubListState _listState;
    private string _listMessage = string.Empty;
    private ServerQuery? _query;
    private long _nextQuery;
    private string _yourAddress = string.Empty;

    // A hub from the master's list: the master's answer being waited for, its id and name, and its ticket.
    private Task<JoinResponse>? _pendingJoin;
    private string? _masterId;
    private string? _masterName;
    private string? _ticket;
    private bool _requireAccount;
    private bool _hostListed = true;

    public HubListState ListState => _listState;

    /// <summary>What the list says, as the hub page shows it under the list.</summary>
    public string ListMessage => _listMessage;

    /// <summary>The master's hubs: official hubs first, then the nearest (lowest ping, unknown last), then the fullest.</summary>
    public IReadOnlyList<ListedHub> Hubs => _hubs;

    /// <summary>The visit came in through the master's join answer for a listed hub.</summary>
    public bool IsListedVisit => _masterId is not null && Client is not null;

    /// <summary>The master is being asked the way to a listed hub.</summary>
    public bool AskingMaster => _pendingJoin is not null;

    /// <summary>A hub hosted from now on lists itself in the master's hub list (the Godot client's "Show it in the hub list").</summary>
    public bool HostListed
    {
        get => _hostListed;
        set => _hostListed = value;
    }

    private static OnlineMaster Master => OnlineBridge.Instance.Master;

    /// <summary>
    /// Sets the master the hub list comes from when the online side has not set one yet (the online pages were not
    /// opened this run), and the saved sign-in when it has none. A master or token already set is kept.
    /// </summary>
    public void ConfigureMaster(string? url, string? inviteKey, string? token)
    {
        OnlineMaster master = Master;
        if (master.Url is null && !string.IsNullOrWhiteSpace(url))
        {
            master.Configure(url.Trim(), inviteKey);
            Log(master.Url is null ? $"the master server address {url} is not valid: {master.Error}" : $"master server {master.Url}");
        }

        if (master.Url is not null && master.AccountToken is null && !string.IsNullOrWhiteSpace(token))
        {
            master.SetAccountToken(token);
        }
    }

    // ---- the list -----------------------------------------------------------------------------------------------

    /// <summary>Asks the master for its hubs (game servers are listed apart). The list state is Busy until the answer is in.</summary>
    public void RequestList()
    {
        StopPings();
        _hubs.Clear();
        if (Master.Client is not { } client)
        {
            _list = null;
            _listState = HubListState.Failed;
            _listMessage = Master.Url is null && Master.Error is { } invalid
                ? "The master server address is not valid: " + invalid
                : "No master server is set for this game: join a hub by its address.";
            Touch();
            return;
        }

        _list = client.ListAsync(kind: ServerKind.Hub);
        _listState = HubListState.Busy;
        _listMessage = "Asking the master server for hubs...";
        Log("asking " + Master.Url + " for its hub list");
        Touch();
    }

    /// <summary>Picks up the master's answers and the pings that arrived since the last frame.</summary>
    private void PumpMaster()
    {
        if (_list is { IsCompleted: true } list)
        {
            _list = null;
            Listed(list);
        }

        if (_query is { } query)
        {
            // The pings stay current while the list is up (a first answer is often slow).
            long now = _clock.NowMicroseconds;
            if (now >= _nextQuery)
            {
                _nextQuery = now + (long)(ServerQuery.RequerySeconds * 1_000_000);
                QueryAll(query);
            }

            query.Poll();
            if (TakePings(query))
            {
                Sort();
                Touch();
            }
        }

        if (_pendingJoin is { IsCompleted: true } answer)
        {
            _pendingJoin = null;
            Answered(answer);
        }
    }

    private void Listed(Task<ServerListResponse> list)
    {
        if (!Succeeded(list, out string? error))
        {
            _listState = HubListState.Failed;
            _listMessage = "The master server's hub list did not come: " + error;
            Log(_listMessage);
            Touch();
            return;
        }

        _hubs.Clear();
        _yourAddress = list.Result.YourAddress;
        foreach (ServerEntry e in list.Result.Servers)
        {
            _hubs.Add(new ListedHub { Entry = e, Problem = Problem(e) });
        }

        Sort();
        StopPings();
        try
        {
            _query = new ServerQuery(new StopwatchClock());
            _nextQuery = 0;
        }
        catch (InvalidOperationException)
        {
            // No pings without a query socket; the list still joins.
            _query = null;
        }

        _listState = HubListState.Ready;
        _listMessage = _hubs.Count == 0
            ? "No hub is listed right now. Host one, or join one by its address."
            : string.Create(CultureInfo.InvariantCulture, $"{_hubs.Count} hub(s) listed.");
        Log($"hub list: {_hubs.Count} hub(s)" + (_hubs.Count == 0 ? string.Empty : ": " + string.Join(", ", _hubs.ConvertAll(Describe))));
        Touch();
    }

    private static string Describe(ListedHub h) =>
        $"{h.Entry.Name} ({(h.Entry.Trusted ? "official" : h.Entry.Dedicated ? "dedicated" : "player")}"
        + (h.Entry.Region.Length > 0 ? ", " + h.Entry.Region : string.Empty)
        + string.Create(CultureInfo.InvariantCulture, $", {h.Entry.Players}/{h.Entry.MaxPlayers}")
        + (h.Problem is null ? string.Empty : ", " + h.Problem) + ")";

    /// <summary>
    /// Asks every listed hub for its info (the ping column), at its public address, and at its own network addresses
    /// when it shares the player's public address (behind the same router its public address often does not answer).
    /// </summary>
    private void QueryAll(ServerQuery query)
    {
        foreach (ListedHub h in _hubs)
        {
            if (IPAddress.TryParse(h.Entry.Address, out IPAddress? ip) && h.Entry.Port > 0)
            {
                query.QueryServer(new IPEndPoint(ip, h.Entry.Port), h.Entry.Id);
            }

            if (_yourAddress.Length > 0 && h.Entry.Address == _yourAddress)
            {
                foreach (string lan in h.Entry.LanAddresses)
                {
                    if (IPEndPoint.TryParse(lan, out IPEndPoint? end) && end.Port > 0)
                    {
                        query.QueryServer(end, h.Entry.Id);
                    }
                }
            }
        }
    }

    private bool TakePings(ServerQuery query)
    {
        bool changed = false;
        foreach (ServerQueryResult r in query.Results)
        {
            foreach (ListedHub h in _hubs)
            {
                // The lowest answer of the hub's addresses counts (its own network's, when it is on the player's).
                if (h.Entry.Id == r.Tag && (h.PingMs < 0 || r.PingMs < h.PingMs))
                {
                    h.PingMs = r.PingMs;
                    changed = true;
                }
            }
        }

        return changed;
    }

    /// <summary>Official hubs first, then the nearest (lowest ping, unknown last), then the fullest.</summary>
    private void Sort() => _hubs.Sort((a, b) =>
    {
        if (a.Entry.Trusted != b.Entry.Trusted)
        {
            return b.Entry.Trusted.CompareTo(a.Entry.Trusted);
        }

        int pa = a.PingMs < 0 ? int.MaxValue : a.PingMs;
        int pb = b.PingMs < 0 ? int.MaxValue : b.PingMs;
        return pa != pb ? pa.CompareTo(pb) : b.Entry.Players.CompareTo(a.Entry.Players);
    });

    /// <summary>Why a listed hub cannot be joined from this game, or null.</summary>
    private string? Problem(ServerEntry e)
    {
        if (MasterApi.TryParseHex(e.Protocol, out ulong protocol) && protocol != 0 && protocol != Classes.ComputeProtocolHash())
        {
            return "another build";
        }

        if (MasterApi.TryParseHex(e.Content, out ulong content) && content != 0 &&
            Catalog.Maps.FindMap(e.Map) is { } map && map.ContentHash != content)
        {
            return "another hub map";
        }

        return e.MaxPlayers > 0 && e.Players >= e.MaxPlayers ? "full" : null;
    }

    private void StopPings()
    {
        _query?.Dispose();
        _query = null;
    }

    // ---- joining a listed hub -----------------------------------------------------------------------------------

    /// <summary>
    /// Joins the hub at <paramref name="index"/> of <see cref="Hubs"/>: asks the master how to reach it, then tries
    /// the ways in it names one after another (HubService.JoinListed; a NAT punch is left out, the relay covers those
    /// hosts). False with <see cref="Error"/> set when it cannot start.
    /// </summary>
    public bool JoinListed(int index, string? name, int team)
    {
        if (index < 0 || index >= _hubs.Count)
        {
            Fail("That hub is no longer on the list. Refresh it.");
            return false;
        }

        ListedHub hub = _hubs[index];
        if (hub.Problem is { } problem)
        {
            Fail($"Cannot join {hub.Entry.Name}: {problem}.");
            return false;
        }

        if (Master.Client is not { } client)
        {
            Fail("No master server is set for this game: join a hub by its address.");
            return false;
        }

        Leave();
        ForgetListed();
        _masterId = hub.Entry.Id;
        _masterName = hub.Entry.Name;
        _requireAccount = hub.Entry.RequireAccounts;
        _name = string.IsNullOrWhiteSpace(name) ? "Player" : name.Trim();
        _team = team == 1 ? 1 : 0;
        _pendingJoin = client.JoinAsync(_masterId);
        _status = $"Asking the master server the way to {_masterName}...";
        Log($"joining listed hub {_masterName} ({_masterId}) as '{_name}' ({(_team == 1 ? "merc" : "spy")}){(Master.AccountToken is null ? string.Empty : ", signed in")}");
        Touch();
        return true;
    }

    /// <summary>The master answered the way to a listed hub: the attempts are queued and the first one starts.</summary>
    private void Answered(Task<JoinResponse> answer)
    {
        if (!Succeeded(answer, out string? error))
        {
            Fail($"The master server could not say how to reach {_masterName}: {error}");
            return;
        }

        JoinResponse join = answer.Result;
        _ticket = join.Ticket is { Length: > 0 } ticket ? ticket : null;
        if ((_requireAccount || join.RequireAccounts) && _ticket is null)
        {
            Fail(join.TicketProblem ?? $"{_masterName} takes signed-in players only. Sign in on the server list page, then join it again.");
            return;
        }

        if (_ticket is null && join.TicketProblem is { Length: > 0 } problem && Master.AccountToken is not null)
        {
            Log("no ticket for this hub: " + problem);
        }

        _attempts.Clear();
        foreach (JoinAttempt a in JoinTarget.FromJoin(join).Attempts)
        {
            if (a.Kind is ConnectKind.Direct or ConnectKind.Relay)
            {
                _attempts.Enqueue(a);
            }
        }

        Log($"the master answered: {_attempts.Count} way(s) in{(_ticket is null ? string.Empty : ", with a ticket")}");
        if (!TryNextAttempt(null))
        {
            Fail($"The master server knows no way to reach {_masterName}.");
        }
    }

    /// <summary>Connects with the next way into a listed hub (the last one never answered); false when none is left.</summary>
    private bool TryNextAttempt(string? failed)
    {
        if (!_attempts.TryDequeue(out JoinAttempt? attempt))
        {
            return false;
        }

        if (failed is not null)
        {
            Log($"that way failed ({failed}), trying the next");
        }

        if (Client is { } old)
        {
            Client = null;
            try
            {
                old.Session.Disconnect();
            }
            finally
            {
                old.Dispose();
            }
        }

        Log("trying " + attempt.Describe());
        Connect(attempt.Address, _name, _team);
        _status = $"Connecting to {_masterName} ({attempt.Describe()})...";
        Touch();
        return true;
    }

    private void ForgetListed()
    {
        _pendingJoin = null;
        _masterId = null;
        _masterName = null;
        _ticket = null;
        _requireAccount = false;
        _attempts.Clear();
    }

    /// <summary>The master listing of a hub this PC hosts, or null (not wanted, this PC only, or no master set).</summary>
    private MasterLinkSettings? HostListing(bool loopbackOnly)
    {
        if (loopbackOnly || !_hostListed || Master.Url is not { Length: > 0 } url)
        {
            return null;
        }

        return new MasterLinkSettings { Url = url, InviteKey = Master.InviteKey };
    }

    /// <summary>True when the request went through; otherwise <paramref name="error"/> is a line a player can read.</summary>
    private static bool Succeeded(Task task, out string? error)
    {
        if (task.IsCompletedSuccessfully)
        {
            error = null;
            return true;
        }

        Exception? e = task.Exception?.GetBaseException();
        error = e is MasterException ? e.Message : task.IsCanceled ? "the request was cancelled." : e?.Message ?? "no answer.";
        return false;
    }
}
