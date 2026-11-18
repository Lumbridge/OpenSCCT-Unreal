using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.Net.Scoreboards;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Hud;
using OpenScct.UnrealBridge.Online;

/// <summary>
/// HudProbe &lt;content&gt; [map] [spy|merc] [seconds] [mode] board: the running round's scoreboard as the HUD reads it
/// (LiveScoreboard): a local match with bots is played for the given time (40 s or more shows the first release of
/// the delayed combat counters) and every few seconds the board's title, clock, timing line and both tables are
/// printed. Fails when no detailed board arrives, when the countdown does not run down between two snapshots, or when
/// the tables do not start with the local player's side.
/// </summary>
internal static class BoardProbe
{
    public static int Run(string content, string map, string mode, bool merc, double seconds)
    {
        Console.WriteLine($"sizes: LiveScoreboardInfo {Unsafe.SizeOf<LiveScoreboardInfo>()}, LiveScoreboardApi {Unsafe.SizeOf<LiveScoreboardApi>()}, LobbyScoreRow {Unsafe.SizeOf<LobbyScoreRow>()}");
        BridgeSession.Configure(content);
        using BridgeSession session = BridgeSession.StartLocal(map, mode, merc, bots: 3);
        HudFeature.Of(session).SetViewport(1920, 1080);
        LiveScoreboard live = LiveScoreboard.Of(session);
        var run = Stopwatch.StartNew();
        double nextReport = 1;
        int failures = 0, revisions = 0, lastRevision = -1, detailed = 0, released = 0;
        bool countdownRan = false, firstSideRight = true;
        string lastTiming = string.Empty;
        RoundScoreboard? lastBoard = null;
        while (run.Elapsed.TotalSeconds < seconds)
        {
            session.SetInput(default);
            session.Update();
            if (live.Board(out int local) is { } board)
            {
                LiveScoreboardInfo info = live.Info(board, local);
                if (info.Revision != lastRevision)
                {
                    lastRevision = info.Revision;
                    revisions++;
                }

                detailed += (info.Flags & LiveScoreboardFlags.Detailed) != 0 ? 1 : 0;
                string timing = live.Text(board, LiveScoreboardText.Timing, 0, 0);
                if (ReferenceEquals(board, lastBoard) && timing != lastTiming && timing.Contains("Next update", StringComparison.Ordinal))
                {
                    countdownRan = true;
                }

                lastBoard = board;
                lastTiming = timing;
                if (board.AsOfSeconds is not null && released++ == 0)
                {
                    Console.WriteLine($"   t={run.Elapsed.TotalSeconds:F1}: first combat sample released, as of {board.AsOfSeconds:F1} s of the round");
                    nextReport = 0;
                }

                IReadOnlyList<ScoreboardPlayer> rows = live.Rows;
                if (rows.Count > 0 && local >= 0 && rows[0].Team != (merc ? 1 : 0))
                {
                    firstSideRight = false;
                }

                if (run.Elapsed.TotalSeconds >= nextReport)
                {
                    nextReport = run.Elapsed.TotalSeconds + 6;
                    Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1}: \"{live.Text(board, LiveScoreboardText.Title, 0, 0)}\"  {live.Text(board, LiveScoreboardText.Clock, 0, 0)}  " +
                        $"flags 0x{info.Flags:x} schema {board.SchemaVersion} local {local} first team {info.FirstTeam} rows {info.RowCount} omitted {info.Omitted} revision {info.Revision}");
                    Console.WriteLine($"   {timing}   (next deadline {board.NextCombatUpdateSeconds?.ToString("F1") ?? "none"}, elapsed {board.ElapsedSeconds:F1}, as of {board.AsOfSeconds?.ToString("F1") ?? "none"})");
                    int team = -1;
                    for (int i = 0; i < rows.Count; i++)
                    {
                        int side = rows[i].Team == 1 ? 1 : 0;
                        int columns = side == 0 ? info.SpyColumns : info.MercColumns;
                        if (side != team)
                        {
                            team = side;
                            Console.WriteLine($"   {(side == 0 ? "SPIES" : "MERCENARIES")}: Name | " +
                                string.Join(" | ", Enumerable.Range(0, columns).Select(c => live.Text(board, LiveScoreboardText.ColumnLabel, side, c))));
                        }

                        Console.WriteLine($"     {(rows[i].ParticipantId == local ? "*" : " ")}{rows[i].Name}{(rows[i].Bot ? " · bot" : "")} | " +
                            string.Join(" | ", Enumerable.Range(0, columns).Select(c => live.Text(board, LiveScoreboardText.Cell, i, c))));
                    }

                    string spectators = live.Text(board, LiveScoreboardText.Spectators, 0, 0);
                    if (spectators.Length > 0)
                    {
                        Console.WriteLine("   " + spectators);
                    }
                }
            }

            Thread.Sleep(4);
        }

        void Check(bool ok, string what)
        {
            Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {what}");
            failures += ok ? 0 : 1;
        }

        Check(revisions > 3, $"the board was refreshed ({revisions} revisions)");
        Check(detailed > 0, "the server's detailed board arrived (schema 1 or later)");
        Check(countdownRan, "the Next update countdown ran down between two snapshots");
        Check(firstSideRight, "the first table is the local player's side");
        if (seconds >= 40)
        {
            Check(released > 0, "a delayed combat sample was released");
        }

        return failures == 0 ? 0 : 1;
    }
}
