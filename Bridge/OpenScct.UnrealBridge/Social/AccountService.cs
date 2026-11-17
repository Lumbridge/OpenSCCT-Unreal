using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using OpenScct.Core.Engine;
using OpenScct.Net.Cosmetics;
using OpenScct.Net.Discovery;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// The player's account on the master for the whole run (a port of the Godot client's AccountService, AccountSettings
/// and the master calls of its AccountScreen): it asks the master for the account after each sign-in, at start-up
/// with a saved sign-in and a few seconds after each round's results, keeps the last <see cref="AccountInfo"/> for the
/// Account page, claims and changes the name, enters the next prestige and signs out. It also
/// syncs the player's settings with the account (<see cref="ProfileSync"/>): it takes the account's copy after a
/// sign-in, and sends this PC's 3 s after a setting or a key layout changed.
/// <para>The sign-in itself and its token belong to the online bridge's <see cref="OnlineMaster"/> (Unreal keeps the
/// token sealed with DPAPI); this class follows it. A 401 from the master on any of its calls signs the player out
/// on this PC: Unreal is asked (<see cref="SocialRequestKind.SignedOut"/>) to forget the token.</para>
/// <para>Master calls run on worker threads with a client of their own; their answers are taken in
/// <see cref="Update"/>, on the game thread. The settings themselves live in Unreal (the settings registry and the
/// control files): Unreal hands this PC's copy over with <see cref="SetLocalProfile"/> and applies the account's
/// when <see cref="Incoming"/> has one.</para>
/// </summary>
public sealed partial class AccountService
{
    /// <summary>How long after a change the settings are sent (more changes in that time send once).</summary>
    public const long PushDelayMs = 3000;

    /// <summary>A refresh asked for by a page or a timer goes to the master at most this often.</summary>
    private const long RefreshEveryMs = 60_000;

    /// <summary>The round's results reach the master from the server a little after the client sees them.</summary>
    private const long RoundRefreshDelayMs = 8_000;

    private readonly SocialBridge _social;
    private readonly long _epoch = Stopwatch.GetTimestamp();
    private string? _stateFile;
    private bool _started;
    private bool _syncOff;
    private AccountStateFile _state = new();
    private (string? Url, string? Invite, string? Token) _identity;

    private Task<AccountInfo>? _refresh;
    private bool _refreshPulls;
    private long _nextRefresh;
    private Task<AccountInfo>? _claim;
    private Task<AccountInfo>? _prestige;
    private Task<bool>? _signOut;
    private OnlineState _lastOnline;
    private long _roundRefreshAt;
    private string? _chosenTitle;

    internal AccountService(SocialBridge social)
    {
        _social = social;
    }

    /// <summary>The account as the master last described it this run (null until then, and when signed out).</summary>
    public AccountInfo? Account { get; private set; }

    /// <summary>The last refresh failed; what is shown is from before.</summary>
    public bool RefreshFailed { get; private set; }

    /// <summary>The page's last message.</summary>
    public string Status { get; private set; } = string.Empty;

    /// <summary>Goes up whenever anything the page shows changed.</summary>
    public int Revision { get; private set; }

    /// <summary>A made-up account for the page (-ScctAccountPreview): nothing is asked of the master or sent.</summary>
    public bool Preview { get; private set; }

    private static OnlineMaster? Master => OnlineBridge.Existing?.Master;

    private long NowMs => (long)Stopwatch.GetElapsedTime(_epoch).TotalMilliseconds;

    private bool SignedIn => Master is { Url: not null, AccountToken: not null };

    // ---- the run --------------------------------------------------------------------------------------------------

