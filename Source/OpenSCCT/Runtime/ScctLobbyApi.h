#pragma once

#include "CoreMinimal.h"

// The lobby's moderation: the host's or an admin's Kick, Ban and Voice Ban from the player rows (the Godot client's
// OnlineLobby), and a packed rank as the lobby shows it. Mirrors Bridge/OpenScct.UnrealBridge/Online/LobbyExports.cs
// field for field; change both together and bump the version.
//
// It works on the online visit (Runtime/ScctOnlineApi.h): player ids are FScctOnlinePlayer::Id. The server decides
// whether a request is allowed (the host or an admin, never on a bot or oneself); a refused one changes nothing. What
// happens comes back as the visit does: the kicked player leaves the list, a voice ban sets his VoiceBanned flag, and
// the server says it in a system chat line. Text is UTF-16, zero terminated.

constexpr int32 ScctLobbyApiVersion = 1;

struct FScctLobbyApi
{
	int32 Version;

	/** Asks the server to kick a player. Returns 1 when sent (a visit is on), 0 otherwise. */
	int32 (__cdecl* Kick)(int32 PlayerId);

	/** Asks the server to ban a player (kicked, and kept out by install id and address). Returns 1 when sent. */
	int32 (__cdecl* Ban)(int32 PlayerId);

	/** Asks the server to ban a player from voice chat (bBanned 1) or to lift the ban (0). Returns 1 when sent. */
	int32 (__cdecl* SetVoiceBanned)(int32 PlayerId, int32 bBanned);

	/** A packed rank (FScctOnlinePlayer::Rank) as text: "P2 41" with the title after a middle dot, "41", or empty for none. Returns its full length in characters. */
	int32 (__cdecl* RankText)(int32 PackedRank, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctLobbyApi) == 8 + 4 * sizeof(void*), "FScctLobbyApi does not match LobbyApi");
