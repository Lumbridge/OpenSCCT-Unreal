using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using OpenScct.Net.Cosmetics;
using OpenScct.Net.Discovery;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Social;

// DashboardInfoBlock crosses to C++ as it is: Source/OpenSCCT/Runtime/ScctDashboardApi.h mirrors it field for field;
// change both together and bump DashboardExports.ApiVersion. Bridge/Probes/SocialProbe ("dashboard") prints its size.

[Flags]
public enum DashboardFlags
{
    None = 0,

    /// <summary>A master server is set (with none, every feed is unavailable and nothing is asked).</summary>
    HasMaster = 1,

    /// <summary>A page that shows the dashboard is on screen: the feeds are being polled.</summary>
    Watched = 2,

    StatusLoading = 4,
    StatusUnavailable = 8,
    ChangelogLoading = 16,
    ChangelogUnavailable = 32,

    /// <summary>The master counts the players online (PlayersOnline is filled).</summary>
    PlayersKnown = 64,

    GamesKnown = 128,
    HubsKnown = 256,

    /// <summary>The data is made up (-ScctDashboardPreview): nothing is asked of the master.</summary>
    Preview = 512,

    /// <summary>A next unlock is known (the NextUnlock text and UnlockCurrent / UnlockTarget are filled).</summary>
    HasUnlock = 1024,
}

/// <summary>The dashboard's numbers (FScctDashboardInfo).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct DashboardInfoBlock
{
    public int Flags;

    /// <summary>Goes up whenever anything here or in the texts changed.</summary>
    public int Revision;

    public int PlayersOnline;
    public int GameSessions;
    public int GamePlayers;
    public int HubSessions;
    public int HubPlayers;

    /// <summary>Releases in the changelog (newest first).</summary>
    public int Releases;

    public int UnlockCurrent;
    public int UnlockTarget;

    /// <summary>Requests started so far this run, all four feeds together (proof that polling runs and stops).</summary>
    public int Requests;

    public int Reserved;
}

/// <summary>A text of the dashboard (DashboardExports.GetText).</summary>
public enum DashboardText
{
    /// <summary>"Master 1.0.0  build 20261027.1200 (abc1234)", or "Master: checking...", "Master: not reached", "Master: none set".</summary>
    MasterLine = 0,

    /// <summary>The player site's announcement (empty: none).</summary>
    Announcement = 1,

    /// <summary>The master's message of the day.</summary>
    Motd = 2,

    /// <summary>"Players online: 1,204" or "Players online: —".</summary>
    PlayersLine = 3,

    /// <summary>"3 games, 41 players · 1 hub, 6 players", or why it is not known.</summary>
    SessionsLine = 4,

    /// <summary>The newest release's tag, or "Latest update".</summary>
    LatestTitle = 5,

    /// <summary>Up to three highlights of the newest release, or what stands in for them ("Checking release notes...").</summary>
    LatestPreview = 6,

    /// <summary>"12 Nov 2026" (with " · Last received" when the master no longer answers), or a line inviting to read.</summary>
    LatestState = 7,

    /// <summary>The changelog page's status line ("The latest five releases · newest first").</summary>
    ChangelogStatus = 8,

    /// <summary>"Recent: W-L-W-D" (newest first), empty when the master gives none.</summary>
    RecentResults = 9,

    /// <summary>"Next unlock · Desert Pattern · 12 / 25".</summary>
    NextUnlock = 10,

    /// <summary>The announcement page's status line.</summary>
    AnnouncementStatus = 11,
}

/// <summary>
/// The main menu's live information (the Godot client's game/Scripts/Online/MenuDashboardService.cs, MenuDashboardModel
/// and MasterVersionCheck.cs): the master's status (players online, its announcement and message of the day), the game
/// and hub lists (how many sessions and players) and the changelog. Requests run in the background and start only while
/// Unreal says a dashboard page is on screen (<see cref="SetWatched"/>), every 30 s for the status and lists and every
/// 10 min for the changelog (2 min after a failure); while watched the account is refreshed too (AccountService limits
/// that to once a minute). A change of master or invite key discards every answer. The master's own version line is
/// asked once per run whatever is on screen (again a minute after a failure, and when the master changes), as the
/// build label in the corner shows it. Runs inside the social table's Update on the game thread.
/// </summary>
public sealed class MenuDashboard
{
    public const long StatusEveryMs = 30_000;
    public const long ChangelogEveryMs = 600_000;
    private const long FailedRetryMs = 120_000;
    private const long VersionRetryMs = 60_000;
    public const string ReleasesUrl = "https://github.com/Lumbridge/OpenSCCT/releases";

