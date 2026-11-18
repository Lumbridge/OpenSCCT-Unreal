using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net.Scoreboards;
using OpenScct.Server;
using OpenScct.UnrealBridge.Hud;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge;

/// <summary>The running round's scoreboard as a whole (<see cref="LiveScoreboardApi.GetBoard"/>). The header mirrors it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct LiveScoreboardInfo
{
    /// <summary>The rows GetRows writes: the local player's side first, then the other side (24 at most).</summary>
    public int RowCount;

    /// <summary>The side of the first table (0 spies, 1 mercs): the local player's, the spies' for a spectator.</summary>
    public int FirstTeam;

    /// <summary>Columns of each side's table (RoundScoreboardModel.Columns).</summary>
    public int SpyColumns, MercColumns;

    /// <summary><see cref="LiveScoreboardFlags"/>.</summary>
    public int Flags;

    public int LocalParticipantId;

    /// <summary>Players of the two sides the 24 rows had no room for.</summary>
    public int Omitted;

    public float ElapsedSeconds;

    /// <summary>Changes whenever the rows or cells may have changed (a new snapshot, another local participant).</summary>
    public int Revision;
}

public static class LiveScoreboardFlags
{
    /// <summary>The server sends detailed scoreboards (schema 1 or later); without it the combat columns read "—".</summary>
    public const int Detailed = 1;
    public const int Practice = 2;
    public const int Partial = 4;
    public const int Completed = 8;
}

/// <summary>The texts of the running round's scoreboard (<see cref="LiveScoreboardApi.GetText"/>).</summary>
public enum LiveScoreboardText
{
    /// <summary>"Enhanced Aquarius (AquaE) · Practice".</summary>
    Title = 0,

    /// <summary>The round's time played, "04:12".</summary>
    Clock = 1,

    /// <summary>"Combat stats delayed at least 30s · Next update 12.4s"; it changes every frame.</summary>
    Timing = 2,

    /// <summary>A column's label: a = side (0 spies, 1 mercs), b = column.</summary>
    ColumnLabel = 3,

    /// <summary>A column's explanation: a = side, b = column.</summary>
    ColumnDescription = 4,

    /// <summary>A cell: a = row (GetRows' order), b = column of the row's side.</summary>
    Cell = 5,

    /// <summary>"Spectators: a, b", or empty.</summary>
    Spectators = 6,

    /// <summary>"3 earlier participants are included in final details", or empty.</summary>
    Omitted = 7,
}

/// <summary>
/// The running round's scoreboard: Unreal asks for <see cref="LiveScoreboardExports.GetApi"/> once and keeps the
/// table; Source/OpenSCCT/Runtime/ScctLiveScoreboardApi.h mirrors it field for field. Rows are the lobby table's
/// <see cref="LobbyScoreRow"/>. Every entry answers "no board" while no session runs. Text is UTF-16, zero terminated.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LiveScoreboardApi
{
    public int Version;

    /// <summary>Fills the info (may be null). Returns 1 when there is a board to show, else 0.</summary>
    public delegate* unmanaged[Cdecl]<LiveScoreboardInfo*, int> GetBoard;

    /// <summary>Writes the rows (only capacity of them). Returns their count, -1 without a board.</summary>
    public delegate* unmanaged[Cdecl]<LobbyScoreRow*, int, int> GetRows;

    /// <summary>(kind, a, b, buffer, capacity): a text of the board (<see cref="LiveScoreboardText"/>). Returns its full length.</summary>
    public delegate* unmanaged[Cdecl]<int, int, int, char*, int, int> GetText;
}

/// <summary>
/// The running round's scoreboard for the HUD (a port of the Godot client's ScoreboardOverlay with
/// RoundScoreboardView's live board): the playing client's latest snapshot (ClientSession.Scoreboard), whose public
/// columns the server refreshes each second and whose combat counters it releases on its 30 second delay, the tables
/// in the board's order (the local player's side first, 24 rows at most, players who left last), and the "Next update"
/// countdown counted on from the moment the snapshot arrived. On a server without detailed scoreboards the board is
/// made from the HUD's own player list once a second, with the combat columns unavailable.
/// <para>A per-session feature: it notes when each snapshot arrives, so it must be asked for when the session starts
/// (the HUD does, by asking for the board every frame).</para>
/// </summary>
public sealed class LiveScoreboard : IDisposable
{
    private readonly BridgeSession _session;
    private readonly ClientSession _client;
    private RoundScoreboard? _received;
    private long _receivedAt;
    private RoundScoreboard? _legacy;
    private long _legacyAt;
    private int _legacyLocal = -1;
    private RoundScoreboard? _shown;
    private int _shownLocal = -1;
    private RoundScoreboard? _table;
    private ScoreboardPlayer[] _rows = [];
    private int _omitted;
    private int _revision;
    private bool _disposed;