    /// <summary>
    /// Starts the service once: <paramref name="stateFile"/> keeps the account as last seen and the sync's bookkeeping
    /// between runs (null: nothing kept). <paramref name="syncOff"/> never sends or takes settings (unattended runs
    /// that must not change the account); <paramref name="preview"/> shows a made-up account.
    /// </summary>
    public void Start(string? stateFile, bool syncOff, bool preview)
    {
        if (_started)
        {
            return;
        }

        _started = true;
        _stateFile = string.IsNullOrWhiteSpace(stateFile) ? null : stateFile;
        _syncOff = syncOff;
        _state = AccountStateFile.Load(_stateFile);
        if (preview)
        {
            Preview = true;
            Account = PreviewAccount();
            Status = "Preview: a made-up account; nothing is sent to the master.";
        }

        _social.Log($"Account: started (settings sync {(syncOff ? "off for this run" : "on")}{(preview ? ", preview account" : string.Empty)})");
        Touch();
    }

    /// <summary>Once a frame on the game thread: follows the sign-in, takes the master's answers, sends the settings when due.</summary>
    public void Update()
    {
        if (!_started || Preview)
        {
            return;
        }

        FollowSignIn();
        PumpRefresh();
        PumpActions();
        PumpProfile();
        FollowRounds();
    }

    /// <summary>The sign-in changed (a new one, a saved one at start-up, a sign-out, another master): start over with it.</summary>
    private void FollowSignIn()
    {
        OnlineMaster? master = Master;
        (string? Url, string? Invite, string? Token) identity = (master?.Url, master?.InviteKey, master?.AccountToken);
        if (identity == _identity)
        {
            return;
        }

        bool wasSignedIn = _identity.Token is not null;
        _identity = identity;

        // Answers for the previous sign-in cannot touch this one.
        _refresh = null;
        _claim = null;
        _prestige = null;
        _signOut = null;
        ResetProfile();
        Account = null;
        RefreshFailed = false;

        if (identity is { Url: not null, Token: not null })
        {
            _refresh = Call(c => c.AccountAsync());
            _refreshPulls = true;
            _nextRefresh = NowMs + RefreshEveryMs;
            _social.Log("Account: asking the master for the signed-in account");
        }
        else if (wasSignedIn)
        {
            _state.Forget();
            SaveState();
            _social.Log("Account: signed out on this PC");
        }

        Touch();
    }

    /// <summary>A few seconds after each round's results the account is asked for again (XP, rank and stats moved).</summary>
    private void FollowRounds()
    {
        OnlineState now = OnlineBridge.Existing?.State ?? OnlineState.Idle;
        if (now != _lastOnline)
        {
            if (now == OnlineState.Results)
            {
                _roundRefreshAt = NowMs + RoundRefreshDelayMs;
            }

            _lastOnline = now;
        }

        if (_roundRefreshAt != 0 && NowMs >= _roundRefreshAt)
        {
            _roundRefreshAt = 0;
            if (SignedIn)
            {
                _social.Log("Account: the round is over; asking for the account's new progress");
                Refresh(force: true);
            }
        }
    }

    // ---- the account ----------------------------------------------------------------------------------------------

    /// <summary>Asks the master for the account (the Account page on opening; at most once a minute unless forced).</summary>
    public void Refresh(bool force = false)
    {
        if (Preview || !SignedIn || _refresh is not null || (!force && NowMs < _nextRefresh))
        {
            return;
        }

        _nextRefresh = NowMs + RefreshEveryMs;
        _refreshPulls = false;
        _refresh = Call(c => c.AccountAsync());
        Touch();
    }

