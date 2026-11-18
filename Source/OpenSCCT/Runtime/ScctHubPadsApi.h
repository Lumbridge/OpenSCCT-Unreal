#pragma once

#include "CoreMinimal.h"

// The hub's match pads half of the C# bridge (an OpenSCCT addition): each pad's state, match, queue counts, floor,
// sign, theme kit and map picture; the player's own queue line; the trip to a pad's match and the note about it; the
// open pad laptop's draft (its rows, its maps, apply); and the unattended pad run. These mirror
// Bridge/OpenScct.UnrealBridge/Hub/HubPadsTypes.cs and HubPadsExports.cs field for field; change both together and
// bump the version. Bridge/Probes/HubProbe (mode "social") prints the sizes asserted below.
//
// The table is fetched like the other feature tables: ScctFeature<FScctHubPadsApi>() (Runtime/ScctFeatures.h). It
// reads the visit of the hub table (ScctHubApi.h); while not in a hub the reads return nothing. Positions are in UE
// units. Text is UTF-16, zero terminated. Game thread only.

constexpr int32 ScctHubPadsApiVersion = 1;

/** A pad's state, as the hub sends it. */
enum class EScctHubPadState : int32
{
	Off = 0,       // the hub cannot run the pad's match
	Idle = 1,      // nobody queued
	Waiting = 2,   // players queued, not enough or not all ready
	Countdown = 3, // the match starts when the countdown ends
	Starting = 4,  // the match is being started; the queued players are sent to it
};

namespace EScctHubPadsFlags
{
	// FScctHubPadsState::Flags
	constexpr int32 InHub = 1;   // the visit's session is drawn
	constexpr int32 Laptop = 2;  // a pad's laptop is open (GetText(LaptopPad))
	constexpr int32 InMatch = 4; // away at a pad's match (GetText(MatchStatus))
	constexpr int32 Hosting = 8; // this PC hosts the hub

	// FScctHubPadRow::Flags
	constexpr int32 Queued = 1;      // the player is queued on this pad
	constexpr int32 Customised = 2;  // its match was set at its laptop
	constexpr int32 HasPicture = 4;  // Picture names the map's picture, a PNG on this PC
	constexpr int32 HasSign = 8;     // Sign* say where the pad's sign stands
	constexpr int32 SignMounted = 16; // the sign lies on a board's face (SignYaw, SignWidth, SignHeight)
	constexpr int32 Ready = 32;      // the player is queued here and ready

	// FScctHubLaptopMap::Flags
	constexpr int32 Selected = 1; // the laptop's draft plays this map
	constexpr int32 Downloadable = 2; // not installed: picking it downloads it (its name says the size)
}

/** The texts GetText copies. */
enum class EScctHubPadsText : int32
{
	Queue = 0,         // "Queued on Pad 2 as a merc: 3/4, ...", empty when on no pad
	QueuePad = 1,      // the id of the pad the player is queued on
	Note = 2,          // the note about the last match (going, back, why), empty once it is old
	MatchStatus = 3,   // what the trip to a pad's match is doing, empty when there is none
	LaptopPad = 4,     // the id of the pad whose laptop is open
	LaptopStatus = 5,  // what the laptop says (applied, why not)
	LaptopMap = 6,     // the draft's map code
	LaptopSummary = 7, // the draft in one line
};

struct FScctHubPadsState
{
	int32 Valid;        // 1 while in a hub, or away at one of its matches
	int32 PadCount;
	int32 Revision;     // the hub bridge's revision
	int32 LaptopSerial; // goes up each time the hub opened a pad's laptop for the player
	int32 QueueSide;    // 0 spies, 1 mercs, -1 not queued
	int32 Flags;        // EScctHubPadsFlags (the state's group)
	float Now;          // the bridge's clock, seconds
	int32 Reserved;
};

