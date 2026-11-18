using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenScct.Net.Scoreboards;
using OpenScct.Server;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// Saves every round the local player played to the scoreboard archive (a port of the Godot client's
/// game/Scripts/Play/ScoreboardCapture.cs): each snapshot the playing client receives is kept as the round's latest;
/// the final one is saved when the round ends, and a round the player left before it ended (the session ended, or the
/// next round's board arrived first) is saved marked partial and left early. Pure spectators save nothing. Only the
/// boards this client received are saved, for a local match and an online round alike (a listen host included).
/// <para>A per-session feature (<see cref="BridgeSession.Feature{T}"/>), made for each session that takes the main slot
/// once Unreal has named the archive's folder (<see cref="ScoreboardHistory.Configure"/>); it takes its hook off the
/// client when the session ends, so it never touches the HUD feature's hooks.</para>
/// </summary>
public sealed class ScoreboardCapture : IDisposable
{
    private readonly ClientSession _client;
    private readonly string _master;
    private readonly long _account;
    private RoundScoreboard? _last;
    private int _lastLocal = -1;
    private bool _disposed;

    private ScoreboardCapture(BridgeSession session)
    {
        _client = session.Session.Client;
        _master = AccountService.MasterUrl ?? string.Empty;
        _account = SocialBridge.Instance.Account.SignedInAccountId;
        _client.ScoreboardReceived += Receive;
        if (_client.Scoreboard is { } board)
        {
            Receive(board);
        }
    }

    public static ScoreboardCapture Of(BridgeSession session) => session.Feature(static s => new ScoreboardCapture(s));

    private void Receive(RoundScoreboard board)
    {
        if (_last is { EndedUtc: null } previous && previous.RoundId != board.RoundId)
        {
            Save(previous, _lastLocal, left: true);
        }

        _last = board;
        _lastLocal = _client.ScoreboardLocalParticipantId;
        if (board.EndedUtc is not null)
        {
            Save(board, _lastLocal, left: false);
        }
    }

    private void Save(RoundScoreboard board, int local, bool left)
    {
        if (board.RoundId.Length == 0 || board.Rows.Count == 0)
        {
            return;
        }

        if (!board.Rows.Any(r => r.ParticipantId == local))
        {
            return; // Pure spectators have not played this round.
        }

        if (left)
        {
            board = board with
            {
                Partial = true,
                EndedUtc = DateTimeOffset.UtcNow,
                Rows = board.Rows.Select(r => r.ParticipantId == local ? r with { LeftEarly = true } : r).ToArray(),
            };
        }

        bool sameMaster = board.Master is { } master && _master.Length > 0 &&
            ScoreboardArchive.NormaliseMaster(master) == ScoreboardArchive.NormaliseMaster(_master);
        ScoreboardHistory.Save(new ArchivedScoreboard
        {
            Scoreboard = board,
            LocalParticipantId = local,
            Master = board.Master ?? string.Empty,
            AccountId = sameMaster ? _account : 0,
            PlayedLocally = true,
            LeftEarly = left,
        });
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _client.ScoreboardReceived -= Receive;
        if (_last is { EndedUtc: null } board)
        {
            Save(board, _lastLocal, left: true);
        }
    }
}

/// <summary>
/// The scoreboard archive of this PC (the Godot client's ScoreboardHistory): its folder (Unreal's
/// Saved/Scoreboards), saving with error reporting, and reading what the current master and account may see.
/// </summary>
public static class ScoreboardHistory
{
    private static ScoreboardArchive? s_store;
    private static bool s_hooked;

    /// <summary>The archive (null until Unreal named its folder).</summary>
    public static ScoreboardArchive? Store => s_store;

    public static string? LastError { get; private set; }

    /// <summary>Scoreboards saved this run.</summary>
    public static int Saved { get; private set; }

    /// <summary>Goes up whenever something was saved (the history page reads the archive again).</summary>
    public static int Revision { get; private set; }

    /// <summary>
    /// Names the archive's folder and starts capturing: every session that takes the main slot from now on gets a
    /// <see cref="ScoreboardCapture"/>, and so does one already running.
    /// </summary>
    public static void Configure(string folder)
    {
        s_store = new ScoreboardArchive(folder);
        if (!s_hooked)
        {
            s_hooked = true;
            Exports.SessionStarted += session => ScoreboardCapture.Of(session);
            if (Exports.Current is { IsDisposed: false } running)
            {
                ScoreboardCapture.Of(running);
            }
        }

        Log("Scoreboard history: rounds played are saved to " + s_store.DirectoryPath);
    }

    public static ArchivedScoreboard Save(ArchivedScoreboard entry)
    {
        if (s_store is not { } store)
        {
            return entry;
        }

        try
        {
            ArchivedScoreboard saved = store.Save(entry);
            LastError = null;
            Saved++;
            Revision++;
            RoundScoreboard board = saved.Scoreboard;
            string outcome = board.Partial ? "left early" : board.Winner == 0 ? "spies win" : board.Winner == 1 ? "mercenaries win" : "no winner";
            Log($"Scoreboard saved: {board.Map}, {outcome}{(board.Practice ? ", practice" : string.Empty)}, {board.Rows.Count} players, " +
                $"round {board.RoundId} ({CountFiles(store)} in {store.DirectoryPath})");
            return saved;
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException)
        {
            LastError = "Could not save the scoreboard: " + e.Message;
            Log(LastError);
            return entry;
        }
    }

    /// <summary>The archived rounds the current master and account may see, newest first.</summary>
    public static IReadOnlyList<ArchivedScoreboard> Read(string master, long account)
    {
        if (s_store is not { } store)
        {
            return [];
        }

        try
        {
            return store.ReadAll(error => Log("Scoreboard history: skipped " + error))
                .Where(e => ScoreboardArchive.VisibleTo(e, master, account)).ToArray();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            LastError = "Could not read the scoreboard history: " + e.Message;
            return [];
        }
    }

    private static int CountFiles(ScoreboardArchive store)
    {
        try
        {
            return Directory.Exists(store.DirectoryPath) ? Directory.EnumerateFiles(store.DirectoryPath, "*.json").Count() : 0;
        }
        catch (IOException)
        {
            return 0;
        }
    }

    private static void Log(string line)
    {
        if (SocialBridge.Existing is { } social)
        {
            social.Log(line);
        }
        else
        {
            Console.WriteLine(line);
        }
    }
}
