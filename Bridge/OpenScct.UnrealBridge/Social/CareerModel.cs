using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using OpenScct.Net.Cosmetics;
using OpenScct.Net.Discovery;
using OpenScct.Net.Scoreboards;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Demo;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Loadout;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// What the Stats, Match history and Achievements pages show (ports of the Godot client's StatsScreen, the match
/// history of DemosScreen.History and MatchHistoryScreen, and AchievementsScreen, with the widgets taken out):
/// <list type="bullet">
/// <item>match history: the rounds this PC saved (<see cref="ScoreboardHistory"/>) joined with the signed-in account's
/// history on the master (MatchHistoryAsync, page by page) by round, newest first; a round's details (players and
/// timeline) come from MatchDetailsAsync for a round the master knows, else from the saved scoreboard;</item>
/// <item>stats: <see cref="StatsModel"/>'s rows of the account the master last described;</item>
/// <item>achievements: every achievement of the shared catalogue with the account's progress and earned dates.</item>
/// </list>
/// Game thread only; master calls run on worker threads and are picked up by <see cref="Pump"/>, which every read of
/// the career table calls.
/// </summary>
public sealed class CareerModel
{
    public static CareerModel Instance { get; } = new();

    public sealed record Item(ArchivedScoreboard Record, long? CloudId, LocalDemo? Replay);

    private readonly List<ArchivedScoreboard> _archive = new();
    private readonly Dictionary<long, MatchHistoryEntry> _cloud = new();
    private readonly Dictionary<long, MatchHistoryDetails> _details = new();
    private readonly List<Item> _items = new();
    private (string Master, long Account, string? Token) _identity;
    private (string Master, long Account, string? Token) _requestIdentity;
    private Task<MatchHistoryPage>? _historyRequest;
    private Task<MatchHistoryDetails>? _detailsRequest;
    private long _detailsId;
    private int _cloudPage = -1;
    private bool _historyWanted;
    private int _archiveRevision = -1;
    private int _demoRevision = -1;
    private int _accountRevision = -1;
    private long _masteryRevision = -1;
    private bool _privateProgress;
    private int _nextUnlock = -1;
    private string? _historyError;
    private string? _detailsError;
    private IReadOnlyList<StatsModel.Row> _stats = [];
    private readonly List<AchievementRow> _achievements = new();

    private readonly Dictionary<string, AchievementRow> _rewardProgress = new(StringComparer.Ordinal);
    public AchievementRow? UnlockFor(string rewardId) => _rewardProgress.GetValueOrDefault(rewardId);

    public sealed record AchievementRow(Achievement Achievement, bool Earned, int Current, int Target, DateTimeOffset? EarnedAt, bool Reported, bool Private = false);

    public int Revision { get; private set; }

    public IReadOnlyList<Item> Items => _items;

    public bool HistoryLoading => _historyRequest is not null;

    public bool HistoryMore { get; private set; }

    public bool DetailsLoading => _detailsRequest is not null;

    public long DetailsId => _detailsId;

    public IReadOnlyList<StatsModel.Row> Stats => _stats;

    public IReadOnlyList<AchievementRow> AchievementRows => _achievements;

    public int AchievementsEarned => _achievements.Count(a => a.Earned);

    private static AccountService Account => SocialBridge.Instance.Account;

    private static (string, long, string?) CurrentIdentity =>
        (AccountService.MasterUrl ?? string.Empty, Account.SignedInAccountId, OnlineBridge.Existing?.Master.AccountToken);

    private bool CanAskMaster => !Account.Preview && _identity.Account > 0 && _identity.Master.Length > 0 && _identity.Token is not null;

    // ---- the run --------------------------------------------------------------------------------------------------

