using System;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core.Engine;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Social;

namespace OpenScct.UnrealBridge;

[Flags]
public enum CareerFlags
{
    None = 0,
    HasMaster = 1,
    SignedIn = 2,
    HistoryLoading = 4,
    HistoryMore = 8,
    DetailsLoading = 16,

    /// <summary>The master described the account: the stats and the achievements' progress are its.</summary>
    AccountKnown = 32,

    /// <summary>The master gives detailed counters (career feats); without them those rows read "—".</summary>
    HasCareer = 64,

    /// <summary>Unreal named the archive's folder: rounds played are saved.</summary>
    Capturing = 128,
    Preview = 256,
    AccountLoading = 512,
}

/// <summary>The career table's state (CareerApi.GetInfo). Mirrors FScctCareerInfo.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct CareerInfoBlock
{
    public int Flags;
    public int Revision;
    public int Matches;
    public int LocalMatches;
    public int Stats;
    public int Achievements;
    public int AchievementsEarned;
    public int NextUnlock;
    public int Saved;
    public int StatCategories;
    public int Reserved0;
    public int Reserved1;
}

[Flags]
public enum CareerMatchFlags
{
    None = 0,
    Saved = 1,
    Online = 2,
    Partial = 4,
    Practice = 8,
    LeftEarly = 16,
    Completed = 32,
    DetailsLoaded = 64,
    HasTimeline = 128,
    TimelineTruncated = 256,
    Counted = 512,
    HasReplay = 1024,
    ReplayPlayable = 2048,
}

/// <summary>One round of the match history (CareerApi.GetMatch). Mirrors FScctCareerMatch.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct CareerMatchBlock
{
    public int Flags;
    public int Winner;
    public int Mode;
    public int DurationSeconds;
    public int Players;
    public int Events;
    public int LocalTeam;
    public int Reserved;
    public long MatchId;
    public long StartedUnix;
}

[Flags]
public enum CareerPlayerFlags
{
    None = 0,
    Local = 1,
    Bot = 2,
    LeftEarly = 4,
}

/// <summary>One player of a round (CareerApi.GetPlayer). Mirrors FScctCareerPlayer.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct CareerPlayerBlock
{
    public int Team;
    public int Score;
    public int Kills;
    public int Deaths;
    public int Flags;
    public int Reserved;
}

[Flags]
public enum CareerAchievementFlags
{
    None = 0,
    Earned = 1,
    Reported = 2,
    HasRewards = 4,
    NextUnlock = 8,
}

/// <summary>One achievement (CareerApi.GetAchievement). Mirrors FScctCareerAchievement.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct CareerAchievementBlock
{
    public int Category;
    public int Current;
    public int Target;
    public int Flags;
    public long EarnedUnix;
}

/// <summary>GetString's kinds. Index and Sub are what the kind says (a round and a player, an achievement...).</summary>
public enum CareerString
{
    HistoryStatus = 0,
    HistoryError = 1,
    MatchMap = 2,         // index: the round
    MatchOutcome = 3,
    MatchMetadata = 4,
    MatchServer = 5,
    MatchReplay = 6,
    MatchReplayPath = 7,
    MatchReason = 8,
    PlayerName = 9,       // index: the round, sub: the player
    EventTime = 10,       // index: the round, sub: the event
    EventText = 11,
    StatCategory = 12,    // index: the row
    StatLabel = 13,
    StatValue = 14,
    StatCategoryName = 15, // index: the category (StatsModel.Categories)
    AchievementId = 16,   // index: the achievement
    AchievementTitle = 17,
    AchievementDescription = 18,
    AchievementStatus = 19,
    AchievementStatusLong = 20,
    AchievementRewards = 21,
    CategoryName = 22,    // index: AchievementCategory
    CategoryNote = 23,
    StatsStatus = 24,
    AchievementsSummary = 25,
    DetailsError = 26,
    ArchiveFolder = 27,
    AchievementRewardIds = 28,
    AchievementWeapon = 29,
}

/// <summary>Command's kinds.</summary>
public enum CareerCommand
{
    RefreshHistory = 0,
    MoreHistory = 1,
    LoadDetails = 2, // index: the round
    RefreshAccount = 3,
    RefreshAccountNow = 4,
}

