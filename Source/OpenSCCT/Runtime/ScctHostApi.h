#pragma once

#include "CoreMinimal.h"

// Hosting from the game: a listen server in this process that the local player joins as the lobby's host (the Godot
// client's ListenHost). Mirrors Bridge/OpenScct.UnrealBridge/Online/HostExports.cs field for field; change both
// together and bump the version.
//
// Hosting is part of the online visit (Runtime/ScctOnlineApi.h): once Host returned a port, the online table follows
// the host's own player through the lobby, the rounds and the results as it follows any visit, and its Leave closes
// the server. The session runs on its own thread; nothing here blocks a frame except Host itself (it opens the port).
// Text is UTF-16, zero terminated.

constexpr int32 ScctHostApiVersion = 2;

/** FScctHostApi::Host's flags. */
namespace EScctHostFlags
{
	constexpr int32 Listed = 1;          // list the server on the master set with the online table's SetMaster
	constexpr int32 Upnp = 2;            // forward the port on the router with UPnP (the result is the Upnp text)
	constexpr int32 RequireAccounts = 4; // only signed-in players may join (needs a master)
	constexpr int32 RecordDemos = 8;     // record every round as a demo into DemoFolder
	constexpr int32 Lan = 16;            // answer LAN searches on LanPort
	constexpr int32 LoopbackOnly = 32;   // bind 127.0.0.1 and ::1 only: tests on one machine (no firewall prompt, no UPnP, no master)
}

/** FScctHostApi::GetHostText's kinds. */
enum class EScctHostText : int32
{
	Status = 0,  // "Lobby on AquaE (Story), 2/8 players, 0 round(s) started", written by the host thread about once a second
	Players = 1, // "1 Name (spy, host); 2 Other (merc, ready)"
	Master = 2,  // the master listing (registered id, reachability, public address, or why not) and the LAN beacon
	Upnp = 3,    // the UPnP mapping's result; empty while the router is being asked
	Error = 4,   // why the last Host failed (the online visit's error)
};

/** FScctHostApi::SetupText's kinds: what the Create Session page asks (Bridge/Online/HostSetupModel.cs). */
enum class EScctSetupText : int32
{
	Maps = 0,          // the hostable maps, one per line: "Id TAB name TAB picture path TAB mode keys (comma separated)"
	Modes = 1,         // the modes of the map named by Argument, as mode keys separated by commas, in the page's order
	Defaults = 2,      // a fresh setup for Argument's ?MapName=..?Mode=.. on top of Options (session-wide values kept); returns the setup line
	Normalize = 3,     // Options as the setup line the session reads, every value in the page's ranges
	Describe = 4,      // the setup in one line as the lobby describes it
	Rows = 5,          // the page's rows: "Row TAB number|check|choice TAB caption TAB shown text TAB value TAB min TAB max TAB visible TAB choices (|)", then "hint TAB Voice|Bots TAB text"
	Edit = 6,          // Argument "Row value" ("TimeLimit 12", "Mode DiskHunt", "Map AquaE") applied with the page's rules; returns the setup line
	Alternate = 7,     // the Alternate rules: "ability TAB bit TAB side TAB caption TAB on", "gadget TAB side TAB option TAB name TAB allowed", "slots TAB side TAB count TAB most"
	EditAlternate = 8, // Argument "ability BIT 0|1", "gadget SIDE OPTION 0|1", "slots SIDE COUNT" or "reset"; returns the setup line
	NormalizeNewHost = 9, // restore a local host draft with the standard equipment and smoke rules enabled
};

struct FScctHostApi
{
	int32 Version;

	/**
	 * Hosts a game and joins it as the host. SetupOptions is the first match as an options line: MapName, Mode as a
	 * mode key (Story, DiskHunt, Deathmatch, MercDeathmatch...) or GameMode as a number, then any lobby value
	 * ("?MapName=AquaE?Mode=Story?TimeLimit=10?SvmBots=2"). Port 0 lets the system pick one. Password and DemoFolder may
	 * be null. Returns the UDP port it listens on, or 0 (the online state is then Failed with the reason).
	 */
	int32 (__cdecl* Host)(const TCHAR* Name, const TCHAR* Password, int32 Port, int32 MaxPlayers, const TCHAR* SetupOptions, int32 Flags, int32 LanPort, const TCHAR* DemoFolder);

	/** Closes the server (the players hear it closed, the master unlists it, the UPnP mapping goes). The visit ends with it. */
	void (__cdecl* StopHosting)();

	/** The UDP port of the player's server while it runs, else 0. */
	int32 (__cdecl* IsHosting)();

	/** An admin command on the player's own server (status, players, kick, ban, map, set, say...); the answers arrive as system chat lines. Returns 1 when queued. */
	int32 (__cdecl* HostCommand)(const TCHAR* Line);

	/** Copies a text (EScctHostText) into Buffer. Returns its full length in characters. */
	int32 (__cdecl* GetHostText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** Writes the next line of the host's log (joins, launches, the master, UPnP, command answers), cut to fit. Returns its full length, -1 when none waits. */
	int32 (__cdecl* TakeHostLine)(TCHAR* Buffer, int32 Capacity);

	/** How listed servers are joined: 0 as the master suggests, 1 direct only, 2 NAT punch only, 3 relay only. */
	void (__cdecl* SetConnectPath)(int32 Path);

	// Version 2.

	/**
	 * Create Session's rules (EScctSetupText): OpenSCCT's CreateSessionModel over the bridge's map catalog. Options is the
	 * page's setup line (may be null: the first map's defaults); MaxPlayers the slots the host picked, 0 while they follow
	 * the map. Copies the text into Buffer and returns its full length in characters.
	 */
	int32 (__cdecl* SetupText)(int32 Kind, const TCHAR* Options, const TCHAR* Argument, int32 MaxPlayers, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctHostApi) == 8 + 8 * sizeof(void*), "FScctHostApi does not match HostApi");