    private void PumpRefresh()
    {
        if (_refresh is not { IsCompleted: true } refresh)
        {
            return;
        }

        _refresh = null;
        if (refresh.IsCompletedSuccessfully)
        {
            bool first = Account is null;
            Remember(refresh.Result);
            if (first)
            {
                AccountInfo a = refresh.Result;
                string rank = a.Progress is { } p ? $"level {PlayerRank.FromXp(p.Prestige, p.Xp).Level}, prestige {p.Prestige}, {p.TotalXp} XP" : "no levels on this master";
                _social.Log($"Account: signed in as {(a.Name is null ? "(no name yet)" : "'" + a.Name + "'")} (account {a.Id}; {rank}; {a.Stats.Matches} counted round(s))");
            }

            if (_refreshPulls)
            {
                _refreshPulls = false;
                Pull();
            }
        }
        else if (IsSignedOutError(refresh))
        {
            SignOutLocally("Your sign-in ended. Sign in again.", "the master no longer knows this sign-in (401)");
        }
        else
        {
            RefreshFailed = true;
            _nextRefresh = NowMs + RefreshEveryMs;
            _social.Log("Account: the refresh failed: " + ErrorOf(refresh));
            Touch();
        }
    }

    /// <summary>A newer description of the account: kept for the page, the next run and the joins (the claimed name).</summary>
    private void Remember(AccountInfo account)
    {
        Account = account;
        RefreshFailed = false;
        _state.Remember(account);
        SaveState();
        Master?.Remember(account);
        Touch();
    }

    /// <summary>Claims or changes the account's name. False (with the reason in <see cref="Status"/>) when it cannot be asked.</summary>
    public bool ClaimName(string? name)
    {
        name = name?.Trim() ?? string.Empty;
        if (PlayerNames.ClaimProblem(name) is { } problem)
        {
            SetStatus(problem);
            return false;
        }

        if (Preview)
        {
            SetStatus("Preview: the name is not sent.");
            return false;
        }

        if (!SignedIn || _claim is not null)
        {
            return false;
        }

        SetStatus("Claiming " + name + "...");
        _social.Log("Account: claiming the name '" + name + "'");
        _claim = Call(c => c.ClaimNameAsync(name));
        return true;
    }

    /// <summary>Enters the next prestige (the page asks the player to confirm first).</summary>
    public bool Prestige()
    {
        if (Preview)
        {
            SetStatus("Preview: nothing is sent.");
            return false;
        }

        if (!SignedIn || _prestige is not null)
        {
            return false;
        }

        SetStatus("Asking the master server...");
        _social.Log("Account: entering the next prestige");
        _prestige = Call(c => c.PrestigeAsync());
        return true;
    }

    /// <summary>Ends the sign-in on the master, then forgets it here whatever the master said.</summary>
    public bool SignOut()
    {
        if (Preview)
        {
            SetStatus("Preview: nothing is sent.");
            return false;
        }

        if (!SignedIn || _signOut is not null)
        {
            return false;
        }

        SetStatus("Signing out...");
        _signOut = Call(async c =>
        {
            await c.SignOutAsync().ConfigureAwait(false);
            return true;
        });
        return true;
    }

    private void PumpActions()
    {
        if (_claim is { IsCompleted: true } claim)
        {
            _claim = null;
            if (claim.IsCompletedSuccessfully)
            {
                Remember(claim.Result);
                SetStatus("Name claimed: " + claim.Result.Name);
                _social.Log("Account: name claimed: '" + claim.Result.Name + "'");
            }
            else if (!SignedOutBy(claim))
            {
                SetStatus(ErrorOf(claim));
                _social.Log("Account: the name was refused: " + ErrorOf(claim));
            }
        }

        if (_prestige is { IsCompleted: true } prestige)
        {
            _prestige = null;
            if (prestige.IsCompletedSuccessfully)
            {
                Remember(prestige.Result);
                int p = prestige.Result.Progress?.Prestige ?? 0;
                SetStatus($"Prestige {p}: back to level 1 with a new emblem" +
                    (TitleCatalog.Find("prestige_" + p.ToString(CultureInfo.InvariantCulture)) is { } t ? $" and the title {t.Text}." : "."));
                _social.Log("Account: entered prestige " + p);
            }
            else if (!SignedOutBy(prestige))
            {
                SetStatus(ErrorOf(prestige));
            }
        }

        if (_signOut is { IsCompleted: true } signOut)
        {
            // Best effort: whatever the master said, this PC forgets the sign-in.
            _signOut = null;
            _ = signOut.Exception;
            SignOutLocally("Signed out.", "the player signed out");
        }
    }

