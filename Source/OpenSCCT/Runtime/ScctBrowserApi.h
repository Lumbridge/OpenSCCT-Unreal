#pragma once

#include "CoreMinimal.h"

// The server browser (the Godot client's ServerBrowserScreen): one list of the master's servers, a LAN search on the
// LAN port and the player's saved addresses, each server asked for its info on its game port so the list shows a
// measured ping, the phase, the players and how it is reached; and a join that takes a password and a side. Mirrors
// Bridge/OpenScct.UnrealBridge/Online/BrowserExports.cs field for field; change both together and bump the version.
//
// The search runs inside the online table's Update (Runtime/ScctOnlineApi.h), which the online subsystem calls every
// frame; GetRevision goes up when a row or the status changed. A join started here is an ordinary visit afterwards
// (the online status follows it). Text is UTF-16, zero terminated.

constexpr int32 ScctBrowserApiVersion = 1;

/** FScctBrowserRow::Flags: the online table's EScctOnlineServerFlags (Password, Dedicated... Full), plus these. */
namespace EScctBrowserRowFlags
{
	constexpr int32 Answered = 512; // the server answered a query: ping, phase and players are its own
	constexpr int32 Saved = 1024;   // one of the player's saved addresses
	constexpr int32 OnLan = 2048;   // the LAN search found it
}

/** Where a row came from. */
enum class EScctBrowserSource : int32
{
	Internet = 0, // the master's list
	Lan = 1,      // the LAN search (saved too, maybe)
	Saved = 2,    // a saved address only
};

/** How a row's server is reached. */
enum class EScctBrowserReach : int32
{
	Checking = 0, // the master does not know yet
	Direct = 1,
	Punch = 2,    // NAT punch through the master's introducer
	Relay = 3,
	Lan = 4,
};

/** One server of the browser (GetRows). */
struct FScctBrowserRow
{
	int32 PingMs;     // the lowest of the last answers, -1 while it has not answered
	int32 Players, MaxPlayers;
	int32 Mode;       // EGameMode, as FScctOnlineStatus::Mode
	int32 Phase;      // 0 lobby, 1 loading, 2 match, 3 results
	int32 Flags;      // EScctOnlineServerFlags | EScctBrowserRowFlags
	int32 Source;     // EScctBrowserSource
	int32 Reach;      // EScctBrowserReach
	TCHAR Key[96];    // stays the same for one server while the page is open
	TCHAR Name[64];
	TCHAR Map[32];
	TCHAR Region[16]; // the master's region of the server (EU, US...), empty when not listed or none given
	TCHAR Address[64];// "ip:port" it answered on, or the master's address for it
	TCHAR Problem[48];// why this install cannot join it ("another build", "missing map X", "full"...), empty when it can
};

enum class EScctBrowserText : int32
{
	Status = 0,  // what the browser is doing: the master's state, LAN and saved answers
	Refusal = 1, // why the last Join was refused, or empty
};

struct FScctBrowserApi
{
	int32 Version;

	/**
	 * A new search: the LAN on LanPort, the saved addresses (host or host:port, separated by ';'), and the master's
	 * list when one is set. bLoopbackOnly 1: the LAN search asks this machine only (unattended runs: no firewall
	 * prompt). Returns 1, or 0 when no UDP socket could be opened (the master's servers still come).
	 */
	int32 (__cdecl* Open)(int32 LanPort, const TCHAR* SavedServers, int32 bLoopbackOnly);

	/** Stops searching (the page closed). */
	void (__cdecl* Close)();

	/** Goes up whenever a row or the status changed. */
	int32 (__cdecl* GetRevision)();

	/** Writes up to Capacity rows. Returns the number of rows there are. */
	int32 (__cdecl* GetRows)(FScctBrowserRow* Rows, int32 Capacity);

	/** A text (EScctBrowserText). Returns its full length in characters. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** The players of the row at Index by side ("Spies: a, b     Mercenaries: c"). Returns its full length in characters. */
	int32 (__cdecl* GetPlayers)(int32 Index, TCHAR* Buffer, int32 Capacity);

	/**
	 * Joins the row at Index with a password (null for none) and a side (0 spies, 1 mercs, 255 watch, -1 the smaller
	 * side): a listed server through the master (the connect mode, the signed-in player's ticket), a LAN or saved one
	 * straight to its address. Returns 1 when the join started, 0 when refused (GetText Refusal says why).
	 */
	int32 (__cdecl* Join)(int32 Index, const TCHAR* Password, int32 PreferredTeam);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctBrowserRow) == 8 * 4 + (96 + 64 + 32 + 16 + 64 + 48) * 2, "FScctBrowserRow does not match BrowserRowData");
static_assert(sizeof(FScctBrowserApi) == 8 + 7 * sizeof(void*), "FScctBrowserApi does not match BrowserApi");
