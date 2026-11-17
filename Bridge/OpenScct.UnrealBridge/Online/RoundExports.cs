using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core.Engine;
using OpenScct.Net.Scoreboards;
using OpenScct.Server;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// What a round of an online visit shows around its play (Source/OpenSCCT/Runtime/ScctRoundApi.h mirrors it): the
/// waiting list while the round loads (the Godot client's RoundStartList), the loading picture the server's seed picks
/// (MapImages.Loading), and what the results screen adds to the online table's results (ResultsScreen): each line's
/// objectives and early leave from the round's scoreboard, and the personal demo's state. A table of its own, so the
/// online table keeps its layout. Every entry catches its exceptions. Strings are UTF-16, zero terminated. New entries
/// are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct RoundApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<RoundWaitLine*, int, int> GetWaitList;
    public delegate* unmanaged[Cdecl]<RoundResultExtra*, int, int> GetResultExtras;
    public delegate* unmanaged[Cdecl]<int> GetDemoFlags;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<int> EndLocalRound;
}

/// <summary>RoundWaitLine::State, as RoundStartState.</summary>
public enum RoundWaitState
{
    /// <summary>In the session, not on the round's map here yet (no PRI): "Loading...".</summary>
    Joining = 0,

    /// <summary>On the map, still loading (his PRI has not reported waiting): "is connecting".</summary>
    Loading = 1,

    /// <summary>Loaded and waiting for the round (PRI.bWaitingPlayer): "is ready".</summary>
    Ready = 2,

    /// <summary>Was in the session when the round began loading and was dropped: "Player timed out...".</summary>
    TimedOut = 3,
}

/// <summary>One line of the round's waiting list.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct RoundWaitLine
{
    public int State;
    public int Team;
    public fixed char Name[OnlinePlayer.NameCapacity];
}

/// <summary>What the results screen adds to a line of the online table's results, in the same order.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct RoundResultExtra
{
    public int Id;

    /// <summary>Objectives the player took, from the round's scoreboard; -1 when the scoreboard does not have him.</summary>
    public int Objectives;

    public int LeftEarly;

    /// <summary>His packed rank (the lobby's), 0 for none.</summary>
    public int Rank;
}

public static class RoundDemoFlags
{
    /// <summary>The player records his matches (the server sends him his demo).</summary>
    public const int Recording = 1;

    /// <summary>The round's demo is still recording or downloading: leaving now loses it.</summary>
    public const int PendingSave = 2;

    /// <summary>A demo of this visit was saved (RoundText.LastDemoPath).</summary>
    public const int Saved = 4;
}

public enum RoundText
{
    /// <summary>The personal demo's state as the results screen words it.</summary>
    DemoStatus = 0,

    /// <summary>The file of the last saved demo, or empty.</summary>
    LastDemoPath = 1,

    /// <summary>The full path of the loading picture of the round's map for the player's side, picked by the server's seed; empty when none.</summary>
    LoadingPicture = 2,
}

public static unsafe class RoundExports
{
    public const int ApiVersion = 1;

    private static OnlineBridge Online => OnlineBridge.Instance;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(RoundApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetWaitList = &GetWaitList;
        api->GetResultExtras = &GetResultExtras;
        api->GetDemoFlags = &GetDemoFlags;
        api->GetText = &GetText;
        api->EndLocalRound = &EndLocalRound;
        return ApiVersion;
    }