    /// <summary>True (after signing out here) when <paramref name="task"/> failed because the master no longer knows the sign-in.</summary>
    private bool SignedOutBy(Task task)
    {
        if (!IsSignedOutError(task))
        {
            return false;
        }

        SignOutLocally("Your sign-in ended. Sign in again.", "the master no longer knows this sign-in (401)");
        return true;
    }

    /// <summary>Forgets the sign-in on this PC: the account, the sync's pending work, and (through Unreal) the saved token.</summary>
    private void SignOutLocally(string status, string why)
    {
        _social.Log("Account: signed out on this PC: " + why);
        Account = null;
        RefreshFailed = false;
        _refresh = null;
        _claim = null;
        _prestige = null;
        _signOut = null;
        ResetProfile();
        _state.Forget();
        SaveState();
        Status = status;

        // The online bridge forgets the token now; Unreal deletes its saved copy when it takes the request.
        Master?.SignOut();
        _identity = (Master?.Url, Master?.InviteKey, null);
        _social.Request(SocialRequestKind.SignedOut, SocialRequestFlags.None, string.Empty, status);
        Touch();
    }

    /// <summary>The title Unreal's settings hold (its id, or empty for none): the page marks it and the rank shows it.</summary>
    public void SetChosenTitle(string? id)
    {
        id = string.IsNullOrWhiteSpace(id) ? null : id.Trim();
        if (id != _chosenTitle)
        {
            _chosenTitle = id;
            Touch();
        }
    }

    // ---- what the page reads --------------------------------------------------------------------------------------

    public AccountView View
    {
        get
        {
            if (Preview)
            {
                return Account?.Name is null ? AccountView.PickName : AccountView.SignedIn;
            }

            OnlineMaster? master = Master;
            if (master?.Url is null)
            {
                return AccountView.NoMaster;
            }

            if (master.AccountState == OnlineAccountState.SigningIn)
            {
                return AccountView.SigningIn;
            }

            if (master.AccountToken is null)
            {
                return AccountView.SignedOut;
            }

            string? name = Account is { } a ? a.Name : _state.AccountId > 0 && _state.Name.Length > 0 ? _state.Name : null;
            // While the first answer is on its way the page says so on the signed-in view.
            return name is null && (Account is not null || _refresh is null) ? AccountView.PickName : AccountView.SignedIn;
        }
    }

    public void Fill(ref AccountInfoBlock info)
    {
        AccountInfo? a = Account;
        info = default;
        info.View = (int)View;
        info.Revision = Revision;
        info.AccountId = a?.Id ?? _state.AccountId;
        info.TitlesTotal = TitleCatalog.All.Count;
        info.TitleChosen = -1;
        info.SyncState = (int)SyncState;

        AccountFlags flags = AccountFlags.None;
        if (a?.Admin ?? _state.Admin) flags |= AccountFlags.Admin;
        if (Preview) flags |= AccountFlags.Preview;
        if (RefreshFailed) flags |= AccountFlags.RefreshFailed;
        if (_claim is not null) flags |= AccountFlags.Claiming;
        if (_prestige is not null) flags |= AccountFlags.Prestiging;
        if (_signOut is not null) flags |= AccountFlags.SigningOut;
        if (_refresh is not null) flags |= AccountFlags.Refreshing;

        if (a is not null)
        {
            flags |= AccountFlags.Known;
            if (a.NameChangeAllowed is { } allowed && allowed > DateTimeOffset.UtcNow)
            {
                flags |= AccountFlags.NameLocked;
                info.NameChangeAllowed = allowed.ToUnixTimeSeconds();
            }

            AccountStats st = a.Stats;
            info.Matches = st.Matches;
            info.SpyWins = st.SpyWins;
            info.MercWins = st.MercWins;
            info.Losses = st.Losses;
            info.Kills = st.Kills;
            info.Deaths = st.Deaths;

            if (a.Progress is { } progress)
            {
                PlayerRank rank = RankOf(a);
                flags |= AccountFlags.HasProgress;
                if (rank.CanPrestige) flags |= AccountFlags.CanPrestige;
                if (rank.AtCap) flags |= AccountFlags.AtCap;
                if (rank.IsMaster) flags |= AccountFlags.Master;
                info.Prestige = rank.Prestige;
                info.Level = rank.Level;
                info.XpIntoLevel = rank.XpIntoLevel;
                info.XpForNext = rank.XpForNext;
                info.TotalXp = progress.TotalXp;
                info.BestLevel = progress.BestLevel;
                info.RankColour = ColourOf(rank);
                info.EmblemsReached = rank.Prestige + (rank.IsMaster ? 1 : 0);
                info.TitlesUnlocked = progress.Titles.Count;
                if (rank.Title is { } title)
                {
                    info.TitleChosen = title.Bit;
                }
            }
        }

        info.Flags = (int)flags;
    }

