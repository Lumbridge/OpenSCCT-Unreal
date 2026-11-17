using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;
using OpenScct.Net.Discovery;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// The master server as a client uses it (docs/ONLINE.md "Master server" and "Accounts"; the Godot client's
/// ServerBrowserScreen, AccountService and AccountScreen): the list of servers, how to reach a listed one (direct
/// address, NAT punch, relay, and the signed-in player's ticket for it), and signing in through the browser.
/// <para>The requests run on worker threads; their answers are picked up in <see cref="Pump"/>, which
/// <see cref="OnlineBridge.Update"/> calls on the game thread. Nothing here blocks a frame.</para>
/// </summary>
public sealed class OnlineMaster : IDisposable
{
    /// <summary>Milliseconds between two polls of a sign-in (the Godot client's AccountScreen.PollIntervalMs).</summary>
    private const long PollIntervalMs = 2000;

    private readonly OnlineBridge _bridge;
    private readonly List<ServerEntry> _servers = new();
    private MasterClient? _client;
    private Task<ServerListResponse>? _list;
    private PendingJoin? _join;

    private Task<LinkStartResponse>? _linkStart;
    private Task<LinkPollResponse>? _linkPoll;
    private Task<AccountInfo>? _account;
    private LinkStartResponse? _link;
    private long _linkExpires;
    private long _nextPoll;
    private readonly long _epoch = Stopwatch.GetTimestamp();

    internal OnlineMaster(OnlineBridge bridge)
    {
        _bridge = bridge;
    }

    /// <summary>The master's address (never the original game's or Reloaded's), or null before <see cref="Configure"/>.</summary>
    public string? Url { get; private set; }

    /// <summary>The master's client, carrying the signed-in token, for the hub's list and joins (Hub/HubBridge.Master.cs); null before <see cref="Configure"/>.</summary>
    internal MasterClient? Client => _client;

    /// <summary>The invite key given with the master's address (a hub this PC hosts lists itself with it), or null.</summary>
    internal string? InviteKey { get; private set; }

    public OnlineMasterState State { get; private set; }

    /// <summary>Why the last request failed, as a player reads it, or null.</summary>
    public string? Error { get; private set; }

    /// <summary>The last list, as the master sent it.</summary>
    public IReadOnlyList<ServerEntry> Servers => _servers;

    /// <summary>The master's message of the day from the last list, or null.</summary>
    public string? Motd { get; private set; }

    public OnlineAccountState AccountState { get; private set; }

    public string? AccountError { get; private set; }

    /// <summary>The signed-in token (to save and give back with <see cref="SetAccountToken"/>), or null.</summary>
    public string? AccountToken { get; private set; }

    /// <summary>The account's claimed name: a signed-in player joins under it (the server checks it against the ticket).</summary>
    public string? AccountName { get; private set; }

    /// <summary>The page to open in the browser and the code it shows, while a sign-in waits.</summary>
    public string? SignInUrl => _link?.Url;

    public string? SignInCode => _link?.Code;

    /// <summary>
    /// Sets the master to talk to and its invite key (empty for a public master). The address comes from the game's
    /// settings; OpenSCCT's own master is https://master.lumbridge.co.uk with the invite key every build ships.
    /// </summary>
    public void Configure(string url, string? inviteKey)
    {
        string? token = AccountToken;
        _client?.Dispose();
        _client = null;
        _list = null;
        _join = null;
        Url = null;

        // Answers of the old master's client are not waited for: they would arrive as failures of a disposed
        // client and show as a refused account or sign-in on the new master. A sign-in in progress starts over.
        _account = null;
        _linkStart = null;
        _linkPoll = null;
        _link = null;
        if (AccountState is OnlineAccountState.SigningIn or OnlineAccountState.Failed)
        {
            AccountState = OnlineAccountState.SignedOut;
            AccountError = null;
        }

        try
        {
            _client = new MasterClient(url, string.IsNullOrEmpty(inviteKey) ? null : inviteKey) { SessionToken = token };
            Url = url;
            InviteKey = string.IsNullOrEmpty(inviteKey) ? null : inviteKey;
            State = OnlineMasterState.Idle;
            Error = null;
            if (token is not null)
            {
                // A token given before the master was set: the account is asked for now.
                _account = _client.AccountAsync();
            }
        }
        catch (ArgumentException e)
        {
            // Not an http or https address: a setting to correct, not a fault of the bridge.
            State = OnlineMasterState.Failed;
            Error = e.Message;
        }

        _bridge.Touch();
    }