    private readonly SocialBridge _owner;
    private readonly long _epoch = Stopwatch.GetTimestamp();
    private readonly Feed<MasterStatus> _status = new("status", StatusEveryMs);
    private readonly Feed<ServerListResponse> _games = new("games", StatusEveryMs);
    private readonly Feed<ServerListResponse> _hubs = new("hubs", StatusEveryMs);
    private readonly Feed<ChangelogResponse> _changelog = new("changelog", ChangelogEveryMs);
    private (string Master, string Invite) _identity = (string.Empty, string.Empty);
    private bool _watched;
    private bool _wasActive;
    private int _revision = 1;
    private bool _preview;

    // The master version line (MasterVersionCheck).
    private Task<MasterStatus>? _version;
    private string _versionUrl = string.Empty;
    private long _versionFailedAt = -1;

    internal MenuDashboard(SocialBridge owner)
    {
        _owner = owner;
    }

    private long NowMs => (long)Stopwatch.GetElapsedTime(_epoch).TotalMilliseconds;

    private static OnlineMaster? Master => OnlineBridge.Existing?.Master;

    public int Revision => _revision;

    public int Requests => _status.Requests + _games.Requests + _hubs.Requests + _changelog.Requests;

    /// <summary>Shows made-up data (-ScctDashboardPreview, as the Godot client's smoke previews): nothing is asked.</summary>
    public void SetPreview(bool preview)
    {
        if (_preview == preview)
        {
            return;
        }

        _preview = preview;
        if (preview)
        {
            _status.Set(new MasterStatus
            {
                PlayersOnline = 1204, MasterVersion = "1.0.0", MasterBuild = "20261110.0930", MasterCommit = "a1b2c3d",
                Motd = "Preview: the message of the day.",
                Announcement = "Preview: the community night is on Saturday at 20:00 UTC on the EU servers.",
            });
            _games.Set(new ServerListResponse
            {
                Servers = [new ServerEntry { Kind = ServerKind.Game, Players = 4 }, new ServerEntry { Kind = ServerKind.Game, Players = 3 }],
            });
            _hubs.Set(new ServerListResponse { Servers = [new ServerEntry { Kind = ServerKind.Hub, Players = 6 }] });
            _changelog.Set(new ChangelogResponse
            {
                Entries =
                [
                    new ChangelogEntry
                    {
                        Tag = "v0.9.6", Name = "Preview release", Published = new DateTimeOffset(2026, 11, 12, 0, 0, 0, TimeSpan.Zero),
                        Notes = "## Highlights\n- **Rejoin** a round still being played after a crash.\n- The main menu shows the players online and what is new.\n- Demos can be watched with an X-ray view.\n\nMore fixes.",
                        Url = ReleasesUrl + "/tag/v0.9.6",
                    },
                    new ChangelogEntry
                    {
                        Tag = "v0.9.5", Name = "Earlier preview release", Published = new DateTimeOffset(2026, 10, 30, 0, 0, 0, TimeSpan.Zero),
                        Notes = "- Custom crosshairs.\n- Sequence audio follows its actors.", Url = ReleasesUrl + "/tag/v0.9.5",
                    },
                ],
            });
        }

        Touch();
    }

    /// <summary>A dashboard page is on screen (true) or no longer is (false).</summary>
    public void SetWatched(bool watched)
    {
        if (_watched == watched)
        {
            return;
        }

        _watched = watched;
        Touch();
    }