    /// <summary>Takes the master's answers, follows the account and the archive.</summary>
    public void Pump()
    {
        var identity = CurrentIdentity;
        if (identity != _identity)
        {
            _identity = identity;
            _cloud.Clear();
            _details.Clear();
            _cloudPage = -1;
            HistoryMore = false;
            _historyError = null;
            _detailsError = null;
            if (_historyWanted)
            {
                ReadArchive();
                LoadCloud(0);
            }

            Touch();
        }

        if (_historyWanted && (ScoreboardHistory.Revision != _archiveRevision || DemoLibrary.Revision != _demoRevision))
        {
            ReadArchive();
            Touch();
        }

        if (_historyRequest is { IsCompleted: true } history)
        {
            _historyRequest = null;
            if (_requestIdentity != _identity)
            {
                _ = history.Exception;
            }
            else if (history.IsCompletedSuccessfully)
            {
                foreach (MatchHistoryEntry entry in history.Result.Matches)
                {
                    _cloud[entry.Id] = entry;
                }

                HistoryMore = history.Result.HasMore;
                _historyError = null;
            }
            else
            {
                _historyError = "Your account's history could not be read: " + AccountService.ErrorOf(history);
            }

            Rebuild();
        }

        if (_detailsRequest is { IsCompleted: true } details)
        {
            _detailsRequest = null;
            if (details.IsCompletedSuccessfully)
            {
                _details[_detailsId] = details.Result;
                _detailsError = null;
            }
            else
            {
                _detailsError = "The match details could not be read: " + AccountService.ErrorOf(details);
            }

            Touch();
        }

        bool privateProgress = UsesPrivateMastery;
        long masteryRevision = privateProgress ? WeaponCareerView.MasteryRevision : 0;
        if (Account.Revision != _accountRevision || masteryRevision != _masteryRevision || privateProgress != _privateProgress)
        {
            _accountRevision = Account.Revision;
            _masteryRevision = masteryRevision;
            _privateProgress = privateProgress;
            BuildCareer(Account.Account);
            Touch();
        }
    }

    /// <summary>The history page opened or asked for a refresh: the archive is read again and the master asked for page 1.</summary>
    public void RefreshHistory()
    {
        _historyWanted = true;
        ReadArchive();
        LoadCloud(0);
        Touch();
    }

    /// <summary>The next page of the account's history on the master.</summary>
    public void MoreHistory()
    {
        if (HistoryMore && _historyRequest is null)
        {
            LoadCloud(_cloudPage + 1);
            Touch();
        }
    }

    /// <summary>Asks the master for a round's players and timeline (a round the master knows; others show their saved scoreboard).</summary>
    public void LoadDetails(int index)
    {
        if (index < 0 || index >= _items.Count || _items[index].CloudId is not { } id || _detailsRequest is not null || !CanAskMaster)
        {
            return;
        }

        if (_details.ContainsKey(id))
        {
            return;
        }

        _detailsId = id;
        _detailsError = null;
        _detailsRequest = AccountService.Call(c => c.MatchDetailsAsync(id));
        Touch();
    }

    public MatchHistoryDetails? DetailsOf(Item item) => item.CloudId is { } id && _details.TryGetValue(id, out var d) ? d : null;

    public string Status
    {
        get
        {
            if (ScoreboardHistory.Store is null)
            {
                return "The scoreboard history is not set up yet.";
            }

            string count = _items.Count == 1 ? "1 round" : _items.Count.ToString(CultureInfo.InvariantCulture) + " rounds";
            string account = _historyRequest is not null ? " · Loading your account's history…"
                : AccountService.MasterUrl is null ? " · No master server is set, so only rounds played on this PC are listed"
                : _identity.Account == 0 ? " · Sign in to add your account's history"
                : HistoryMore ? " · More on the master" : string.Empty;
            return count + account + ". Newest first; practice and unfinished rounds are labelled.";
        }
    }

    public string Error => _historyError ?? ScoreboardHistory.LastError ?? string.Empty;

    public string DetailsError => _detailsError ?? string.Empty;

    private void LoadCloud(int page)
    {
        if (_historyRequest is not null || !CanAskMaster)
        {
            return;
        }

        if (page == 0)
        {
            _cloud.Clear();
            HistoryMore = false;
        }

        _requestIdentity = _identity;
        _historyError = null;
        _cloudPage = page;
        _historyRequest = AccountService.Call(c => c.MatchHistoryAsync(page));
    }