    private LiveScoreboard(BridgeSession session)
    {
        _session = session;
        _client = session.Session.Client;
        _client.ScoreboardReceived += Receive;
        if (_client.Scoreboard is { } board)
        {
            Receive(board);
        }
    }

    public static LiveScoreboard Of(BridgeSession session) => session.Feature(static s => new LiveScoreboard(s));

    private void Receive(RoundScoreboard board)
    {
        _received = board;
        _receivedAt = Stopwatch.GetTimestamp();
    }

    /// <summary>Seconds since the snapshot shown arrived (RoundScoreboardView.SnapshotAge).</summary>
    public double SnapshotAge => _received is null ? 0 : Stopwatch.GetElapsedTime(_receivedAt).TotalSeconds;

    /// <summary>The board to show now and the local player's participant id in it, or null before anything is known.</summary>
    public RoundScoreboard? Board(out int local)
    {
        RoundScoreboard? board = _client.Scoreboard;
        if (board is not null)
        {
            if (!ReferenceEquals(board, _received))
            {
                Receive(board);
            }

            local = _client.ScoreboardLocalParticipantId;
        }
        else
        {
            if (_legacy is null || Stopwatch.GetElapsedTime(_legacyAt).TotalSeconds >= 1)
            {
                _legacy = LegacyBoard(out _legacyLocal) ?? _legacy;
                _legacyAt = Stopwatch.GetTimestamp();
            }

            board = _legacy;
            local = _legacyLocal;
        }

        if (board is not null && (!ReferenceEquals(board, _shown) || local != _shownLocal))
        {
            _shown = board;
            _shownLocal = local;
            _table = board with { Rows = RoundScoreboardModel.LiveRows(board) };
            int first = RoundScoreboardModel.FirstTeam(board, local);
            _rows = RoundScoreboardModel.Team(_table, first).Concat(RoundScoreboardModel.Team(_table, 1 - first)).ToArray();
            _omitted = board.Rows.Count(static p => p.Team is 0 or 1) - _rows.Length;
            _revision++;
        }

        return board;
    }

    /// <summary>The rows of the board <see cref="Board"/> last returned, in the tables' order.</summary>
    public IReadOnlyList<ScoreboardPlayer> Rows => _rows;

    public LiveScoreboardInfo Info(RoundScoreboard board, int local) => new()
    {
        RowCount = _rows.Length,
        FirstTeam = RoundScoreboardModel.FirstTeam(board, local),
        SpyColumns = RoundScoreboardModel.Columns(0).Count,
        MercColumns = RoundScoreboardModel.Columns(1).Count,
        Flags = (board.SchemaVersion > 0 ? LiveScoreboardFlags.Detailed : 0) | (board.Practice ? LiveScoreboardFlags.Practice : 0) |
            (board.Partial ? LiveScoreboardFlags.Partial : 0) | (board.Completed ? LiveScoreboardFlags.Completed : 0),
        LocalParticipantId = local,
        Omitted = _omitted,
        ElapsedSeconds = (float)board.ElapsedSeconds,
        Revision = _revision,
    };

    public string Text(RoundScoreboard board, LiveScoreboardText kind, int a, int b)
    {
        switch (kind)
        {
            case LiveScoreboardText.Title:
                return MapTitle(board.Map) + (board.Practice ? " · Practice" : string.Empty) + (board.Partial ? " · Partial" : string.Empty);
            case LiveScoreboardText.Clock:
                return RoundScoreboardModel.Clock(board.ElapsedSeconds);
            case LiveScoreboardText.Timing:
                return RoundScoreboardModel.Timing(board, ReferenceEquals(board, _received) ? SnapshotAge : 0);
            case LiveScoreboardText.ColumnLabel:
            case LiveScoreboardText.ColumnDescription:
            {
                IReadOnlyList<RoundScoreboardModel.Column> columns = RoundScoreboardModel.Columns(a == 1 ? 1 : 0);
                if (b < 0 || b >= columns.Count)
                {
                    return string.Empty;
                }

                return kind == LiveScoreboardText.ColumnLabel ? columns[b].Label : columns[b].Description;
            }
            case LiveScoreboardText.Cell:
            {
                if (a < 0 || a >= _rows.Length || _table is null)
                {
                    return string.Empty;
                }

                IReadOnlyList<RoundScoreboardModel.Column> columns = RoundScoreboardModel.Columns(_rows[a].Team == 1 ? 1 : 0);
                return b >= 0 && b < columns.Count ? columns[b].Read(_table, _rows[a]) : string.Empty;
            }
            case LiveScoreboardText.Spectators:
            {
                string[] watching = board.Rows.Where(static p => p.Team is not (0 or 1)).Select(static p => p.Name).ToArray();
                return watching.Length == 0 ? string.Empty : "Spectators: " + string.Join(", ", watching);
            }
            case LiveScoreboardText.Omitted:
                return _omitted > 0 ? $"{_omitted} earlier participants are included in final details" : string.Empty;
            default:
                return string.Empty;
        }
    }