    // ---- the list -----------------------------------------------------------------------------------------------

    /// <summary>Asks the master for its game servers. The state is Busy until the answer is in.</summary>
    public void RequestList()
    {
        if (Need() is not { } client)
        {
            return;
        }

        // Every server is listed, not only this build's: the list says which ones cannot be joined and why.
        _list = client.ListAsync();
        State = OnlineMasterState.Busy;
        Error = null;
        _bridge.Touch();
    }

    /// <summary>
    /// Joins a server of the last list (ServerBrowserScreen's join): asks the master how to reach it, then starts the
    /// visit through the attempts its reachability suggests. A signed-in player gets the master's ticket for it.
    /// </summary>
    public void JoinListed(int index, string? password, int preferredTeam)
    {
        if (Need() is not { } client)
        {
            _bridge.FailJoin(Error ?? "No master server is set.");
            return;
        }

        if (index < 0 || index >= _servers.Count)
        {
            _bridge.FailJoin("That server is no longer on the list.");
            return;
        }

        ServerEntry entry = _servers[index];
        _bridge.BeginListedJoin();
        _join = new PendingJoin(client.JoinAsync(entry.Id), entry, password, preferredTeam);
    }

    /// <summary>Drops a listed join still waiting for the master (the player left the connecting screen).</summary>
    internal void CancelJoin() => _join = null;

    // ---- the account --------------------------------------------------------------------------------------------

    /// <summary>
    /// Signs in with a token saved from an earlier sign-in: the master is asked for the account, and the state is
    /// SignedIn once it answered (Failed when the token no longer works).
    /// </summary>
    public void SetAccountToken(string? token)
    {
        _link = null;
        _linkStart = null;
        _linkPoll = null;
        _account = null;
        AccountName = null;
        AccountError = null;
        AccountToken = string.IsNullOrWhiteSpace(token) ? null : token;
        AccountState = OnlineAccountState.SignedOut;
        if (_client is { } client)
        {
            client.SessionToken = AccountToken;
            if (AccountToken is not null)
            {
                _account = client.AccountAsync();
            }
        }

        _bridge.Touch();
    }

    /// <summary>
    /// Starts a sign-in (AccountScreen): the master answers with a page to open in the player's browser
    /// (<see cref="SignInUrl"/>, it goes on to Discord), and the bridge polls until the player finished there.
    /// </summary>
    public void BeginSignIn()
    {
        if (Need() is not { } client)
        {
            AccountState = OnlineAccountState.Failed;
            AccountError = Error ?? "No master server is set.";
            _bridge.Touch();
            return;
        }

        SetAccountToken(null);
        _linkStart = client.StartLinkAsync();
        AccountState = OnlineAccountState.SigningIn;
        _bridge.Touch();
    }

    /// <summary>Forgets the account here (the saved token should be deleted too). The master's sign-in is not ended.</summary>
    public void SignOut() => SetAccountToken(null);

    /// <summary>A newer description of the signed-in account (Social/AccountService: a claimed or changed name): joins use its name.</summary>
    internal void Remember(AccountInfo account)
    {
        if (AccountToken is not null && AccountName != account.Name)
        {
            AccountName = account.Name;
            _bridge.Touch();
        }
    }

    // ---- answers ------------------------------------------------------------------------------------------------

    /// <summary>Picks up the answers that arrived since the last frame.</summary>
    internal void Pump()
    {
        if (_list is { IsCompleted: true } list)
        {
            _list = null;
            if (Succeeded(list, out string? error))
            {
                _servers.Clear();
                _servers.AddRange(list.Result.Servers);
                Motd = list.Result.Motd;
                State = OnlineMasterState.Ready;
                Error = null;
            }
            else
            {
                State = OnlineMasterState.Failed;
                Error = error;
            }

            _bridge.Touch();
        }

        if (_join is { Answer.IsCompleted: true } join)
        {
            _join = null;
            Joined(join);
        }

        PumpAccount();
    }