    /// <summary>Once a frame on the game thread (the social table's Update).</summary>
    public void Update()
    {
        if (_preview)
        {
            return;
        }

        bool changed = false;
        OnlineMaster? master = Master;
        var identity = ((master?.Url ?? string.Empty).TrimEnd('/'), master?.InviteKey ?? string.Empty);
        if (_identity != identity)
        {
            _identity = identity;
            _status.Reset();
            _games.Reset();
            _hubs.Reset();
            _changelog.Reset();
            changed = true;
        }

        long now = NowMs;
        bool active = _watched && _identity.Master.Length > 0;
        if (active != _wasActive)
        {
            _wasActive = active;
            _owner.Log(active
                ? $"Dashboard: shown, polling {_identity.Master} (status and lists every {StatusEveryMs / 1000} s, changelog every {ChangelogEveryMs / 60000} min)"
                : $"Dashboard: {(_watched ? "no master server set" : "hidden")}, polling stopped after {Requests} requests");
        }

        if (_identity.Master.Length == 0)
        {
            changed |= _status.Disable();
            changed |= _games.Disable();
            changed |= _hubs.Disable();
            changed |= _changelog.Disable();
        }
        else
        {
            changed |= Pumped(_status.Pump(now, active, () => AccountService.Call(c => c.StatusAsync(), withToken: false)), _status);
            changed |= Pumped(_games.Pump(now, active, () => AccountService.Call(c => c.ListAsync(kind: ServerKind.Game), withToken: false)), _games);
            changed |= Pumped(_hubs.Pump(now, active, () => AccountService.Call(c => c.ListAsync(kind: ServerKind.Hub), withToken: false)), _hubs);
            changed |= Pumped(_changelog.Pump(now, active, () => AccountService.Call(c => c.ChangelogAsync(), withToken: false),
                r => r.Unavailable, MergeChangelog), _changelog);
            if (active)
            {
                _owner.Account.Refresh();
            }
        }

        changed |= PumpVersion(now);
        if (changed)
        {
            Touch();
        }
    }

    private bool Pumped<T>(Feed<T>.Result result, Feed<T> feed)
        where T : class
    {
        if (result.Answered)
        {
            _owner.Log(feed.Unavailable
                ? $"Dashboard: {feed.Name} not reached ({result.Error}){(feed.Value is not null ? ", keeping the last answer" : string.Empty)}"
                : $"Dashboard: {feed.Name} answered{Describe(feed)}");
        }

        return result.Changed;
    }

    private string Describe<T>(Feed<T> feed)
        where T : class =>
        feed.Value switch
        {
            MasterStatus s => s.PlayersOnline is { } p ? $", {p} players online" : ", no player count",
            ServerListResponse l => $", {l.Servers.Count} listed",
            ChangelogResponse c => $", {c.Entries.Count} releases",
            _ => string.Empty,
        };

    private void Touch() => _revision++;

    // ---- the master version line ----------------------------------------------------------------------------------

    private bool PumpVersion(long now)
    {
        string url = Master?.Url ?? string.Empty;
        if (url.Length == 0)
        {
            bool had = _version is not null;
            _version = null;
            _versionUrl = string.Empty;
            return had;
        }

        if (_version is null || !string.Equals(url, _versionUrl, StringComparison.Ordinal) ||
            (_version.IsFaulted && _versionFailedAt >= 0 && now - _versionFailedAt >= VersionRetryMs))
        {
            _versionUrl = url;
            _versionFailedAt = -1;
            _version = AccountService.Call(c => c.StatusAsync(), withToken: false);
            return true;
        }

        if (_version.IsCompleted && !_version.IsCompletedSuccessfully && _versionFailedAt < 0)
        {
            _versionFailedAt = now;
            _owner.Log("Master version: not reached (" + AccountService.ErrorOf(_version) + "), asked again in a minute");
            return true;
        }

        if (_version.IsCompletedSuccessfully && _versionFailedAt != -2)
        {
            _versionFailedAt = -2;
            _owner.Log("Master version: " + VersionText(_version.Result));
            return true;
        }

        return false;
    }

    /// <summary>"Master 1.0.0  build 20261027.1200 (abc1234)"; a master older than its own version shows the game version it was built with.</summary>
    public static string VersionText(MasterStatus status)
    {
        if (status.MasterVersion is not { Length: > 0 } version)
        {
            return $"Master (game {status.Version})";
        }

        string text = "Master " + version;
        if (status.MasterBuild is { Length: > 0 } build && build != "0")
        {
            text += "  build " + build;
        }

        return status.MasterCommit is { Length: > 0 } commit ? $"{text} ({commit})" : text;
    }

    private string MasterLine()
    {
        if (_preview && _status.Value is { } shown)
        {
            return VersionText(shown);
        }

        if ((Master?.Url ?? string.Empty).Length == 0)
        {
            return "Master: none set";
        }

        if (_version is { IsCompletedSuccessfully: true } done)
        {
            return VersionText(done.Result);
        }

        // A dashboard status answer says it as well.
        if (_status.Value is { } status && !_status.Unavailable)
        {
            return VersionText(status);
        }

        return _version is null || !_version.IsCompleted ? "Master: checking..." : "Master: not reached";
    }

    // ---- what Unreal reads ----------------------------------------------------------------------------------------