/** One match pad. */
struct FScctHubPadRow
{
	int32 State;   // EScctHubPadState
	int32 Seconds; // whole seconds left of the countdown
	int32 Number;  // Pad2 is 2
	int32 Mode;    // EGameMode
	int32 TeamSize;
	int32 Spies, SpySlots, Mercs, MercSlots;
	int32 Flags;   // EScctHubPadsFlags (the pad's group)
	uint32 Rgba;   // the state's colour, 0xRRGGBBAA
	float LoX, LoY, HiX, HiY, FloorZ;
	float SignX, SignY, SignZ, SignYaw, SignWidth, SignHeight; // the sign's anchor; its board's facing (65536 a turn) and size
	TCHAR Id[16];
	TCHAR Map[32];
	TCHAR Title[64];
	TCHAR Theme[16];   // hub, polar, steel, aquarius, lab, museum
	TCHAR ModeName[32];
	TCHAR Picture[260];
};

/** A row of the open laptop. */
struct FScctHubLaptopRow
{
	int32 Index;
	int32 Count;
	int32 Enabled;
	TCHAR Key[24];
	TCHAR Label[32];
	TCHAR Value[48];
};

/** A map the open laptop offers. */
struct FScctHubLaptopMap
{
	int32 Flags; // EScctHubPadsFlags (the laptop map's group)
	TCHAR Id[32];
	TCHAR Name[64];
	TCHAR Modes[32];
	TCHAR Picture[260];
};

struct FScctHubPadsApi
{
	int32 Version;

	/** The state. Returns Valid. */
	int32 (__cdecl* GetState)(FScctHubPadsState* State);

	/** Writes up to Capacity pads, by number. Returns how many there are. */
	int32 (__cdecl* GetPads)(FScctHubPadRow* Pads, int32 Capacity);

	/** Copies a text (EScctHubPadsText). Returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** Writes the open laptop's rows. Returns how many there are (0 when none is open). */
	int32 (__cdecl* GetLaptopRows)(FScctHubLaptopRow* Rows, int32 Capacity);

	/** Steps a row by Delta (it wraps). Returns 1 when it changed. */
	int32 (__cdecl* StepLaptopRow)(int32 Row, int32 Delta);

	/** Writes the maps the open laptop offers (installed versus maps). Returns how many there are. */
	int32 (__cdecl* GetLaptopMaps)(FScctHubLaptopMap* Maps, int32 Capacity);

	/** Picks the draft's map. Returns 1 when it changed. */
	int32 (__cdecl* SetLaptopMap)(const TCHAR* Map);

	/** The map's own settings for the draft. Returns 1 when done. */
	int32 (__cdecl* LaptopDefaults)();

	/** Lays a lobby's options ("?TimeLimit=1?SvmBots=2") over the draft. Returns 1 when done. */
	int32 (__cdecl* LaptopMerge)(const TCHAR* Options);

	/** Sends the draft to the hub (it checks the player stands at the laptop). Returns 1 when sent. */
	int32 (__cdecl* ApplyLaptop)();

	/** Closes the laptop. */
	void (__cdecl* CloseLaptop)();

	/** The unattended pad run: queue the host's player on pad Number as a spy (0) or merc (1), the laptop taking Options. Returns 1 when started. */
	int32 (__cdecl* QueueOnPad)(int32 Number, int32 Side, const TCHAR* Options);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctHubPadsState) == 32, "FScctHubPadsState does not match HubPadsState");
static_assert(sizeof(FScctHubPadRow) == 928, "FScctHubPadRow does not match HubPadRow");
static_assert(sizeof(FScctHubLaptopRow) == 220, "FScctHubLaptopRow does not match HubLaptopRow");
static_assert(sizeof(FScctHubLaptopMap) == 780, "FScctHubLaptopMap does not match HubLaptopMap");
static_assert(sizeof(FScctHubPadsApi) == 8 + 12 * sizeof(void*), "FScctHubPadsApi does not match HubPadsApi");
