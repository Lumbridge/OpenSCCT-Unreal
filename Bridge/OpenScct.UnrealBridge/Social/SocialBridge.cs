using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading.Tasks;
using OpenScct.Net.Discovery;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// The game's social services for the whole run (the Godot client starts them from App and Boot): the Discord status
/// with a friend's Join (<see cref="DiscordPresence"/>), the website's Join button (<see cref="WebsiteJoin"/>) and the
/// master's presence lease (<see cref="ClientPresence"/>). Unreal's UScctSocialSubsystem drives it through
/// <see cref="SocialExports"/>: it describes the player's state each second, says each frame whether the front end is
/// up to take a join, and carries out the joins this class asks for (<see cref="Request"/>), since only Unreal knows
/// its pages and levels. The joins through the master by id (a friend's listed server, a website join) run here, on
/// the online bridge's visit, which Unreal's online subsystem follows as it follows any visit.
/// <para>Single-threaded except the log, which the pipe's listener also writes from its worker thread.</para>
/// </summary>
public sealed class SocialBridge : IDisposable
{
    private const int RequestCapacity = 32;
    private const int LogCapacity = 256;

    private readonly ConcurrentQueue<string> _log = new();
    private readonly Queue<(SocialRequest Request, string Value, string Text)> _requests = new();
    private readonly Dictionary<string, string> _titles = new(StringComparer.OrdinalIgnoreCase);
    private DiscordJoinSecret? _discordJoin;
    private PendingMasterJoin? _masterJoin;
    private string? _launchExe;
    private string? _launchCommand;
    private bool _started;

    private SocialBridge()
    {
        Discord = new DiscordPresence(Log, join => _discordJoin = join);
        Web = new WebsiteJoin(this);
        Lease = new ClientPresence(this);
        Account = new AccountService(this);
        Dashboard = new MenuDashboard(this);
    }

    /// <summary>The one social bridge of the process, made on first use.</summary>
    public static SocialBridge Instance => s_instance ??= new SocialBridge();

    private static SocialBridge? s_instance;

    internal static SocialBridge? Existing => s_instance;

    public DiscordPresence Discord { get; }

    public WebsiteJoin Web { get; }

    public ClientPresence Lease { get; }

    /// <summary>The signed-in account, its progress and the settings sync (the Account page's table: AccountExports).</summary>
    public AccountService Account { get; }

    /// <summary>The main menu's live information: players online, sessions, changelog, master version (DashboardExports).</summary>
    public MenuDashboard Dashboard { get; }

    /// <summary>The state Unreal last described, as Discord would show it (whether the status is on or not).</summary>
    public PresenceState? LastState { get; private set; }

    public int RequestsWaiting => _requests.Count;

    public int LogWaiting => _log.Count;

    // ---- the run --------------------------------------------------------------------------------------------------

    /// <summary>
    /// Starts the services that run regardless of the Discord option, once: the website join (with the id of the
    /// game's command line, or null) and the presence lease unless <paramref name="lease"/> is false.
    /// </summary>
    public void Start(string? webJoinId, bool lease)
    {
        if (_started)
        {
            return;
        }

        _started = true;
        Lease.Enabled = lease;
        Log("Social: started (presence lease " + (lease ? "every " + ClientPresence.IntervalMs / 1000 + " s" : "off") + ")");
        Web.Start(webJoinId);
    }

    /// <summary>The Online page's "Discord status": on starts the Discord connection, off stops it and clears the status.</summary>
    public void SetDiscord(bool enabled, string? launchExe, string? launchCommand)
    {
        _launchExe = launchExe;
        _launchCommand = launchCommand;
        if (enabled && !Discord.Running)
        {
            Discord.Start(_launchExe, _launchCommand);
        }
        else if (!enabled && Discord.Running)
        {
            Discord.Stop();
        }
    }

    /// <summary>The player's state as Unreal sees it now: what Discord shows within a second.</summary>
    public PresenceState SetPresence(in SocialPresence presence, string? server, string? map, string? joinValue)
    {
        PresenceState state = PresenceSnapshot.Build(presence, server, map, joinValue, MapTitle);
        LastState = state;
        Discord.Show(state);
        return state;
    }

