using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Net.Cosmetics;
using OpenScct.Net.Scoreboards;
using OpenScct.Server;
using OpenScct.Server.Online;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge.Online;

/// <summary>The last round's scoreboard as a whole (LobbyApi.GetLastScoreboard).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct LobbyScoreboardInfo
{
    /// <summary>The rows GetLastScoreboardRows writes: the local player's side first, then the other side.</summary>
    public int RowCount;

    /// <summary>Columns of each side's table (RoundScoreboardModel.Columns).</summary>
    public int SpyColumns, MercColumns;

    /// <summary>0 spies, 1 mercs, 255 nobody.</summary>
    public int Winner;

    public int Reason;

    /// <summary>EGameMode.</summary>
    public int Mode;

    /// <summary><see cref="LobbyScoreboardFlags"/>.</summary>
    public int Flags;

    public int LocalParticipantId;
    public int ObjectivesTaken, ObjectivesNeeded;
    public float ElapsedSeconds;

    /// <summary>Changes with every new scoreboard (its sequence, folded into an int).</summary>
    public int Sequence;
}

public static class LobbyScoreboardFlags
{
    public const int Completed = 1;
    public const int Practice = 2;
    public const int Partial = 4;
}

/// <summary>One line of the last round's scoreboard.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LobbyScoreRow
{
    public int ParticipantId;
    public int Team;
    public int Score;
    public int Kills, Deaths;

    /// <summary>Lives left, -1 unlimited, below -1 unknown.</summary>
    public int Lives;

    /// <summary>Objectives taken, -1 unknown.</summary>
    public int Objectives;

    /// <summary>The packed rank, 0 for none.</summary>
    public int Rank;

    public int PingMs;

    /// <summary><see cref="LobbyScoreRowFlags"/>.</summary>
    public int Flags;

    public fixed char Name[OnlinePlayer.NameCapacity];
}

public static class LobbyScoreRowFlags
{
    public const int Bot = 1;
    public const int LeftEarly = 2;
    public const int Local = 4;
}

/// <summary>The texts of the last round's scoreboard (LobbyApi.GetLastScoreboardText).</summary>
public enum LobbyScoreboardText
{
    /// <summary>"Enhanced Aquarius · Mercenaries win · Practice".</summary>
    Title = 0,

    /// <summary>"17 Nov 2026 21:04 · Server · 01:00".</summary>
    Metadata = 1,

    /// <summary>"Final totals", "Partial record · ..." (RoundScoreboardModel.Timing).</summary>
    Timing = 2,

    /// <summary>A column's label: a = side (0 spies, 1 mercs), b = column.</summary>
    ColumnLabel = 3,

    /// <summary>A column's explanation: a = side, b = column.</summary>
    ColumnDescription = 4,

    /// <summary>A cell: a = row (GetLastScoreboardRows' order), b = column of the row's side.</summary>
    Cell = 5,

    /// <summary>A row's breakdown, one "Group\tLabel\tValue" per line: a = row.</summary>
    Details = 6,

    /// <summary>The lobby's line: "Last game · Enhanced Aquarius\nMercenaries win".</summary>
    Summary = 7,

    /// <summary>The spectators of the round, "Spectators: a, b", or empty.</summary>
    Spectators = 8,
}

/// <summary>The texts of the lobby's Briefing (LobbyApi.GetBriefingText).</summary>
public enum LobbyBriefingText
{
    /// <summary>"Enhanced Aquarius - Story" (the panel's heading).</summary>
    Title = 0,

    /// <summary>"MISSION MAIN OBJECTIVE".</summary>
    PageTitle = 1,

    Objective = 2,
    ObjectiveName = 3,
    MapBrief = 4,
    ModeBrief = 5,
    Legend = 6,

    /// <summary>The level picture (page 1), a full PNG path, or empty.</summary>
    Picture = 7,

    /// <summary>The briefing plan (page 2), else the mercs' loading picture, a full PNG path, or empty.</summary>
    Plan = 8,
}

/// <summary>A place number on the briefing plan, at U across and V down (0..1 over the picture).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct LobbyBriefingMark
{
    public float U, V;
    public int Number;
}

/// <summary>
/// The lobby's extras for every player (the Godot client's LobbyRoomScreen.Scoreboard, RoundScoreboardDialog with
/// RoundScoreboardModel, BriefingPanel with BriefingPages, and RankIcons' colours and words), worked out for
/// LobbyExports. The scoreboard is OnlineClient.LastScoreboard, the server's final snapshot of the round just played,
/// with the participant id the local player had in it. The briefing is the setup's map and mode, for the local
/// player's side (a spectator reads the mercs' words, as the original's pages do).
/// </summary>
public static class LobbyExtras
{
    private static RoundScoreboard? s_board;
    private static ScoreboardPlayer[] s_rows = [];