    /// <summary>The map's name with its code after it when they differ (MapTitles.WithCode); "Scoreboard" without a map.</summary>
    private static string MapTitle(string map)
    {
        if (string.IsNullOrWhiteSpace(map))
        {
            return "Scoreboard";
        }

        string name = map;
        try
        {
            name = LobbyExtras.MapTitle(OnlineBridge.Instance.Catalog.Maps, map);
        }
        catch (Exception)
        {
            // Without a catalog the code alone names the map.
        }

        return string.Equals(name, map, StringComparison.OrdinalIgnoreCase) ? name : $"{name} ({map})";
    }

    /// <summary>
    /// ScoreboardOverlay.LegacyBoard: the HUD's player list as a schema 0 board, for a server that sends no scoreboards.
    /// </summary>
    private RoundScoreboard? LegacyBoard(out int local)
    {
        local = -1;
        HudFeature hud = HudFeature.Of(_session);
        HudFrame frame = hud.Frame;
        ReadOnlySpan<HudScoreRow> score = frame.Score.Span;
        if (score.Length == 0)
        {
            return null;
        }

        bool deathMatch = frame.Round.GameMode is 6 or 7;
        var rows = new ScoreboardPlayer[score.Length];
        for (int i = 0; i < score.Length; i++)
        {
            HudScoreRow row = score[i];
            if ((row.Flags & HudScoreFlags.Local) != 0)
            {
                local = i;
            }

            rows[i] = new ScoreboardPlayer
            {
                ParticipantId = i,
                Name = hud.Texts.Text(row.NameId) ?? string.Empty,
                Team = (row.Flags & HudScoreFlags.Spectator) != 0 ? 255 : row.Team,
                Kills = row.Kills,
                Deaths = row.Deaths,
                Objectives = deathMatch ? 0 : row.ObjOrKills,
                Lives = row.Lives,
                Rank = row.Rank,
                PingMs = row.Ping,
                StatsVersion = 0,
            };
        }

        return new RoundScoreboard
        {
            SchemaVersion = 0,
            RoundId = "legacy",
            Map = _session.Map,
            ElapsedSeconds = Math.Max(0, frame.Round.ElapsedTime),
            Rows = rows,
        };
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _client.ScoreboardReceived -= Receive;
    }
}

public static unsafe class LiveScoreboardExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(LiveScoreboardApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetBoard = &GetBoard;
        api->GetRows = &GetRows;
        api->GetText = &GetText;
        return ApiVersion;
    }

    private static LiveScoreboard? Live => Exports.Current is { IsDisposed: false, IsBackdrop: false } session ? LiveScoreboard.Of(session) : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetBoard(LiveScoreboardInfo* info)
    {
        try
        {
            if (Live is not { } live || live.Board(out int local) is not { } board)
            {
                if (info != null)
                {
                    *info = default;
                }

                return 0;
            }

            if (info != null)
            {
                *info = live.Info(board, local);
            }

            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRows(LobbyScoreRow* rows, int capacity)
    {
        try
        {
            if (Live is not { } live || live.Board(out int local) is null)
            {
                return -1;
            }

            IReadOnlyList<ScoreboardPlayer> list = live.Rows;
            for (int i = 0; i < list.Count && rows != null && i < capacity; i++)
            {
                rows[i] = default;
                LobbyExtras.FillRow(ref rows[i], list[i], local);
            }

            return list.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, int a, int b, char* buffer, int capacity)
    {
        try
        {
            string text = Live is { } live && live.Board(out _) is { } board ? live.Text(board, (LiveScoreboardText)kind, a, b) : string.Empty;
            return OnlineTextCopy.Buffer(buffer, capacity, text);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }
}
