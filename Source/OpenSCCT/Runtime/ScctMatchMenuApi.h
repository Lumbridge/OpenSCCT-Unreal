#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The in-match menu, from the C# bridge (Bridge/OpenScct.UnrealBridge/MatchMenu/MatchMenuExports.cs, over the Godot
// client's InGameMenuModel linked from game/Scripts/UI): who the player is when the menu opens (host or not, online
// or not, tutorial, practice, lives...), the buttons the Godot client shows for that, and the actions of PlayMenu.Act
// that need the simulation or the online visit: switch team, resign, return to lobby. Also the round's goals for the
// help page.
//
// This mirrors MatchMenuExports.cs field for field; change both together and bump the version. Strings are UTF-16
// and zero terminated, cut to fit; the full length is returned.

constexpr int32 ScctMatchMenuApiVersion = 1;

/** The buttons of the In-Game Menu (InGameMenuButton). */
namespace EScctMatchMenuButton
{
	constexpr int32 Back = 0;
	constexpr int32 ChangeGear = 1;
	constexpr int32 SwitchTeam = 2;
	constexpr int32 StrategicMap = 3;
	constexpr int32 Options = 4;
	constexpr int32 Resign = 5;
	constexpr int32 ReturnToLobby = 6;
	constexpr int32 QuitSession = 7;
	constexpr int32 Help = 8;
}

/** InGameMenuContext, and the entries InGameMenuModel.Layout gives for it. */
struct FScctMatchMenuContext
{
	static constexpr int32 MaxEntries = 12;

	int32 IAmServer;      // the player runs the round (offline always; online the lobby's host)
	int32 Online;         // a round on a server
	int32 Tutorial;
	int32 MatchBegun;
	int32 GameEnded;
	int32 Spectator;
	int32 Lives;          // 100 or more reads as unlimited
	int32 Practice;       // a practice lobby online, unlimited gear offline
	int32 MercDeathMatch;
	int32 CanSwitchTeam;
	int32 CanResign;
	int32 LocalTeam;      // 0 spies, 1 mercs, -1 none yet
	int32 Merc;           // the local player plays a merc
	int32 GameMode;       // 1 Extraction, 2 Sabotage, 3 Neutralization, 4 Disk Hunt, 5 Story, 6 Deathmatch, 7 Merc Deathmatch
	int32 EntryCount;
	int32 Entries[MaxEntries]; // EScctMatchMenuButton, top to bottom
};

struct FScctMatchMenuApi
{
	int32 Version;

	/** Writes the context and its entries. Returns 1 while a round is being played. */
	int32 (__cdecl* GetContext)(FScctMatchMenuContext* Context);

	/** Asks for the other side (online the lobby's SetTeam, offline the practice side change). 1, or 0 with the reason in LastError; on 1 LastError says what was asked. */
	int32 (__cdecl* SwitchTeam)();

	/** Online: the "!resign" chat command. 1, or 0 with the reason in LastError. */
	int32 (__cdecl* Resign)();

	/** The online host: everyone back to the lobby. 1, or 0 with the reason in LastError. */
	int32 (__cdecl* ReturnToLobby)();

	/** The round's goals, lines broken with '\n' (the mode, what it asks, the lives, the side's objectives). */
	int32 (__cdecl* GetGoals)(TCHAR* Buffer, int32 Capacity);

	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};

// The sizes the C# side has (BridgeMatchMenuContext: 27 ints; MatchMenuApi: the version, padding and six pointers).
static_assert(sizeof(FScctMatchMenuContext) == 108, "FScctMatchMenuContext must match BridgeMatchMenuContext");
static_assert(sizeof(FScctMatchMenuApi) == 56, "FScctMatchMenuApi must match MatchMenuApi (64-bit)");

// Fetched like every other feature table (Runtime/ScctFeatures.h), declared here so the shared list stays as it is.
template <> struct TScctFeatureInfo<FScctMatchMenuApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.MatchMenuExports"); }
	static constexpr int32 Version = ScctMatchMenuApiVersion;
};