    /// <summary>A friend's Join as Discord would hand it (unattended checks: -ScctDiscordJoin).</summary>
    public bool SimulateDiscordJoin(string? secret)
    {
        DiscordJoinSecret? join = DiscordJoinSecret.Parse(secret);
        Log(join is null ? "Discord: ignored a Join this build does not understand (" + secret + ")" : "Discord: a friend's Join to " + join.Kind + " (simulated)");
        _discordJoin = join;
        return join is not null;
    }

    /// <summary>Once a frame on the game thread: Discord, a friend's Join, the website join, the lease, a master join.</summary>
    public void Update(double delta, SocialReadiness ready)
    {
        Discord.Update(delta);
        TryDiscordJoin(ready);
        Web.Update(delta, ready);
        Lease.Update();
        PumpMasterJoin();
        Account.Update();
        Dashboard.Update();
    }

    public SocialStatus Status => new()
    {
        Discord = (int)Discord.State,
        DiscordCanJoin = Discord.CanJoin ? 1 : 0,
        WebJoinListening = Web.Listening ? 1 : 0,
        WebJoinPending = Web.Pending ? 1 : 0,
        PresenceLeases = Lease.Accepted,
        RequestsWaiting = _requests.Count,
        LogWaiting = _log.Count,
    };

    // ---- a friend's Join ------------------------------------------------------------------------------------------

    /// <summary>
    /// Starts a friend's Join once the player is in the menus (a running match waits until it is left), the way the
    /// server list, the address page or the hub page would (DiscordPresence.TryJoin).
    /// </summary>
    private void TryDiscordJoin(SocialReadiness ready)
    {
        if (_discordJoin is not { } join || !ready.HasFlag(SocialReadiness.InMenus))
        {
            return;
        }

        _discordJoin = null;
        SocialRequestFlags flags = SocialRequestFlags.FromDiscord | (join.NeedsPassword ? SocialRequestFlags.NeedsPassword : SocialRequestFlags.None);
        switch (join.Kind)
        {
            case DiscordJoinKind.Hub:
                Request(SocialRequestKind.JoinHub, SocialRequestFlags.FromDiscord, join.Value, join.Value);
                break;
            case DiscordJoinKind.Master:
                Request(SocialRequestKind.JoinMaster, flags, join.Value, "your friend's server");
                break;
            default:
                Request(SocialRequestKind.JoinAddress, flags, join.Value, join.Value);
                break;
        }
    }

    // ---- joins through the master by id ---------------------------------------------------------------------------

    /// <summary>
    /// Joins the server the master lists as <paramref name="id"/> (the Godot client's server list joining a Discord or
    /// website id): the master's join answer, with the signed-in player's ticket, starts the online visit.
    /// <paramref name="requireAccount"/> refuses the visit when the master gives no ticket. The online state shows
    /// Connecting meanwhile, and Failed with the master's refusal.
    /// </summary>
    public bool JoinMaster(string id, string? name, string? password, int team, bool requireAccount)
    {
        OnlineBridge online = OnlineBridge.Instance;
        string label = string.IsNullOrWhiteSpace(name) ? id : name.Trim();
        if (online.Master.Client is not { } client)
        {
            Log("Master join: no master server is set; " + label + " cannot be joined");
            online.FailJoin(online.Master.Error ?? "No master server is set.");
            return false;
        }

        online.BeginListedJoin();
        _masterJoin = new PendingMasterJoin(client.JoinAsync(id.Trim()), id.Trim(), label, string.IsNullOrEmpty(password) ? null : password, team, requireAccount);
        Log($"Master join: asking {online.Master.Url} the way to {label} ({id.Trim()}){(online.Master.AccountToken is null ? ", not signed in" : ", signed in")}");
        return true;
    }

