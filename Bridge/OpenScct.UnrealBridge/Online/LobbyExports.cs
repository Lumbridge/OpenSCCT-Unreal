using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Net.Cosmetics;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The lobby's moderation entry points Unreal calls (Source/OpenSCCT/Runtime/ScctLobbyApi.h mirrors it): the host's or
/// an admin's Kick, Ban and Voice Ban from the player rows (the Godot client's OnlineLobby.Kick, Ban and
/// SetVoiceBanned), and a packed rank as the lobby shows it. A table of its own, so the online table keeps its layout.
/// The server decides whether a request is allowed (only the host or an admin, never a bot or oneself); a refused one
/// changes nothing. Every entry catches its exceptions. Strings are UTF-16, zero terminated. New entries are added at
/// the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LobbyApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, int> Kick;
    public delegate* unmanaged[Cdecl]<int, int> Ban;
    public delegate* unmanaged[Cdecl]<int, int, int> SetVoiceBanned;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> RankText;

    // Version 2: the lobby's extras for every player (Online/LobbyExtras.cs).
    public delegate* unmanaged[Cdecl]<LobbyScoreboardInfo*, int> GetLastScoreboard;
    public delegate* unmanaged[Cdecl]<LobbyScoreRow*, int, int> GetLastScoreboardRows;
    public delegate* unmanaged[Cdecl]<int, int, int, char*, int, int> GetLastScoreboardText;
    public delegate* unmanaged[Cdecl]<int, int, char*, int, int> GetBriefingText;
    public delegate* unmanaged[Cdecl]<LobbyBriefingMark*, int, int> GetBriefingMarks;
    public delegate* unmanaged[Cdecl]<int, int> RankColour;
    public delegate* unmanaged[Cdecl]<int, int, char*, int, int> RankWords;
}

public static unsafe class LobbyExports
{
    public const int ApiVersion = 2;

    private static OnlineBridge Online => OnlineBridge.Instance;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(LobbyApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Kick = &Kick;
        api->Ban = &Ban;
        api->SetVoiceBanned = &SetVoiceBanned;
        api->RankText = &RankText;
        api->GetLastScoreboard = &GetLastScoreboard;
        api->GetLastScoreboardRows = &GetLastScoreboardRows;
        api->GetLastScoreboardText = &GetLastScoreboardText;
        api->GetBriefingText = &GetBriefingText;
        api->GetBriefingMarks = &GetBriefingMarks;
        api->RankColour = &RankColour;
        api->RankWords = &RankWords;
        return ApiVersion;
    }

    /// <summary>Asks the server to kick a player of the session (by the id GetPlayers gives). Returns 1 when sent.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Kick(int playerId) => Send(() => Online.Kick(playerId));

    /// <summary>Asks the server to ban a player of the session. Returns 1 when sent.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Ban(int playerId) => Send(() => Online.Ban(playerId));

    /// <summary>Asks the server to ban a player from voice chat (banned 1) or to lift it (0). Returns 1 when sent.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetVoiceBanned(int playerId, int banned) => Send(() => Online.SetVoiceBanned(playerId, banned != 0));

    /// <summary>
    /// A packed rank (FScctOnlinePlayer::Rank) as the lobby's rank column shows it: "P2 41 · Title", "41", or empty for
    /// no rank. Copies it into <paramref name="buffer"/> and returns its full length in characters.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RankText(int packed, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, RankOf(packed));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>The managed side of <c>RankText</c> (the probe calls it too).</summary>
    public static string RankOf(int packed)
    {
        PlayerRank rank = PlayerRank.Unpack(packed);
        if (rank.IsNone)
        {
            return string.Empty;
        }

        return rank.Title is { } title ? rank.Tag + " · " + title.Text : rank.Tag;
    }

    // ---- version 2: the last round's scoreboard, the briefing, rank colours and words --------------------------

    /// <summary>
    /// The last round's scoreboard (OnlineClient.LastScoreboard, the Godot lobby's "View scoreboard"): fills
    /// <paramref name="info"/> and returns 1, or 0 when this visit has played no round yet (or there is no visit).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLastScoreboard(LobbyScoreboardInfo* info)
    {
        try
        {
            if (Online.Client is not { LastScoreboard: { } board } client)
            {
                return 0;
            }

            if (info != null)
            {
                *info = LobbyExtras.Info(board, client.LastScoreboardLocalParticipantId);
            }

            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the last scoreboard's rows (the local player's side first). Returns their count, -1 without one.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLastScoreboardRows(LobbyScoreRow* rows, int capacity)
    {
        try
        {
            if (Online.Client is not { LastScoreboard: { } board } client)
            {
                return -1;
            }

            int local = client.LastScoreboardLocalParticipantId;
            OpenScct.Net.Scoreboards.ScoreboardPlayer[] list = LobbyExtras.Rows(board, local);
            for (int i = 0; i < list.Length && rows != null && i < capacity; i++)
            {
                rows[i] = default;
                LobbyExtras.FillRow(ref rows[i], list[i], local);
            }

            return list.Length;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>A text of the last scoreboard (LobbyScoreboardText, with its two arguments). Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLastScoreboardText(int kind, int a, int b, char* buffer, int capacity)
    {
        try
        {
            string text = Online.Client is { LastScoreboard: { } board } client
                ? LobbyExtras.ScoreboardText(board, client.LastScoreboardLocalParticipantId, Online.Catalog.Maps, (LobbyScoreboardText)kind, a, b)
                : string.Empty;
            return OnlineTextCopy.Buffer(buffer, capacity, text);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>
    /// A text of the setup's briefing (LobbyBriefingText) for a side (0 spies, 1 mercs, -1 the local player's: a
    /// spectator reads the mercs'). Returns its full length; empty outside a visit.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetBriefingText(int kind, int side, char* buffer, int capacity)
    {
        try
        {
            string text = Online.Client is { } client ? LobbyExtras.BriefingText(client, Online.Catalog.Maps, (LobbyBriefingText)kind, side) : string.Empty;
            return OnlineTextCopy.Buffer(buffer, capacity, text);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>Writes the place numbers to draw on the briefing plan. Returns their count (only capacity are written).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetBriefingMarks(LobbyBriefingMark* marks, int capacity)
    {
        try
        {
            if (Online.Client is not { } client)
            {
                return 0;
            }

            System.Collections.Generic.List<LobbyBriefingMark> list = LobbyExtras.Marks(Online.Catalog.Maps, client.Lobby.Setup.Map);
            for (int i = 0; i < list.Count && marks != null && i < capacity; i++)
            {
                marks[i] = list[i];
            }

            return list.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>A packed rank's emblem colour, 0xRRGGBB (white for none).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RankColour(int packed)
    {
        try
        {
            return LobbyExtras.RankColour(packed);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0xffffff;
        }
    }

    /// <summary>A packed rank in words: kind 0 the badge ("P3 · 42", no title), 1 the description ("Prestige 3, level 42 - Title").</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RankWords(int packed, int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, kind == 1 ? LobbyExtras.RankDescribe(packed) : LobbyExtras.RankTag(packed));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>True when a visit is on (the request went to the server, which may still refuse it).</summary>
    private static int Send(Action request)
    {
        try
        {
            if (Online.Client is null)
            {
                return 0;
            }

            request();
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