    private void ReadArchive()
    {
        _archiveRevision = ScoreboardHistory.Revision;
        if (DemoLibrary.SavedFolder is not null)
        {
            DemoLibrary.RefreshLocal();
        }

        _demoRevision = DemoLibrary.Revision;
        _archive.Clear();
        _archive.AddRange(ScoreboardHistory.Read(_identity.Master, _identity.Account));
        Rebuild();
    }

    /// <summary>The saved rounds and the master's joined by round (DemosScreen.HistoryItems), newest first.</summary>
    private void Rebuild()
    {
        var items = new Dictionary<string, Item>();
        foreach (ArchivedScoreboard entry in _archive)
        {
            string key;
            try
            {
                key = ScoreboardArchive.Identity(entry);
            }
            catch (System.IO.InvalidDataException)
            {
                continue;
            }

            long? cloud = ScoreboardArchive.CanQuery(entry, _identity.Master, _identity.Account) ? entry.Scoreboard.MatchId : null;
            items[key] = new Item(entry, cloud, FindReplay(entry.Scoreboard.RoundId));
        }

        foreach (MatchHistoryEntry entry in _cloud.Values)
        {
            var record = new ArchivedScoreboard
            {
                Master = _identity.Master,
                AccountId = _identity.Account,
                Scoreboard = new RoundScoreboard
                {
                    RoundId = entry.RoundId ?? string.Empty, MatchId = entry.Id, Master = _identity.Master,
                    Map = entry.Map, Server = entry.Server, Mode = entry.Mode, StartedUtc = entry.Ended.AddSeconds(-entry.DurationSeconds),
                    EndedUtc = entry.Ended, ElapsedSeconds = entry.DurationSeconds, Winner = entry.Winner, Reason = entry.Reason,
                    ObjectivesTaken = entry.ObjectivesTaken, ObjectivesNeeded = entry.ObjectivesNeeded, Completed = true,
                },
            };
            string key;
            try
            {
                key = ScoreboardArchive.Identity(record);
            }
            catch (System.IO.InvalidDataException)
            {
                continue;
            }

            items[key] = items.TryGetValue(key, out Item? found) ? found with { CloudId = entry.Id } : new Item(record, entry.Id, FindReplay(entry.RoundId));
        }

        _items.Clear();
        _items.AddRange(items.Values.OrderByDescending(i => i.Record.Scoreboard.StartedUtc));
        Touch();
    }

    private static LocalDemo? FindReplay(string? roundId) => string.IsNullOrEmpty(roundId) ? null
        : DemoLibrary.Local.FirstOrDefault(d => d.Header.RoundId == roundId || d.Index.Scoreboard?.RoundId == roundId);

    // ---- one round's words ----------------------------------------------------------------------------------------

    public static string Outcome(Item item)
    {
        RoundScoreboard board = item.Record.Scoreboard;
        string outcome = board.Partial ? "Incomplete" : board.Winner == 0 ? "Spies win" : board.Winner == 1 ? "Mercenaries win" : "Round stopped";
        if (board.Practice) outcome += " · Practice";
        if (item.Record.LeftEarly) outcome += " · Left early";
        return outcome;
    }

    public static string Metadata(Item item)
    {
        RoundScoreboard board = item.Record.Scoreboard;
        return $"{board.StartedUtc.ToLocalTime().ToString("dd MMM yyyy · HH:mm", CultureInfo.InvariantCulture)} · {DemoLibrary.ModeName(board.Mode)} · {Duration(board.ElapsedSeconds)}";
    }

    public static string Server(Item item) => string.IsNullOrWhiteSpace(item.Record.Scoreboard.Server) ? "Offline game" : item.Record.Scoreboard.Server;

    public static string ReplayLine(Item item) => item.Replay is { } demo
        ? demo.Playable ? "Replay saved on this computer" : "Replay recorded by another game version"
        : item.CloudId is null ? "No replay on this computer" : "No replay on this computer · details from the master";