    private void PumpMasterJoin()
    {
        if (_masterJoin is not { Answer.IsCompleted: true } join)
        {
            return;
        }

        _masterJoin = null;
        OnlineBridge online = OnlineBridge.Instance;
        if (online.Client is not null || online.State != OnlineState.Connecting)
        {
            // The player left the connecting page, or joined something else meanwhile.
            Log("Master join: the answer for " + join.Label + " came after the join was given up; dropped");
            return;
        }

        if (!join.Answer.IsCompletedSuccessfully)
        {
            Exception? e = join.Answer.Exception?.GetBaseException();
            string error = e is MasterException ? e.Message : join.Answer.IsCanceled ? "The request to the master server was cancelled." : "The master server request failed: " + (e?.Message ?? "unknown error");
            Log("Master join: the master refused " + join.Label + ": " + error);
            online.FailJoin(error);
            return;
        }

        JoinResponse answer = join.Answer.Result;
        if ((answer.RequireAccounts || join.RequireAccount) && answer.Ticket is null)
        {
            string problem = answer.TicketProblem ?? $"{char.ToUpperInvariant(join.Label[0])}{join.Label[1..]} takes signed-in players only. Sign in to join it.";
            Log("Master join: no ticket for " + join.Label + ": " + problem);
            online.FailJoin(problem);
            return;
        }

        JoinTarget target = JoinTarget.FromJoin(answer);
        Log($"Master join: the master answered for {join.Label}: {target.Attempts.Count} way(s) in{(answer.Ticket is null ? string.Empty : ", with a ticket")}; connecting");
        online.Join(target, join.Password, answer.Ticket, join.Team);
    }

    /// <summary>Joins the hub the master lists as <paramref name="id"/> (a website join of a hub): HubBridge's listed join.</summary>
    public bool JoinHubListed(string id, string? hubName, string? playerName, int team, bool requireAccount)
    {
        Log("Hub join: the master's hub " + (hubName ?? id) + " (" + id + ")");
        return HubBridge.Instance.JoinListedId(id, hubName, requireAccount, playerName, team);
    }

    /// <summary>
    /// How friends join the online visit (PresenceSnapshot.OnlineJoin): a listed server by its master id, else a
    /// single direct outside address the player typed; null otherwise. A server with a password says so.
    /// </summary>
    public static DiscordJoinSecret? VisitJoin()
    {
        if (OnlineBridge.Existing?.Client is not { } client)
        {
            return null;
        }

        bool password = client.Settings.Password is { Length: > 0 };
        JoinTarget target = client.Target;
        if (target.MasterId is { Length: > 0 } masterId)
        {
            return new DiscordJoinSecret(DiscordJoinKind.Master, masterId, password);
        }

        return target.Attempts is [{ Kind: ConnectKind.Direct, IsLan: false } attempt] && DiscordJoinSecret.Shareable(attempt.Address)
            ? new DiscordJoinSecret(DiscordJoinKind.Address, attempt.Address, password)
            : null;
    }

    // ---- requests and the log -------------------------------------------------------------------------------------

    internal void Request(SocialRequestKind kind, SocialRequestFlags flags, string value, string text)
    {
        if (_requests.Count == RequestCapacity)
        {
            _requests.Dequeue();
        }

        _requests.Enqueue((new SocialRequest { Kind = (int)kind, Flags = (int)flags }, value, text));
    }

    public bool TakeRequest(out SocialRequest request, out string value, out string text)
    {
        if (_requests.TryDequeue(out (SocialRequest Request, string Value, string Text) next))
        {
            (request, value, text) = next;
            return true;
        }

        request = default;
        value = text = string.Empty;
        return false;
    }

    internal void Log(string line)
    {
        while (_log.Count >= LogCapacity && _log.TryDequeue(out _))
        {
        }

        _log.Enqueue(line);
    }

    public bool TakeLog(out string line) => _log.TryDequeue(out line!);

    /// <summary>The map's name as the menus show it ("ENHANCED STEEL SQUAT"), else its id.</summary>
    private string MapTitle(string id)
    {
        if (_titles.TryGetValue(id, out string? title))
        {
            return title;
        }

        try
        {
            title = OnlineBridge.Instance.Catalog.Maps.GetCatalogEntry(id)?.DisplayName is { Length: > 0 } name ? name : id;
        }
        catch (Exception e) when (e is System.IO.IOException or InvalidOperationException or ArgumentException)
        {
            title = id;
        }

        _titles[id] = title;
        return title;
    }

    public void Dispose()
    {
        Discord.Dispose();
        Web.Dispose();
        if (ReferenceEquals(s_instance, this))
        {
            s_instance = null;
        }
    }

    private sealed record PendingMasterJoin(Task<JoinResponse> Answer, string Id, string Label, string? Password, int Team, bool RequireAccount);
}
