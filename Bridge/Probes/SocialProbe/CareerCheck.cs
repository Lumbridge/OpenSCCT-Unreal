using System;
using System.IO;
using System.Linq;
using OpenScct.Net.Scoreboards;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Social;

/// <summary>
/// SocialProbe career [folder]: the career table's record sizes (Source/OpenSCCT/Runtime/ScctCareerApi.h asserts
/// them), then the scoreboard archive in a scratch folder: a finished round and a round left early are saved, read
/// back and listed by the history model, newest first, with their outcome lines and players.
/// </summary>
internal static class CareerCheck
{
    public static int Run(string[] args)
    {
        int failures = 0;
        void Check(bool ok, string what)
        {
            Console.WriteLine((ok ? "ok: " : "FAILED: ") + what);
            failures += ok ? 0 : 1;
        }

        unsafe
        {
            Console.WriteLine($"record sizes: CareerInfoBlock {sizeof(CareerInfoBlock)}, CareerMatchBlock {sizeof(CareerMatchBlock)}, " +
                $"CareerPlayerBlock {sizeof(CareerPlayerBlock)}, CareerAchievementBlock {sizeof(CareerAchievementBlock)}, CareerApi {sizeof(CareerApi)}");
            Check(sizeof(CareerInfoBlock) == 48 && sizeof(CareerMatchBlock) == 48 && sizeof(CareerPlayerBlock) == 24 && sizeof(CareerAchievementBlock) == 24,
                "the record sizes match the C++ header");
        }

        string folder = args.Length > 0 ? args[0] : Path.Combine(Path.GetTempPath(), "scct-career-" + Guid.NewGuid().ToString("N"));
        ScoreboardHistory.Configure(folder);
        DateTimeOffset start = DateTimeOffset.UtcNow.AddMinutes(-20);
        RoundScoreboard Board(string map, DateTimeOffset started, bool ended) => new()
        {
            RoundId = Guid.NewGuid().ToString(), Map = map, Server = string.Empty, Mode = 5, StartedUtc = started,
            EndedUtc = ended ? started.AddMinutes(6) : null, ElapsedSeconds = 360, Completed = ended, Winner = ended ? 1 : 255, Reason = ended ? 2 : 255,
            Rows =
            [
                new ScoreboardPlayer { ParticipantId = 1, Name = "Agent", Team = 0, Score = 120, Kills = 1, Deaths = 2 },
                new ScoreboardPlayer { ParticipantId = 2, Name = "Bot Merc", Team = 1, Score = 300, Kills = 3, Deaths = 0, Bot = true },
            ],
        };

        ScoreboardHistory.Save(new ArchivedScoreboard { Scoreboard = Board("AquaE", start, true), LocalParticipantId = 1, PlayedLocally = true });
        RoundScoreboard left = Board("ClubE", start.AddMinutes(10), false);
        ScoreboardHistory.Save(new ArchivedScoreboard
        {
            Scoreboard = left with { Partial = true, EndedUtc = DateTimeOffset.UtcNow, Rows = left.Rows.Select(r => r.ParticipantId == 1 ? r with { LeftEarly = true } : r).ToArray() },
            LocalParticipantId = 1, PlayedLocally = true, LeftEarly = true,
        });
        Check(Directory.GetFiles(folder, "*.json").Length == 2, "two scoreboard files in " + folder);

        CareerModel model = CareerModel.Instance;
        model.RefreshHistory();
        model.Pump();
        foreach (CareerModel.Item item in model.Items)
        {
            Console.WriteLine($"  {item.Record.Scoreboard.Map}: {CareerModel.Outcome(item)} · {CareerModel.Metadata(item)} · {CareerModel.Server(item)} · " +
                string.Join(", ", model.Players(item).Select(p => $"{p.Name}{(p.Local ? " (you)" : string.Empty)} {p.Score}")));
        }

        Check(model.Items.Count == 2, "the history lists both rounds");
        Check(model.Items.Count == 2 && model.Items[0].Record.Scoreboard.Map == "ClubE" && CareerModel.Outcome(model.Items[0]).Contains("Left early"),
            "newest first, the round left early marked");
        Check(model.Items.Count == 2 && CareerModel.Outcome(model.Items[1]) == "Mercenaries win", "the finished round's outcome");
        Console.WriteLine("status: " + model.Status);
        Console.WriteLine($"achievements: {model.AchievementRows.Count} ({model.AchievementsSummary})");
        Check(model.AchievementRows.Count > 0, "the achievement catalogue is listed signed out");
        Console.WriteLine("stats: " + model.StatsStatus);
        if (args.Length == 0)
        {
            Directory.Delete(folder, recursive: true);
        }

        Console.WriteLine(failures == 0 ? "career: all checks passed" : $"career: {failures} check(s) failed");
        return failures == 0 ? 0 : 1;
    }
}