    public void Fill(ref DashboardInfoBlock block)
    {
        DashboardFlags flags = DashboardFlags.None;
        if (_identity.Master.Length > 0 || _preview)
        {
            flags |= DashboardFlags.HasMaster;
        }

        if (_watched)
        {
            flags |= DashboardFlags.Watched;
        }

        if (_preview)
        {
            flags |= DashboardFlags.Preview;
        }

        if (_status.Loading)
        {
            flags |= DashboardFlags.StatusLoading;
        }

        if (_status.Unavailable)
        {
            flags |= DashboardFlags.StatusUnavailable;
        }

        bool changelogUnavailable = _changelog.Unavailable || _changelog.Value?.Unavailable == true;
        if (_changelog.Loading)
        {
            flags |= DashboardFlags.ChangelogLoading;
        }

        if (changelogUnavailable)
        {
            flags |= DashboardFlags.ChangelogUnavailable;
        }

        if (!_status.Unavailable && _status.Value?.PlayersOnline is { } players)
        {
            flags |= DashboardFlags.PlayersKnown;
            block.PlayersOnline = players;
        }

        if (!_games.Unavailable && _games.Value is { } games)
        {
            flags |= DashboardFlags.GamesKnown;
            (block.GamePlayers, block.GameSessions) = Activity(games, ServerKind.Game);
        }

        if (!_hubs.Unavailable && _hubs.Value is { } hubs)
        {
            flags |= DashboardFlags.HubsKnown;
            (block.HubPlayers, block.HubSessions) = Activity(hubs, ServerKind.Hub);
        }

        if (NextUnlock(_owner.Account.Account) is { } unlock)
        {
            flags |= DashboardFlags.HasUnlock;
            block.UnlockCurrent = unlock.Current;
            block.UnlockTarget = unlock.Target;
        }

        block.Flags = (int)flags;
        block.Revision = _revision + _owner.Account.Revision;
        block.Releases = Math.Min(5, _changelog.Value?.Entries.Count ?? 0);
        block.Requests = Requests;
    }

    public string Text(DashboardText kind)
    {
        ChangelogEntry? latest = _changelog.Value?.Entries.FirstOrDefault();
        bool changelogUnavailable = _changelog.Unavailable || _changelog.Value?.Unavailable == true;
        switch (kind)
        {
            case DashboardText.MasterLine:
                return MasterLine();
            case DashboardText.Announcement:
                return _status.Value?.Announcement?.Trim() ?? string.Empty;
            case DashboardText.Motd:
                return _status.Value?.Motd?.Trim() ?? string.Empty;
            case DashboardText.PlayersLine:
                return !_status.Unavailable && _status.Value?.PlayersOnline is { } count
                    ? string.Create(CultureInfo.InvariantCulture, $"Players online: {count:N0}")
                    : "Players online: —";
            case DashboardText.SessionsLine:
                return SessionsLine();
            case DashboardText.LatestTitle:
                return latest is null ? "Latest update" : latest.Tag;
            case DashboardText.LatestPreview:
                if (latest is not null)
                {
                    string preview = Preview(latest.Notes);
                    return string.IsNullOrWhiteSpace(preview) ? "No release notes were provided for this version." : preview;
                }

                return _identity.Master.Length == 0 && !_preview ? "No master server is set, so the release notes cannot be fetched."
                    : _changelog.Loading || (_changelog.Requests == 0 && !changelogUnavailable) ? "Checking release notes..."
                    : changelogUnavailable ? "Release notes are currently unavailable." : "No published updates yet.";
            case DashboardText.LatestState:
                return latest is not null
                    ? latest.Published.ToString("d MMM yyyy", CultureInfo.InvariantCulture) + (changelogUnavailable ? " · Last received" : string.Empty)
                    : "Read the latest features, improvements and fixes.";
            case DashboardText.ChangelogStatus:
                return changelogUnavailable
                    ? "Release notes are currently unavailable." + (_changelog.Value?.Entries.Count > 0 ? " Showing the last notes received." : string.Empty)
                    : _changelog.Loading ? "Checking for updates..."
                    : _changelog.Value?.Entries.Count > 0 ? "The latest five releases · newest first" : "No published updates yet.";
            case DashboardText.RecentResults:
                return _owner.Account.Account?.RecentResults is { Count: > 0 } results ? "Recent: " + string.Join("-", results.Take(9)) : string.Empty;
            case DashboardText.NextUnlock:
                return NextUnlock(_owner.Account.Account) is { } unlock
                    ? string.Create(CultureInfo.InvariantCulture, $"Next unlock · {unlock.RewardName} · {unlock.Current:N0} / {unlock.Target:N0}")
                    : string.Empty;
            case DashboardText.AnnouncementStatus:
                return _status.Unavailable && _status.Value is not null ? "Showing the last announcement received. The service is currently unavailable."
                    : _status.Loading ? "Checking for announcements..."
                    : _status.Unavailable ? "Announcements are currently unavailable." : string.Empty;
            default:
                return string.Empty;
        }
    }