    /// <summary>The scoreboard's rows in the dialog's order: the local player's side first, then the other.</summary>
    public static ScoreboardPlayer[] Rows(RoundScoreboard board, int local)
    {
        if (!ReferenceEquals(board, s_board))
        {
            int first = RoundScoreboardModel.FirstTeam(board, local);
            s_rows = RoundScoreboardModel.Team(board, first).Concat(RoundScoreboardModel.Team(board, 1 - first)).ToArray();
            s_board = board;
        }

        return s_rows;
    }

    public static string Outcome(RoundScoreboard board) =>
        board.Partial ? "Incomplete" : board.Winner == 0 ? "Spies win" : board.Winner == 1 ? "Mercenaries win" : "Round stopped";

    public static string MapTitle(MapCatalog? maps, string map)
    {
        if (string.IsNullOrWhiteSpace(map))
        {
            return "Scoreboard";
        }

        return maps?.GetCatalogEntry(map) is { DisplayName: { Length: > 0 } name } ? name : map;
    }

    public static LobbyScoreboardInfo Info(RoundScoreboard board, int local) => new()
    {
        RowCount = Rows(board, local).Length,
        SpyColumns = RoundScoreboardModel.Columns(0).Count,
        MercColumns = RoundScoreboardModel.Columns(1).Count,
        Winner = board.Winner,
        Reason = board.Reason,
        Mode = board.Mode,
        Flags = (board.Completed ? LobbyScoreboardFlags.Completed : 0) | (board.Practice ? LobbyScoreboardFlags.Practice : 0) |
            (board.Partial ? LobbyScoreboardFlags.Partial : 0),
        LocalParticipantId = local,
        ObjectivesTaken = board.ObjectivesTaken,
        ObjectivesNeeded = board.ObjectivesNeeded,
        ElapsedSeconds = (float)board.ElapsedSeconds,
        Sequence = unchecked((int)board.Sequence ^ (int)(board.Sequence >> 32) ^ board.RoundId.GetHashCode(StringComparison.Ordinal)),
    };

    public static unsafe void FillRow(ref LobbyScoreRow row, ScoreboardPlayer p, int local)
    {
        row.ParticipantId = p.ParticipantId;
        row.Team = p.Team;
        row.Score = p.Score;
        row.Kills = p.Kills;
        row.Deaths = p.Deaths;
        row.Lives = p.Lives;
        row.Objectives = p.Objectives;
        row.Rank = p.Rank;
        row.PingMs = p.PingMs;
        row.Flags = (p.Bot ? LobbyScoreRowFlags.Bot : 0) | (p.LeftEarly ? LobbyScoreRowFlags.LeftEarly : 0) |
            (p.ParticipantId == local && local >= 0 ? LobbyScoreRowFlags.Local : 0);
        fixed (char* name = row.Name)
        {
            OnlineTextCopy.Fixed(name, OnlinePlayer.NameCapacity, p.Name);
        }
    }

    public static string ScoreboardText(RoundScoreboard board, int local, MapCatalog? maps, LobbyScoreboardText kind, int a, int b)
    {
        ScoreboardPlayer[] rows = Rows(board, local);
        switch (kind)
        {
            case LobbyScoreboardText.Title:
                return MapTitle(maps, board.Map) + " · " + Outcome(board) + (board.Practice ? " · Practice" : string.Empty);
            case LobbyScoreboardText.Metadata:
                return string.Create(CultureInfo.InvariantCulture, $"{board.StartedUtc.ToLocalTime():dd MMM yyyy HH:mm}") + " · " + board.Server + " · " +
                    RoundScoreboardModel.Clock(board.ElapsedSeconds);
            case LobbyScoreboardText.Timing:
                return RoundScoreboardModel.Timing(board);
            case LobbyScoreboardText.ColumnLabel:
            case LobbyScoreboardText.ColumnDescription:
            {
                IReadOnlyList<RoundScoreboardModel.Column> columns = RoundScoreboardModel.Columns(a == 1 ? 1 : 0);
                if (b < 0 || b >= columns.Count)
                {
                    return string.Empty;
                }

                return kind == LobbyScoreboardText.ColumnLabel ? columns[b].Label : columns[b].Description;
            }
            case LobbyScoreboardText.Cell:
            {
                if (a < 0 || a >= rows.Length)
                {
                    return string.Empty;
                }

                IReadOnlyList<RoundScoreboardModel.Column> columns = RoundScoreboardModel.Columns(rows[a].Team == 1 ? 1 : 0);
                return b >= 0 && b < columns.Count ? columns[b].Read(board, rows[a]) : string.Empty;
            }
            case LobbyScoreboardText.Details:
                return a < 0 || a >= rows.Length
                    ? string.Empty
                    : string.Join('\n', RoundScoreboardModel.Details(board, rows[a]).Select(static d => d.Group + "\t" + d.Label + "\t" + d.Value));
            case LobbyScoreboardText.Summary:
                return "Last game · " + MapTitle(maps, board.Map) + "\n" + Outcome(board) + (board.Practice ? " · Practice" : string.Empty);
            case LobbyScoreboardText.Spectators:
            {
                string[] watching = board.Rows.Where(static p => p.Team is not (0 or 1)).Select(static p => p.Name).ToArray();
                return watching.Length == 0 ? string.Empty : "Spectators: " + string.Join(", ", watching);
            }
            default:
                return string.Empty;
        }
    }