    /// <summary>
    /// While a network round waits for its players to load: writes the waiting list and returns its line count (only
    /// <paramref name="capacity"/> are written). Returns -1 when no round waits (not loading, or the match has begun).
    /// Call it every frame while the visit is Loading: a player who leaves the session while the round loads is kept
    /// as timed out.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetWaitList(RoundWaitLine* lines, int capacity)
    {
        try
        {
            List<(string Name, int Team, RoundWaitState State)>? list = WaitList.Read(Online.Client);
            if (list is null)
            {
                return -1;
            }

            for (int i = 0; i < list.Count && lines != null && i < capacity; i++)
            {
                lines[i].State = (int)list[i].State;
                lines[i].Team = list[i].Team;
                OnlineTextCopy.Fixed(lines[i].Name, OnlinePlayer.NameCapacity, list[i].Name);
            }

            return list.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>
    /// Writes what the results screen adds to each line of the last results (the order of the online table's
    /// GetResults). Returns the number of lines, or -1 when there are no results.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetResultExtras(RoundResultExtra* rows, int capacity)
    {
        try
        {
            if (Online.Client is not { Results: { } results } client)
            {
                return -1;
            }

            Span<RoundResultExtra> target = rows == null || capacity <= 0 ? default : new Span<RoundResultExtra>(rows, capacity);
            return Extras(results, client.Lobby, target);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>The personal demo's state (RoundDemoFlags); 0 when not online.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetDemoFlags()
    {
        try
        {
            return Online.Client is { } client ? DemoFlags(client) : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Copies a text (RoundText) into <paramref name="buffer"/>. Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, TextOf(Online, (RoundText)kind));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>
    /// Unattended checks only (the game's -ScctEndMatch): runs the clock of the local match out on its server, so the
    /// round ends by time the way a timed round does (SGameInfo's own time check decides the winner and ends it). A
    /// match without a time limit gets one minute first. It waits for the match to have begun (its start resets the
    /// clock). Returns 1 when queued on the server's ticks; 0 on a remote round or without a match.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int EndLocalRound()
    {
        try
        {
            if (Exports.Current is not { IsRemote: false, IsDisposed: false } session || session.Session.Host is not { } host)
            {
                return 0;
            }

            static void Run(ServerHost running)
            {
                // The match's start resets the clock (GameReplicationInfo.Reset), so it waits for the match to begin.
                // A local match's host may spawn its game through a factory that ServerHost.Game does not keep.
                GameInfo? game = running.Game ?? running.World.Level.Info.Game;
                if (game?.GameReplicationInfo is not { bMatchHasBegun: true } gri)
                {
                    return;
                }

                running.BeforeWorldTick -= Run;
                {
                    if (gri.TimeLimit <= 0)
                    {
                        gri.TimeLimit = 1;
                    }

                    gri.ElapsedTime = Math.Max(gri.ElapsedTime, gri.TimeLimit * 60);
                    gri.RemainingTime = 0;
                }
            }

            host.BeforeWorldTick += Run;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    // ---- the managed side (the probe calls it too) ---------------------------------------------------------------

    public static int DemoFlags(OnlineClient client) =>
        (client.RecordDemos ? RoundDemoFlags.Recording : 0) | (client.PendingDemoSave ? RoundDemoFlags.PendingSave : 0) |
        (client.LastDemoPath is not null ? RoundDemoFlags.Saved : 0);

    public static string TextOf(OnlineBridge online, RoundText kind)
    {
        OnlineClient? client = online.Client;
        return kind switch
        {
            RoundText.DemoStatus => client?.DemoRecordingStatus ?? string.Empty,
            RoundText.LastDemoPath => client?.LastDemoPath ?? string.Empty,
            RoundText.LoadingPicture => client is null ? string.Empty : LoadingPicture(online.Catalog.Maps, online.MapToLoad, client.Lobby.Local?.Team == 1,
                client.LoadingPictureSeed) ?? string.Empty,
            _ => string.Empty,
        };
    }

    /// <summary>
    /// The Godot client's MapImages.Loading as a path: the map's loading picture for the side (the other side's, then
    /// the map list picture, when it has none); of several, the one the seed picks, so every player of the session
    /// sees the same. Without a seed the first.
    /// </summary>
    public static string? LoadingPicture(MapCatalog maps, string? map, bool merc, int? seed)
    {
        if (string.IsNullOrEmpty(map) || maps.GetCatalogEntry(map) is not { } entry)
        {
            return null;
        }

        static string? Pick(IReadOnlyList<string> pictures, int? seed) => pictures.Count switch
        {
            0 => null,
            1 => pictures[0],
            _ => pictures[seed is { } s ? (int)((uint)s % (uint)pictures.Count) : 0],
        };

        return Pick(merc ? entry.LoadingMercImages : entry.LoadingSpyImages, seed) ??
            Pick(merc ? entry.LoadingSpyImages : entry.LoadingMercImages, seed) ?? entry.PreviewImage;
    }

    /// <summary>
    /// The extras of each results line: the objectives and early leave of the round's scoreboard line of the same
    /// name (the scoreboard counts by participant, not by session id), the rank from the lobby.
    /// </summary>
    public static int Extras(MatchResults results, LobbyState lobby, Span<RoundResultExtra> rows)
    {
        RoundScoreboard? board = results.Scoreboard;
        for (int i = 0; i < results.Rows.Count && i < rows.Length; i++)
        {
            ResultRow r = results.Rows[i];
            ScoreboardPlayer? line = null;
            if (board is not null)
            {
                foreach (ScoreboardPlayer p in board.Rows)
                {
                    if (string.Equals(p.Name, r.Name, StringComparison.Ordinal))
                    {
                        line = p;
                        break;
                    }
                }
            }

            int rank = 0;
            foreach (LobbyPlayer p in lobby.Players)
            {
                if (p.Id == r.Id)
                {
                    rank = p.Rank;
                    break;
                }
            }

            rows[i] = new RoundResultExtra
            {
                Id = r.Id,
                Objectives = line?.Objectives ?? -1,
                LeftEarly = (line?.LeftEarly ?? false) || r.LeftEarly ? 1 : 0,
                Rank = rank != 0 ? rank : line?.Rank ?? 0,
            };
        }

        return results.Rows.Count;
    }

    /// <summary>
    /// The round's waiting list (the Godot client's RoundStartList): every player of the session in session order, his
    /// state from the PRI of the same name on this client, then the players dropped while the round loaded. It keeps
    /// the round's starting roster from one call to the next, so it must be read while the round loads.
    /// </summary>
    public static class WaitList
    {
        private static object? s_session;
        private static readonly Dictionary<int, (string Name, int Team)> s_roster = new();
        private static readonly List<(string Name, int Team)> s_dropped = new();

        public static List<(string Name, int Team, RoundWaitState State)>? Read(OnlineClient? client)
        {
            GameReplicationInfo? gri = client?.Session?.LocalPlayer?.GameReplicationInfo;
            bool waiting = client is { State: OnlineClientState.Loading } && !(gri?.bMatchHasBegun ?? false);
            if (!waiting || client is null)
            {
                return null;
            }

            // A new round (or a travel's new connection) starts a new roster.
            object key = (object?)client.Session ?? client;
            if (!ReferenceEquals(key, s_session))
            {
                s_session = key;
                s_roster.Clear();
                s_dropped.Clear();
            }

            IReadOnlyList<LobbyPlayer> players = client.Lobby.Players;
            foreach ((int id, (string name, int team)) in s_roster)
            {
                bool present = false;
                foreach (LobbyPlayer p in players)
                {
                    present |= p.Id == id;
                }

                if (!present && !s_dropped.Exists(d => d.Name == name))
                {
                    s_dropped.Add((name, team));
                }
            }

            foreach (LobbyPlayer p in players)
            {
                s_roster[p.Id] = (p.Name, p.Team);
            }

            return Lines(players, gri?.PRIArray, s_dropped);
        }

        /// <summary>RoundStartList.Lines with the players' sides.</summary>
        public static List<(string Name, int Team, RoundWaitState State)> Lines(IReadOnlyList<LobbyPlayer> players,
            IReadOnlyList<PlayerReplicationInfo?>? pris, IEnumerable<(string Name, int Team)> dropped)
        {
            var lines = new List<(string Name, int Team, RoundWaitState State)>();
            foreach (LobbyPlayer p in players)
            {
                PlayerReplicationInfo? pri = null;
                for (int i = 0; pris is not null && i < pris.Count; i++)
                {
                    if (pris[i] is { bDeleteMe: false } candidate && string.Equals(candidate.PlayerName, p.Name, StringComparison.Ordinal))
                    {
                        pri = candidate;
                        break;
                    }
                }

                lines.Add((p.Name, p.Team, pri is null ? RoundWaitState.Joining : pri.bWaitingPlayer ? RoundWaitState.Ready : RoundWaitState.Loading));
            }

            foreach ((string name, int team) in dropped)
            {
                if (!lines.Exists(l => l.Name == name))
                {
                    lines.Add((name, team, RoundWaitState.TimedOut));
                }
            }

            return lines;
        }
    }
}
