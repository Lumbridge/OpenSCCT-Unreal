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
}

public static unsafe class LobbyExports
{
    public const int ApiVersion = 1;

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