    /// <summary>The round's end ("All objectives taken · objectives 3 of 3").</summary>
    public string Reason(Item item)
    {
        RoundScoreboard board = item.Record.Scoreboard;
        string text = board.Partial ? "Not finished" : MatchResults.ReasonText(board.Reason);
        if (board.ObjectivesNeeded > 0) text += $" · objectives {board.ObjectivesTaken} of {board.ObjectivesNeeded}";
        if (DetailsOf(item) is { } details) text += details.Match.Counted ? " · counted in career stats" : " · not counted in career stats";
        return text;
    }

    public static string Duration(double seconds)
    {
        int s = (int)Math.Max(0, seconds);
        return $"{s / 60:00}:{s % 60:00}";
    }

    /// <summary>The players to list: the master's details when loaded, else the saved scoreboard's rows.</summary>
    public IReadOnlyList<(string Name, int Team, int Score, int Kills, int Deaths, bool Local, bool Bot, bool Left)> Players(Item item)
    {
        if (DetailsOf(item) is { Players.Count: > 0 } details)
        {
            long account = _identity.Account;
            return details.Players.OrderBy(p => p.Team).ThenByDescending(p => p.Score)
                .Select(p => (p.Name ?? "Player", p.Team, p.Score, p.Kills, p.Deaths, account > 0 && p.AccountId == account, p.Bot, p.LeftEarly)).ToArray();
        }

        int local = item.Record.LocalParticipantId;
        return item.Record.Scoreboard.Rows.OrderBy(r => r.Team).ThenByDescending(r => r.Score)
            .Select(r => (r.Name, r.Team, r.Score, r.Kills, r.Deaths, item.Record.PlayedLocally && r.ParticipantId == local, r.Bot, r.LeftEarly)).ToArray();
    }

    // ---- stats and achievements -----------------------------------------------------------------------------------

    internal static bool UsesPrivateMastery => HubBridge.Existing?.Client == null && OnlineBridge.Existing?.Client is not
        { State: OnlineClientState.Lobby or OnlineClientState.Loading or OnlineClientState.Match or OnlineClientState.Results };

    private void BuildCareer(AccountInfo? account)
    {
        _stats = account is null ? [] : StatsModel.Build(account);
        _achievements.Clear();
        _rewardProgress.Clear();
        var privateRows = _privateProgress ? WeaponCareerView.PrivateMastery() : null;
        var reported = account?.Achievements.GroupBy(a => a.Id, StringComparer.Ordinal).ToDictionary(g => g.Key, g => g.First(), StringComparer.Ordinal)
            ?? new Dictionary<string, AchievementInfo>(StringComparer.Ordinal);

        // Without the master's numbers for an achievement (an older master), the account's stats still give the
        // progress of the stat-based ones; feats count from zero (AchievementsScreen.Rebuild).
        var local = new AccountProgress
        {
            Stats = account?.Stats ?? new AccountStats(),
            Earned = new HashSet<string>(reported.Values.Where(a => a.Earned is not null).Select(a => a.Id), StringComparer.Ordinal),
        };
        foreach (Achievement a in Achievements.All)
        {
            AchievementInfo? info = reported.GetValueOrDefault(a.Id);
            bool fromPractice = false;
            if (privateRows != null && privateRows.TryGetValue(a.Id, out var practice)
                && info?.Earned is null && (info == null || practice.Earned != null || practice.Current > info.Current))
            {
                info = practice;
                fromPractice = true;
            }
            if (info?.Earned is { } earned)
            {
                int completed = Math.Max(1, info.Target);
                _achievements.Add(new AchievementRow(a, true, completed, completed, earned, true, fromPractice));
            }
            else
            {
                (int current, int target) = info is { Target: > 0 } r ? (r.Current, r.Target) : a.Goal(local);
                _achievements.Add(new AchievementRow(a, false, Math.Max(0, current), Math.Max(0, target), null, info is not null, fromPractice));
            }
            foreach (string reward in a.Rewards)
            {
                if (!_rewardProgress.TryGetValue(reward, out var previous) || (!previous.Earned && _achievements[^1].Earned))
                    _rewardProgress[reward] = _achievements[^1];
            }
        }
        _nextUnlock = FindNextUnlock();
    }