    public string Text(AccountText kind)
    {
        AccountInfo? a = Account;
        PlayerRank rank = RankOf(a);
        return kind switch
        {
            AccountText.Name => a?.Name ?? _state.Name,
            AccountText.Status => Status,
            AccountText.NameCooldown => a?.NameChangeAllowed is { } allowed && allowed > DateTimeOffset.UtcNow
                ? "You can change your name again on " + allowed.ToLocalTime().ToString("d MMMM yyyy", CultureInfo.InvariantCulture) + "."
                : string.Empty,
            AccountText.RankTag => TagOf(rank),
            AccountText.RankDescription => Describe(rank),
            AccountText.NextLevel => rank.IsNone ? string.Empty : rank.AtCap
                ? $"Level {ProgressionK.MaxLevel} · Maximum level"
                : string.Create(CultureInfo.InvariantCulture, $"{rank.XpIntoLevel:N0} / {rank.XpForNext:N0} XP to level {rank.Level + 1}"),
            AccountText.TotalXp => a?.Progress is { } p ? string.Create(CultureInfo.InvariantCulture, $"{p.TotalXp:N0} lifetime XP") : string.Empty,
            AccountText.Title => rank.Title?.Text ?? "No title equipped",
            AccountText.PrestigeHelp => rank.CanPrestige ? $"Return to level 1 for the prestige {rank.Prestige + 1} emblem and a new title."
                : rank.IsMaster ? "Master prestige · The highest rank."
                : string.Empty,
            AccountText.XpRules => string.Create(CultureInfo.InvariantCulture,
                $"{ProgressionK.ScoreXp} XP per score point · {ProgressionK.FinishXp} for finishing · {ProgressionK.WinXp} for winning · " +
                $"{ProgressionK.ObjectiveXp} per objective · {ProgressionK.AchievementXp}–{ProgressionK.MetaAchievementXp} per achievement"),
            AccountText.CareerHelp => a is null ? "Asking the master server…"
                : a.Stats.Matches == 0 ? "Your career starts with your first counted official round." : "Totals from counted official rounds.",
            AccountText.SignInCode => Master?.SignInCode ?? string.Empty,
            _ => string.Empty,
        };
    }

    /// <summary>The title at <paramref name="index"/> of the catalogue, with the account's unlocks and choice; false past the end.</summary>
    public bool Title(int index, out AccountTitle title, out string id, out string text, out string how)
    {
        title = default;
        id = text = how = string.Empty;
        if (index < 0 || index >= TitleCatalog.All.Count)
        {
            return false;
        }

        TitleItem t = TitleCatalog.All[index];
        bool have = Account?.Progress?.Titles.Contains(t.Id) == true;
        title.Bit = t.Bit;
        title.Unlocked = have ? 1 : 0;
        title.Chosen = have && _chosenTitle == t.Id ? 1 : 0;
        id = t.Id;
        text = t.Text;
        how = have
            ? t.HowToUnlock.Replace("Reach", "Reached", StringComparison.Ordinal).Replace("Earn the", "Earned the", StringComparison.Ordinal)
            : "Locked. " + t.HowToUnlock;
        return true;
    }