    // ---- the briefing ------------------------------------------------------------------------------------------

    /// <summary>The local player reads the spies' words only on the spies' side (a spectator reads the mercs').</summary>
    public static bool LocalIsSpy(OnlineClient client) => client.Lobby.Local?.Team == 0;

    public static string BriefingText(OnlineClient client, MapCatalog maps, LobbyBriefingText kind, int side)
    {
        string map = client.Lobby.Setup.Map;
        EGameMode mode = client.Lobby.Setup.Mode;
        bool spy = side < 0 ? LocalIsSpy(client) : side == 0;
        MapCatalogEntry? entry = string.IsNullOrEmpty(map) ? null : maps.GetCatalogEntry(map);
        switch (kind)
        {
            case LobbyBriefingText.Title:
                return MapTitle(maps, map) + " - " + CreateSessionModel.ModeName(mode);
            case LobbyBriefingText.Picture:
                return entry?.PreviewImage ?? string.Empty;
            case LobbyBriefingText.Plan:
                return entry?.BriefingImage ?? RoundExports.LoadingPicture(maps, map, merc: true, seed: null) ?? string.Empty;
        }

        BriefingPages pages = BriefingPages.For(string.IsNullOrEmpty(map) ? null : maps.GetMapSettings(map), mode, spy);
        return kind switch
        {
            LobbyBriefingText.PageTitle => pages.Title,
            LobbyBriefingText.Objective => pages.Objective,
            LobbyBriefingText.ObjectiveName => pages.ObjectiveName,
            LobbyBriefingText.MapBrief => pages.MapBrief,
            LobbyBriefingText.ModeBrief => pages.ModeBrief,
            LobbyBriefingText.Legend => pages.Legend,
            _ => string.Empty,
        };
    }

    /// <summary>The room a place number takes on the plan, as a share of its width and height (LobbyRoomScreen).</summary>
    private const float MarkWidth = 0.06f, MarkHeight = 0.045f;

    /// <summary>
    /// The map's numbered places on its briefing plan when the plan was drawn through the snapshot camera
    /// (LobbyRoomScreen.PlaceMarks): projected with the strategic map's camera; numbers that land on one spot stack down.
    /// </summary>
    public static List<LobbyBriefingMark> Marks(MapCatalog maps, string? map)
    {
        var marks = new List<LobbyBriefingMark>();
        if (string.IsNullOrEmpty(map) || maps.GetCatalogEntry(map) is not { BriefingThroughSnapshot: true, Folder: { } folder } entry || entry.Places.Count == 0)
        {
            return marks;
        }

        StrategicMapData strategic = StrategicMapData.Load(folder);
        foreach (OpenScct.Content.Maps.MapPlace place in entry.Places)
        {
            if (strategic.Project(new StrategicMapPoint(place.At.X, place.At.Y, place.At.Z)) is { } uv && uv.U is >= 0f and <= 1f && uv.V is >= 0f and <= 1f)
            {
                float v = uv.V;
                while (v + MarkHeight <= 1f && marks.Any(m => Math.Abs(m.U - uv.U) < MarkWidth && Math.Abs(m.V - v) < MarkHeight))
                {
                    v += MarkHeight;
                }

                marks.Add(new LobbyBriefingMark { U = uv.U, V = v, Number = place.Number });
            }
        }

        return marks;
    }

    // ---- ranks -------------------------------------------------------------------------------------------------

    /// <summary>The rank's emblem colour, 0xRRGGBB (RankIcons.ColourOf).</summary>
    public static int RankColour(int packed) => Social.AccountService.ColourOf(PlayerRank.Unpack(packed));

    /// <summary>"Prestige 3, level 42 - Field Agent" (RankIcons.Describe), empty for none.</summary>
    public static string RankDescribe(int packed) => Social.AccountService.Describe(PlayerRank.Unpack(packed));

    /// <summary>"42", "P3 · 42" or "Master · 55" without the title (RankIcons.Text, the lobby rows' badge).</summary>
    public static string RankTag(int packed) => Social.AccountService.TagOf(PlayerRank.Unpack(packed));
}
