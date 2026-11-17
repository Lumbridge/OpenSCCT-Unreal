#pragma once

#include "CoreMinimal.h"

// Rejoining a dropped round: the round the game was playing is remembered in Saved/Online/rejoin.json while it is
// played (the server's password sealed with DPAPI), and after a crash, a lost connection or a quit the main menu
// offers it again once the master or the server confirmed it is still being played. A signed-in player is also
// offered the round the account dropped out of on any PC (the master's account answer). The server puts the player
// back on the side, with the score and lives the player left with. Mirrors
// Bridge/OpenScct.UnrealBridge/Online/RejoinExports.cs field for field; change both together and bump the version.
// The online table (Runtime/ScctOnlineApi.h) follows the rejoin's visit as any other. Text is UTF-16, zero terminated.

constexpr int32 ScctRejoinApiVersion = 1;

/** The round offered for a rejoin. */
struct FScctRejoinChoice
{
	int32 Revision;    // goes up whenever the offer changes
	int32 Offered;     // 1 when a round is offered
	int32 FromAccount; // 1 when the master offered it for the signed-in account
	int32 HasPassword; // 1 when the server's password was kept with it
	int32 Checking;    // 1 while the master or the server is being asked
	TCHAR ServerName[64];
	TCHAR Map[32];
	TCHAR Where[64];   // the address, or "master id ..."
};

struct FScctRejoinApi
{
	int32 Version;

	/** Where the record is kept; null or empty turns rejoining off (unattended test runs). */
	void (__cdecl* SetFile)(const TCHAR* Path);

	/** The main menu is up: checks the remembered round (once per drop) and the account's offer. Answers arrive in later frames. */
	void (__cdecl* Check)();

	/** Fills Out (may be null). Returns 1 when a round is offered. */
	int32 (__cdecl* GetRejoin)(FScctRejoinChoice* Out);

	/** Joins the offered round's server again. Returns 1 when the join started. */
	int32 (__cdecl* Rejoin)();

	/** Not now: the round is not offered again. */
	void (__cdecl* Dismiss)();

	/** Writes the next line for the log (found, asked, confirmed, offered, forgotten), cut to fit. Returns its full length, -1 when none waits. */
	int32 (__cdecl* TakeLine)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctRejoinChoice) == 5 * 4 + (64 + 32 + 64) * 2, "FScctRejoinChoice does not match RejoinChoiceData");
static_assert(sizeof(FScctRejoinApi) == 8 + 6 * sizeof(void*), "FScctRejoinApi does not match RejoinApi");