    /// <summary>The account's rank with the chosen title when the account has it (RankIcons.OfAccount); none without levels.</summary>
    private PlayerRank RankOf(AccountInfo? info)
    {
        if (info?.Progress is not { } p)
        {
            return PlayerRank.None;
        }

        int title = _chosenTitle is { } chosen && p.Titles.Contains(chosen) && TitleCatalog.Find(chosen) is { } t ? t.Bit : -1;
        return PlayerRank.FromXp(p.Prestige, p.Xp, title);
    }

    // ---- ranks (RankIcons) ----------------------------------------------------------------------------------------

    private static readonly int[] s_tierColours =
    [
        0x7d8f81, 0x93b38c, 0xa6d38c, 0xbfe07c, 0xdbe46b, 0xecd25a, 0xf0b24a, 0xef8f45, 0xe96a4a, 0xdc5072, 0xc45ae0,
    ];

    private static readonly int[] s_prestigeColours =
    [
        0xffffff, 0xc8463a, 0xe6862b, 0xf1c40f, 0x7fd34a, 0x1fc4a0, 0x3a9be0, 0x6c63f0, 0xb356e6, 0xeef3f4, 0xffcf3a,
    ];

    /// <summary>The colour of a rank's emblem, 0xRRGGBB (RankIcons.ColourOf): by prestige once prestiged, else by tier of five levels.</summary>
    public static int ColourOf(PlayerRank rank) => rank.IsNone ? 0xffffff
        : rank.IsMaster ? s_prestigeColours[ProgressionK.MaxPrestige]
        : rank.Prestige > 0 ? s_prestigeColours[Math.Clamp(rank.Prestige, 0, ProgressionK.MaxPrestige)]
        : s_tierColours[Math.Clamp((rank.Level - 1) / 5, 0, s_tierColours.Length - 1)];

    /// <summary>The prestige emblem's colour (1 to 10; 11 is the master emblem).</summary>
    public static int EmblemColour(int prestige) => s_prestigeColours[Math.Clamp(prestige, 1, ProgressionK.MaxPrestige)];

    /// <summary>"42", "P3 · 42" or "Master · 55" (RankIcons.Text).</summary>
    public static string TagOf(PlayerRank rank) =>
        rank.IsNone ? string.Empty : rank.IsMaster ? $"Master · {rank.Level}" : rank.Prestige > 0 ? $"P{rank.Prestige} · {rank.Level}" : rank.Level.ToString(CultureInfo.InvariantCulture);

    /// <summary>"Prestige 3, level 42 - Field Agent" (RankIcons.Describe).</summary>
    public static string Describe(PlayerRank rank)
    {
        if (rank.IsNone)
        {
            return string.Empty;
        }

        string what = rank.IsMaster ? $"Master prestige, level {rank.Level}" : rank.Prestige > 0 ? $"Prestige {rank.Prestige}, level {rank.Level}" : $"Level {rank.Level}";
        return rank.Title is { } t ? what + " - " + t.Text : what;
    }

    // ---- master calls ---------------------------------------------------------------------------------------------

    /// <summary>
    /// Runs one master call on a worker thread with a client of its own (made now, disposed after), so a change of the
    /// online bridge's master never cuts it off midway. Fails with a <see cref="MasterException"/> when no master is set.
    /// </summary>
    private static Task<T> Call<T>(Func<MasterClient, Task<T>> call, bool withToken = true)
    {
        OnlineMaster? master = Master;
        if (master?.Url is not { } url)
        {
            return Task.FromException<T>(new MasterException("No master server is set."));
        }

        MasterClient client;
        try
        {
            client = new MasterClient(url, master.InviteKey) { SessionToken = withToken ? master.AccountToken : null };
        }
        catch (ArgumentException e)
        {
            return Task.FromException<T>(new MasterException(e.Message));
        }

        return Task.Run(async () =>
        {
            using (client)
            {
                return await call(client).ConfigureAwait(false);
            }
        });
    }