    private string SessionsLine()
    {
        if (_identity.Master.Length == 0 && !_preview)
        {
            return "No master server is set.";
        }

        if (_games.Value is null && _hubs.Value is null)
        {
            return _games.Unavailable && _hubs.Unavailable ? "The master server is not answering." : "Asking the master server...";
        }

        var parts = new List<string>();
        if (_games.Value is { } games)
        {
            (int players, int sessions) = Activity(games, ServerKind.Game);
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"{sessions:N0} game{(sessions == 1 ? string.Empty : "s")}, {players:N0} playing"));
        }

        if (_hubs.Value is { } hubs)
        {
            (int players, int sessions) = Activity(hubs, ServerKind.Hub);
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"{sessions:N0} hub{(sessions == 1 ? string.Empty : "s")}, {players:N0} in them"));
        }

        string line = string.Join(" · ", parts);
        return _games.Unavailable || _hubs.Unavailable ? line + " (last received)" : line;
    }

    /// <summary>The release at <paramref name="index"/> (newest first, at most five): false past the end.</summary>
    public bool Release(int index, out string tag, out string name, out string date, out string notes, out string url)
    {
        tag = name = date = notes = url = string.Empty;
        IReadOnlyList<ChangelogEntry>? entries = _changelog.Value?.Entries;
        if (entries is null || index < 0 || index >= Math.Min(5, entries.Count))
        {
            return false;
        }

        ChangelogEntry release = entries[index];
        tag = release.Tag;
        name = release.Name;
        date = release.Published.ToString("d MMM yyyy", CultureInfo.InvariantCulture);
        notes = PlainNotes(release.Notes);
        // The destination is built from a fixed HTTPS origin, never taken from the master's markup.
        url = ReleasesUrl + "/tag/" + Uri.EscapeDataString(release.Tag);
        return true;
    }

    // ---- MenuDashboardModel ---------------------------------------------------------------------------------------

    private static readonly Regex Links = new(@"!?\[([^\]]*)\]\([^\r\n)]*\)", RegexOptions.NonBacktracking);
    private static readonly Regex Html = new(@"<[^>]*>", RegexOptions.NonBacktracking);

    /// <summary>A restarted master may have lost its release cache while GitHub is unavailable.</summary>
    public static ChangelogResponse MergeChangelog(ChangelogResponse? previous, ChangelogResponse response) =>
        response.Unavailable && response.Entries.Count == 0 && previous is { Entries.Count: > 0 }
            ? previous with { Unavailable = true }
            : response;

    /// <summary>Readable release-note text for plain labels, never HTML or Markdown.</summary>
    public static string PlainNotes(string? notes)
    {
        if (string.IsNullOrWhiteSpace(notes))
        {
            return string.Empty;
        }

        string input = notes.Length > 65_536 ? notes[..65_536] : notes;
        input = WebUtility.HtmlDecode(Html.Replace(Links.Replace(input, "$1"), string.Empty));
        var lines = new List<string>();
        foreach (string raw in input.Replace("\r", string.Empty, StringComparison.Ordinal).Split('\n'))
        {
            string line = raw.Trim();
            if (line.StartsWith("```", StringComparison.Ordinal) || line.StartsWith("~~~", StringComparison.Ordinal))
            {
                continue;
            }

            if (line.Length > 0 && line.All(c => c is '-' or '*' or '_'))
            {
                continue;
            }

            line = line.TrimStart('#', '>').TrimStart();
            if (line.StartsWith("- ", StringComparison.Ordinal) || line.StartsWith("* ", StringComparison.Ordinal) ||
                line.StartsWith("+ ", StringComparison.Ordinal))
            {
                line = "• " + line[2..];
            }

            line = line.Replace("**", string.Empty, StringComparison.Ordinal)
                .Replace("__", string.Empty, StringComparison.Ordinal).Replace("`", string.Empty, StringComparison.Ordinal);
            if (line.Length > 0 || (lines.Count > 0 && lines[^1].Length > 0))
            {
                lines.Add(line);
            }
        }

        return string.Join('\n', lines).Trim();
    }

    /// <summary>Bullet highlights rather than release headings; prose when there are no bullets.</summary>
    public static string Preview(string? notes, int maxLines = 3)
    {
        string[] lines = PlainNotes(notes).Split('\n', StringSplitOptions.RemoveEmptyEntries);
        string[] bullets = lines.Where(l => l.StartsWith("• ", StringComparison.Ordinal)).ToArray();
        return string.Join('\n', (bullets.Length > 0 ? bullets : lines).Take(Math.Clamp(maxLines, 1, 5))
            .Select(l => l.Length > 160 ? l[..157].TrimEnd() + "…" : l));
    }

    private static (int Players, int Sessions) Activity(ServerListResponse list, ServerKind kind)
    {
        ServerEntry[] sessions = list.Servers.Where(s => s.Kind == kind).ToArray();
        return ((int)Math.Min(int.MaxValue, sessions.Sum(s => (long)Math.Max(0, s.Players))), sessions.Length);
    }

    private sealed record Unlock(string RewardName, int Current, int Target);

    /// <summary>The closest reported cumulative achievement with a known cosmetic still locked on the account.</summary>
    private static Unlock? NextUnlock(AccountInfo? account)
    {
        if (account is null)
        {
            return null;
        }

        var reported = account.Achievements.GroupBy(a => a.Id, StringComparer.Ordinal)
            .ToDictionary(g => g.Key, g => g.First(), StringComparer.Ordinal);
        var unlocked = new HashSet<string>(account.Unlocked, StringComparer.Ordinal);
        Unlock? best = null;
        foreach (Achievement achievement in Achievements.All)
        {
            if (!reported.TryGetValue(achievement.Id, out AchievementInfo? progress) || progress.Earned is not null ||
                progress.Target <= 1 || progress.Current >= progress.Target)
            {
                continue;
            }

            CosmeticItem? reward = achievement.Rewards.Where(id => !unlocked.Contains(id))
                .Select(CosmeticCatalog.Find).FirstOrDefault(item => item is not null);
            if (reward is null)
            {
                continue;
            }

            var candidate = new Unlock(reward.Name, Math.Max(0, progress.Current), progress.Target);
            if (best is null || (long)candidate.Current * best.Target > (long)best.Current * candidate.Target)
            {
                best = candidate;
            }
        }

        return best;
    }

    // ---- one polled request (MenuDashboardRequest) ----------------------------------------------------------------

    private sealed class Feed<T>(string name, long interval)
        where T : class
    {
        private Task<T>? _task;
        private long _next;

        public readonly record struct Result(bool Changed, bool Answered, string Error);

        public string Name { get; } = name;

        public T? Value { get; private set; }

        public bool Loading { get; private set; }

        public bool Unavailable { get; private set; }

        public int Requests { get; private set; }

        public void Set(T value)
        {
            Discard();
            Value = value;
            Loading = false;
            Unavailable = false;
        }

        public void Reset()
        {
            Discard();
            _next = 0;
            Value = null;
            Loading = false;
            Unavailable = false;
        }

        public bool Disable()
        {
            bool changed = Value is not null || Loading || !Unavailable;
            Discard();
            _next = 0;
            Value = null;
            Loading = false;
            Unavailable = true;
            return changed;
        }

        private void Discard()
        {
            if (_task is { } task)
            {
                _ = task.ContinueWith(static t => { _ = t.Exception; }, TaskContinuationOptions.OnlyOnFaulted);
            }

            _task = null;
        }

        public Result Pump(long now, bool active, Func<Task<T>> start, Func<T, bool>? unavailable = null, Func<T?, T, T>? merge = null)
        {
            bool changed = false, answered = false;
            string error = string.Empty;
            if (_task is { IsCompleted: true } task)
            {
                _task = null;
                Loading = false;
                answered = true;
                if (task.IsCompletedSuccessfully)
                {
                    Value = merge is not null ? merge(Value, task.Result) : task.Result;
                    Unavailable = unavailable?.Invoke(task.Result) == true;
                    if (Unavailable)
                    {
                        error = "the master has no notes to give";
                    }
                }
                else
                {
                    error = AccountService.ErrorOf(task);
                    Unavailable = true;
                }

                _next = now + (Unavailable ? Math.Min(interval, FailedRetryMs) : interval);
                changed = true;
            }

            if (active && _task is null && now >= _next)
            {
                _task = start();
                Requests++;
                Loading = true;
                changed = true;
            }

            return new Result(changed, answered, error);
        }
    }
}