/// <summary>
/// The Stats, Match history and Achievements pages' table and the scoreboard capture's folder
/// (Source/OpenSCCT/Runtime/ScctCareerApi.h mirrors it). Every entry catches its exceptions; an unexpected one goes to
/// the main table's LastError and the call returns its empty answer. Strings are UTF-16, zero terminated. Every read
/// takes the master's answers first (<see cref="CareerModel.Pump"/>). New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct CareerApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, void> SetFolder;
    public delegate* unmanaged[Cdecl]<CareerInfoBlock*, int> GetInfo;
    public delegate* unmanaged[Cdecl]<int, int, void> Command;
    public delegate* unmanaged[Cdecl]<int, int, int, char*, int, int> GetString;
    public delegate* unmanaged[Cdecl]<int, CareerMatchBlock*, int> GetMatch;
    public delegate* unmanaged[Cdecl]<int, int, CareerPlayerBlock*, int> GetPlayer;
    public delegate* unmanaged[Cdecl]<int, CareerAchievementBlock*, int> GetAchievement;
}

public static unsafe class CareerExports
{
    public const int ApiVersion = 1;

    private static CareerModel Model => CareerModel.Instance;

    [UnmanagedCallersOnly]
    public static int GetApi(CareerApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->SetFolder = &SetFolder;
        api->GetInfo = &GetInfo;
        api->Command = &Command;
        api->GetString = &GetString;
        api->GetMatch = &GetMatch;
        api->GetPlayer = &GetPlayer;
        api->GetAchievement = &GetAchievement;
        return ApiVersion;
    }