    /// <summary>A finished task's failure as a player reads it.</summary>
    public static string ErrorOf(Task task)
    {
        Exception? e = task.Exception?.GetBaseException();
        return e is MasterException ? e.Message : task.IsCanceled ? "The request to the master server was cancelled." : "The master server did not answer: " + (e?.Message ?? "unknown error");
    }

    private bool IsSignedOutError(Task task) =>
        task is { IsFaulted: true } && task.Exception?.GetBaseException() is MasterException { StatusCode: 401 } && _identity.Token is not null;

    private void SetStatus(string text)
    {
        Status = text;
        Touch();
    }

    private void Touch() => Revision++;

    private void SaveState() => _state.Save(_stateFile, _social.Log);

    /// <summary>The page's made-up account (-ScctAccountPreview): prestige 2, part way through level 37, some titles.</summary>
    private static AccountInfo PreviewAccount()
    {
        int xp = PlayerRank.XpToReach(37) + 1250;
        string[] titles = TitleCatalog.All.Where(t => t.Source == TitleSource.Prestige && t.Value <= 2 || t.Source == TitleSource.Level && t.Value <= 37)
            .Select(t => t.Id).ToArray();
        return new AccountInfo
        {
            Id = 1,
            Name = "Preview",
            NameChangeAllowed = DateTimeOffset.UtcNow.AddDays(12),
            Stats = new AccountStats { Matches = 214, SpyWins = 61, MercWins = 58, Losses = 95, Kills = 1403, Deaths = 1177 },
            Progress = new ProgressInfo
            {
                Prestige = 2, Level = 37, Xp = xp, TotalXp = 2L * PlayerRank.CapXp + xp, BestLevel = 55, Titles = titles,
            },
        };
    }
}

/// <summary>
/// What the account service keeps between runs (the Godot client's account.cfg without the token, which Unreal keeps):
/// the account as last seen, so the page can name it before the master answers, and the settings sync's bookkeeping.
/// </summary>
internal sealed class AccountStateFile
{
    public long AccountId { get; set; }
    public string Name { get; set; } = string.Empty;
    public bool Admin { get; set; }

    /// <summary>When this PC's synced settings last changed, Unix milliseconds (0: never).</summary>
    public long ProfileUpdatedMs { get; set; }

    /// <summary>The account this PC last synced its settings with (0: none yet).</summary>
    public long SyncedAccountId { get; set; }

    /// <summary>The account's settings record as last seen (its Godot part is sent back unchanged).</summary>
    public string? LastRecord { get; set; }

    public void Remember(AccountInfo account)
    {
        if (account.Id != AccountId)
        {
            LastRecord = null;
        }

        AccountId = account.Id;
        Name = account.Name ?? string.Empty;
        Admin = account.Admin;
    }

    /// <summary>Forgets the account (the sync's bookkeeping stays, as in the Godot client).</summary>
    public void Forget()
    {
        AccountId = 0;
        Name = string.Empty;
        Admin = false;
    }

    public static AccountStateFile Load(string? path)
    {
        if (path is null || !File.Exists(path))
        {
            return new AccountStateFile();
        }

        try
        {
            return JsonSerializer.Deserialize<AccountStateFile>(File.ReadAllText(path)) ?? new AccountStateFile();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return new AccountStateFile();
        }
    }

    public void Save(string? path, Action<string> log)
    {
        if (path is null)
        {
            return;
        }

        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path) ?? ".");
            File.WriteAllText(path, JsonSerializer.Serialize(this));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            log("Account: could not write " + path + ": " + e.Message);
        }
    }
}