    private void Joined(PendingJoin join)
    {
        if (!Succeeded(join.Answer, out string? error))
        {
            _bridge.FailJoin(error!);
            return;
        }

        JoinResponse answer = join.Answer.Result;
        if ((answer.RequireAccounts || join.Entry.RequireAccounts) && answer.Ticket is null)
        {
            _bridge.FailJoin(answer.TicketProblem ?? "This server takes signed-in players only. Sign in to join it.");
            return;
        }

        _bridge.Join(JoinTarget.FromJoin(answer), join.Password, answer.Ticket, join.PreferredTeam);
    }

    private void PumpAccount()
    {
        if (_account is { IsCompleted: true } account)
        {
            _account = null;
            if (Succeeded(account, out string? error))
            {
                AccountName = account.Result.Name;
                AccountState = OnlineAccountState.SignedIn;
                AccountError = null;
            }
            else
            {
                AccountState = OnlineAccountState.Failed;
                AccountError = error;
            }

            _bridge.Touch();
        }

        if (_linkStart is { IsCompleted: true } start)
        {
            _linkStart = null;
            if (Succeeded(start, out string? error))
            {
                _link = start.Result;
                _linkExpires = _link.ExpiresSeconds > 0 ? NowMs + _link.ExpiresSeconds * 1000L : 0;
                _nextPoll = NowMs + PollIntervalMs;
            }
            else
            {
                AccountState = OnlineAccountState.Failed;
                AccountError = error;
            }

            _bridge.Touch();
        }

        if (_link is { } link && _linkPoll is null && _client is { } client)
        {
            if (_linkExpires != 0 && NowMs >= _linkExpires)
            {
                _link = null;
                AccountState = OnlineAccountState.Failed;
                AccountError = "The sign-in code expired. Try again.";
                _bridge.Touch();
            }
            else if (NowMs >= _nextPoll)
            {
                _linkPoll = client.PollLinkAsync(link.Code, link.PollSecret);
            }
        }

        if (_linkPoll is { IsCompleted: true } poll)
        {
            _linkPoll = null;
            _nextPoll = NowMs + PollIntervalMs;
            if (_link is not null)
            {
                Polled(poll);
            }
        }
    }

    private void Polled(Task<LinkPollResponse> poll)
    {
        // A poll that did not get through is tried again; only the master's own answer ends the wait.
        if (!Succeeded(poll, out _))
        {
            return;
        }

        LinkPollResponse answer = poll.Result;
        switch (answer.Status)
        {
            case LinkStatus.Pending:
                return;
            case LinkStatus.Done when answer.SessionToken is { Length: > 0 } token:
                _link = null;
                AccountToken = token;
                AccountName = answer.Account?.Name;
                AccountState = OnlineAccountState.SignedIn;
                AccountError = null;
                if (_client is { } client)
                {
                    client.SessionToken = token;
                }

                break;
            default:
                _link = null;
                AccountState = OnlineAccountState.Failed;
                AccountError = answer.Error ?? (answer.Status == LinkStatus.Expired ? "The sign-in code expired. Try again." : "The sign-in was refused.");
                break;
        }

        _bridge.Touch();
    }

    private long NowMs => (long)Stopwatch.GetElapsedTime(_epoch).TotalMilliseconds;

    private MasterClient? Need()
    {
        if (_client is null)
        {
            State = OnlineMasterState.Failed;
            Error = "No master server is set.";
            _bridge.Touch();
        }

        return _client;
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
        error = e is MasterException ? e.Message : task.IsCanceled ? "The request to the master server was cancelled." : "The master server request failed: " + (e?.Message ?? "unknown error");
        return false;
    }

    public void Dispose()
    {
        _client?.Dispose();
        _client = null;
        _list = null;
        _join = null;
    }

    private sealed record PendingJoin(Task<JoinResponse> Answer, ServerEntry Entry, string? Password, int PreferredTeam);
}