    /// <summary>The archive's folder (Unreal's Saved/Scoreboards): from now on every round played is saved there.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetFolder(char* folder)
    {
        try
        {
            string path = folder == null ? string.Empty : new string(folder);
            if (path.Length > 0)
            {
                ScoreboardHistory.Configure(path);
            }
        }
        catch (Exception e)
        {
            Failed(e);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInfo(CareerInfoBlock* info)
    {
        if (info == null)
        {
            return 0;
        }

        try
        {
            Model.Pump();
            AccountService account = SocialBridge.Instance.Account;
            CareerFlags flags = CareerFlags.None;
            if (AccountService.MasterUrl is not null) flags |= CareerFlags.HasMaster;
            if (account.SignedInAccountId > 0) flags |= CareerFlags.SignedIn;
            if (Model.HistoryLoading) flags |= CareerFlags.HistoryLoading;
            if (Model.HistoryMore) flags |= CareerFlags.HistoryMore;
            if (Model.DetailsLoading) flags |= CareerFlags.DetailsLoading;
            if (account.Account is not null) flags |= CareerFlags.AccountKnown;
            if (account.Account?.Career is not null) flags |= CareerFlags.HasCareer;
            if (ScoreboardHistory.Store is not null) flags |= CareerFlags.Capturing;
            if (account.Preview) flags |= CareerFlags.Preview;
            if (account.Refreshing) flags |= CareerFlags.AccountLoading;
            *info = new CareerInfoBlock
            {
                Flags = (int)flags,
                Revision = Model.Revision + ScoreboardHistory.Revision,
                Matches = Model.Items.Count,
                LocalMatches = Model.Items.Count(i => i.Record.PlayedLocally),
                Stats = Model.Stats.Count,
                Achievements = Model.AchievementRows.Count,
                AchievementsEarned = Model.AchievementsEarned,
                NextUnlock = Model.NextUnlockIndex(),
                Saved = ScoreboardHistory.Saved,
                StatCategories = OpenScctGame.UI.StatsModel.Categories.Length,
            };
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            *info = default;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Command(int kind, int index)
    {
        try
        {
            switch ((CareerCommand)kind)
            {
                case CareerCommand.RefreshHistory:
                    Model.RefreshHistory();
                    break;
                case CareerCommand.MoreHistory:
                    Model.MoreHistory();
                    break;
                case CareerCommand.LoadDetails:
                    Model.LoadDetails(index);
                    break;
                case CareerCommand.RefreshAccount:
                    SocialBridge.Instance.Account.Refresh();
                    break;
                case CareerCommand.RefreshAccountNow:
                    SocialBridge.Instance.Account.Refresh(force: true);
                    break;
            }
        }
        catch (Exception e)
        {
            Failed(e);
        }
    }

    /// <summary>A text (<see cref="CareerString"/>). Returns its full length (0: none, or past the end).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetString(int kind, int index, int sub, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, Text((CareerString)kind, index, sub));
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    private static string Text(CareerString kind, int index, int sub)
    {
        CareerModel model = Model;
        CareerModel.Item? item = index >= 0 && index < model.Items.Count ? model.Items[index] : null;
        CareerModel.AchievementRow? achievement = index >= 0 && index < model.AchievementRows.Count ? model.AchievementRows[index] : null;
        OpenScctGame.UI.StatsModel.Row? stat = index >= 0 && index < model.Stats.Count ? model.Stats[index] : null;
        switch (kind)
        {
            case CareerString.HistoryStatus: return model.Status;
            case CareerString.HistoryError: return model.Error;
            case CareerString.DetailsError: return model.DetailsError;
            case CareerString.StatsStatus: return model.StatsStatus;
            case CareerString.AchievementsSummary: return model.AchievementsSummary;
            case CareerString.ArchiveFolder: return ScoreboardHistory.Store?.DirectoryPath ?? string.Empty;
            case CareerString.StatCategoryName:
                return index >= 0 && index < OpenScctGame.UI.StatsModel.Categories.Length ? OpenScctGame.UI.StatsModel.Categories[index] : string.Empty;
            case CareerString.CategoryName:
                return Enum.IsDefined(typeof(OpenScct.Net.Cosmetics.AchievementCategory), index) ? CareerModel.CategoryName((OpenScct.Net.Cosmetics.AchievementCategory)index) : string.Empty;
            case CareerString.CategoryNote:
                return Enum.IsDefined(typeof(OpenScct.Net.Cosmetics.AchievementCategory), index) ? CareerModel.CategoryNote((OpenScct.Net.Cosmetics.AchievementCategory)index) : string.Empty;
        }

        if (kind is >= CareerString.MatchMap and <= CareerString.EventText)
        {
            if (item is null)
            {
                return string.Empty;
            }

            switch (kind)
            {
                case CareerString.MatchMap: return item.Record.Scoreboard.Map;
                case CareerString.MatchOutcome: return CareerModel.Outcome(item);
                case CareerString.MatchMetadata: return CareerModel.Metadata(item);
                case CareerString.MatchServer: return CareerModel.Server(item);
                case CareerString.MatchReplay: return CareerModel.ReplayLine(item);
                case CareerString.MatchReplayPath: return item.Replay?.Path ?? string.Empty;
                case CareerString.MatchReason: return model.Reason(item);
                case CareerString.PlayerName:
                    var players = model.Players(item);
                    return sub >= 0 && sub < players.Count ? players[sub].Name : string.Empty;
                case CareerString.EventTime:
                case CareerString.EventText:
                    if (model.DetailsOf(item)?.Events is not { } events || sub < 0 || sub >= events.Count)
                    {
                        return string.Empty;
                    }

                    return kind == CareerString.EventTime ? CareerModel.Duration(events[sub].Seconds) : events[sub].Text;
            }
        }

        if (kind is >= CareerString.StatCategory and <= CareerString.StatValue)
        {
            return stat is null ? string.Empty : kind == CareerString.StatCategory ? stat.Category : kind == CareerString.StatLabel ? stat.Label : stat.Value;
        }

        if (achievement is null)
        {
            return string.Empty;
        }

        return kind switch
        {
            CareerString.AchievementId => achievement.Achievement.Id,
            CareerString.AchievementTitle => achievement.Achievement.Title,
            CareerString.AchievementDescription => achievement.Achievement.Description,
            CareerString.AchievementStatus => CareerModel.StatusText(achievement, false),
            CareerString.AchievementStatusLong => CareerModel.StatusText(achievement, true),
            CareerString.AchievementRewards => CareerModel.Rewards(achievement),
            CareerString.AchievementRewardIds => string.Join("\n", CareerModel.RewardItems(achievement).Select(i => i.Id)),
            CareerString.AchievementWeapon => MercPrimaryMastery.All.FirstOrDefault(g =>
                achievement.Achievement.Id.StartsWith("primary_" + g.Id + "_", StringComparison.Ordinal))?.Name ?? string.Empty,
            _ => string.Empty,
        };
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMatch(int index, CareerMatchBlock* match)
    {
        if (match == null)
        {
            return 0;
        }

        try
        {
            *match = default;
            CareerModel model = Model;
            if (index < 0 || index >= model.Items.Count)
            {
                return 0;
            }

            CareerModel.Item item = model.Items[index];
            var board = item.Record.Scoreboard;
            var details = model.DetailsOf(item);
            CareerMatchFlags flags = CareerMatchFlags.None;
            if (item.Record.PlayedLocally || item.Record.ImportedReplay) flags |= CareerMatchFlags.Saved;
            if (item.CloudId is not null) flags |= CareerMatchFlags.Online;
            if (board.Partial) flags |= CareerMatchFlags.Partial;
            if (board.Practice) flags |= CareerMatchFlags.Practice;
            if (item.Record.LeftEarly) flags |= CareerMatchFlags.LeftEarly;
            if (board.Completed) flags |= CareerMatchFlags.Completed;
            if (details is not null) flags |= CareerMatchFlags.DetailsLoaded;
            if (details?.Events is not null) flags |= CareerMatchFlags.HasTimeline;
            if (details?.TimelineTruncated == true) flags |= CareerMatchFlags.TimelineTruncated;
            if (details?.Match.Counted == true) flags |= CareerMatchFlags.Counted;
            if (item.Replay is not null) flags |= CareerMatchFlags.HasReplay;
            if (item.Replay?.Playable == true) flags |= CareerMatchFlags.ReplayPlayable;
            var players = model.Players(item);
            int local = players.ToList().FindIndex(p => p.Local);
            *match = new CareerMatchBlock
            {
                Flags = (int)flags,
                Winner = board.Winner is 0 or 1 ? board.Winner : -1,
                Mode = board.Mode,
                DurationSeconds = (int)Math.Max(0, board.ElapsedSeconds),
                Players = players.Count,
                Events = details?.Events?.Count ?? 0,
                LocalTeam = local >= 0 ? players[local].Team : -1,
                MatchId = item.CloudId ?? 0,
                StartedUnix = board.StartedUtc.ToUnixTimeSeconds(),
            };
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            *match = default;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPlayer(int index, int row, CareerPlayerBlock* player)
    {
        if (player == null)
        {
            return 0;
        }

        try
        {
            *player = default;
            CareerModel model = Model;
            if (index < 0 || index >= model.Items.Count)
            {
                return 0;
            }

            var players = model.Players(model.Items[index]);
            if (row < 0 || row >= players.Count)
            {
                return 0;
            }

            var p = players[row];
            *player = new CareerPlayerBlock
            {
                Team = p.Team, Score = p.Score, Kills = p.Kills, Deaths = p.Deaths,
                Flags = (int)((p.Local ? CareerPlayerFlags.Local : 0) | (p.Bot ? CareerPlayerFlags.Bot : 0) | (p.Left ? CareerPlayerFlags.LeftEarly : 0)),
            };
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            *player = default;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAchievement(int index, CareerAchievementBlock* achievement)
    {
        if (achievement == null)
        {
            return 0;
        }

        try
        {
            *achievement = default;
            CareerModel model = Model;
            if (index < 0 || index >= model.AchievementRows.Count)
            {
                return 0;
            }

            CareerModel.AchievementRow row = model.AchievementRows[index];
            CareerAchievementFlags flags = CareerAchievementFlags.None;
            if (row.Earned) flags |= CareerAchievementFlags.Earned;
            if (row.Reported) flags |= CareerAchievementFlags.Reported;
            if (CareerModel.RewardItems(row).Any()) flags |= CareerAchievementFlags.HasRewards;
            if (model.NextUnlockIndex() == index) flags |= CareerAchievementFlags.NextUnlock;
            *achievement = new CareerAchievementBlock
            {
                Category = (int)row.Achievement.Category,
                Current = row.Current,
                Target = row.Target,
                Flags = (int)flags,
                EarnedUnix = row.EarnedAt?.ToUnixTimeSeconds() ?? 0,
            };
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            *achievement = default;
            return 0;
        }
    }

    private static void Failed(Exception e) => Exports.ReportFailure(e.ToString());
}