    /// <summary>The closest reported cumulative achievement with a cosmetic still locked (the dashboard's next unlock), or -1.</summary>
    public int NextUnlockIndex() => _nextUnlock;
    public CosmeticItem? NextReward { get; private set; }

    private int FindNextUnlock()
    {
        AccountInfo? account = Account.Account;
        var unlocked = new HashSet<string>(account?.Unlocked ?? [], StringComparer.Ordinal);
        if (_privateProgress) unlocked.UnionWith(WeaponCareerView.PrivateUnlocks());
        int best = -1;
        NextReward = null;
        for (int i = 0; i < _achievements.Count; i++)
        {
            AchievementRow row = _achievements[i];
            if (row.Earned || !row.Reported || row.Target <= 1 || row.Current >= row.Target ||
                RewardItems(row).FirstOrDefault(item => !unlocked.Contains(item.Id)) is not { } reward)
            {
                continue;
            }

            if (best < 0 || (long)row.Current * _achievements[best].Target > (long)_achievements[best].Current * row.Target)
            {
                best = i;
                NextReward = reward;
            }
        }

        return best;
    }

    public static string CategoryName(AchievementCategory c) => c switch
    {
        AchievementCategory.Spy => "Spy",
        AchievementCategory.Merc => "Mercenary",
        AchievementCategory.Weapons => "Weapons",
        AchievementCategory.Gadgets => "Gadgets",
        AchievementCategory.Map => "Maps",
        _ => "General",
    };

    public static string CategoryNote(AchievementCategory c) => c switch
    {
        AchievementCategory.Weapons => "Each weapon tracks its own kills and headshot kills. Select a challenge to see its rewards.",
        AchievementCategory.Gadgets => "Both kits, Change Gear gadgets too, then a side's whole kit for its Mastercraft suit and guns",
        AchievementCategory.Map => "Win on each map 3 times as a spy and 3 times as a mercenary",
        _ => string.Empty,
    };

    public static string StatusText(AchievementRow row, bool longDate) => (row.Earned
        ? "Earned " + row.EarnedAt!.Value.ToLocalTime().ToString(longDate ? "d MMMM yyyy" : "d MMM yyyy", CultureInfo.InvariantCulture)
        : row.Target > 1 ? string.Create(CultureInfo.InvariantCulture, $"{Math.Min(row.Current, row.Target)} / {row.Target}") : "Not earned yet")
        + (row.Private ? " · Private" : string.Empty);

    /// <summary>What the achievement unlocks, one cosmetic a line ("Spy suit · Mastercraft (Spy)"), or why nothing.</summary>
    public static IEnumerable<CosmeticItem> RewardItems(AchievementRow row) =>
        row.Achievement.Rewards.Select(CosmeticCatalog.Find).OfType<CosmeticItem>().Where(i => ArmouryPresentation.IsVisible(i.Slot));

    public static string Rewards(AchievementRow row)
    {
        return string.Join("\n", RewardItems(row).Select(ArmouryPresentation.Describe));
    }

    public string AchievementsSummary => string.Create(CultureInfo.InvariantCulture,
        $"{AchievementsEarned} of {_achievements.Count} earned. ") + (_privateProgress
            ? "Weapon mastery tracks private match progress on this profile. Official matches track account progress separately."
            : "Official challenges count rounds with a signed-in player on each side.");

    public string StatsStatus => Account.Refreshing ? "Refreshing career stats…"
        : Account.Account is null
            ? Account.SignedInAccountId > 0 ? "Stats have not loaded. Use Refresh to try again."
            : AccountService.MasterUrl is null ? "No master server is set, so there is no account to show stats for."
            : "Sign in on the Account page to view your career stats."
        : Account.RefreshFailed ? "Career totals · Counted official rounds · Last received (the last refresh failed)"
        : "Career totals · Counted official rounds";

    private void Touch() => Revision++;
}
