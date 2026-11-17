#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The saved gear, the gear change and the profile choices the simulation reads, from the C# bridge
// (Bridge/OpenScct.UnrealBridge/Loadout/LoadoutExports.cs). The bridge installs the level services itself when it
// loads (the Godot client's GearServices): the saved kit every spawn of the local player reads, the HUD line for the
// server's answer to a gear change, and the profile options (objective sounds, the mouse wheel, in-game help, offline
// session). This table names the file, edits the kit, asks for a gear change and pushes the options.
//
// This mirrors LoadoutExports.cs field for field; change both together and bump the version. Strings are UTF-16 and
// zero terminated, cut to fit; a text handed back is copied as far as it fits and its full length returned.

constexpr int32 ScctLoadoutApiVersion = 1;

/** The saved gear: class paths ("SBase.SGadStickyCam"), an empty path for an empty slot. */
struct FScctLoadout
{
	static constexpr int32 PathCapacity = 64;
	static constexpr int32 Slots = 4;

	TCHAR SpyGadgets[Slots][PathCapacity];
	TCHAR MercGadgets[Slots][PathCapacity];
	TCHAR MercWeapon[PathCapacity];
	/** OpenSCCT's sidearm ("OpenScct.Sidearm_unsuppressed"). */
	TCHAR MercSidearm[PathCapacity];
};

/** The rules a gear list was made under. */
struct FScctGearRules
{
	int32 Spy;
	/** Gadget slots the side has. */
	int32 Slots;
	/** The player's level (1 before a round). */
	int32 CurrentLevel;
	/** Nothing is locked by level (offline, practice). */
	int32 Unrestricted;
	int32 SidearmsEnabled;
	int32 RiflesEnabled;
	/** 1: the player's own team in the running round decided; 0: the team's stock rules. */
	int32 FromTeam;
	/** Items in the full list. */
	int32 Count;
};

namespace EScctGearKind
{
	constexpr int32 Gadget = 0;
	constexpr int32 Weapon = 1;
	constexpr int32 Sidearm = 2;
}

namespace EScctGearFlags
{
	constexpr int32 Allowed = 1;      // can be picked now
	constexpr int32 LevelLocked = 2;  // the player's level is below RequiredLevel
	constexpr int32 HostDisabled = 4; // the host's equipment rules leave it out (or sidearms / rifles are off)
	constexpr int32 Equipped = 8;     // the saved kit holds it
}

struct FScctGearItem
{
	static constexpr int32 NameCapacity = 48;

	TCHAR Path[FScctLoadout::PathCapacity];
	TCHAR Name[NameCapacity];
	int32 Kind;
	int32 RequiredLevel;
	int32 Flags;
};

/** One answer of the server to a gear change. */
struct FScctGearAnswer
{
	static constexpr int32 TextCapacity = 96;

	/** 0 the new kit went on now, 1 it goes on at the next spawn, 2 too soon (wait WaitSeconds). */
	int32 Result;
	float WaitSeconds;
	/** The HUD line ("Gear changed."), already shown in the HUD's message area. */
	TCHAR Text[TextCapacity];
};

/** The profile choices the simulation reads. Pushed, -1 in SelectedMouse or OfflineSession means "follow". */
struct FScctProfileOptions
{
	int32 ObjectiveSounds;
	/** 0 the wheel changes gadgets, 1 it walks and runs; -1 follows the controls' mouse wheel setting. */
	int32 SelectedMouse;
	int32 DisplayHelpInGame;
	/** 1 an offline session (zone tips show), 0 not; -1 follows whether a local match runs. */
	int32 OfflineSession;
};

struct FScctLoadoutApi
{
	int32 Version;

	/** Names the loadout file and reads it. Returns 1 read, 2 no file yet (the defaults are in use), 0 failed. */
	int32 (__cdecl* Load)(const TCHAR* Path);

	/** Writes the kit in use (Path null or empty: the file loaded). Returns 1, or 0 with the reason in LastError. */
	int32 (__cdecl* Save)(const TCHAR* Path);

	void (__cdecl* GetLoadout)(FScctLoadout* Loadout);

	/** Puts a kit in use, cleaned as a read file is; the next spawn (or a gear change) reads it. Returns 1 or 0. */
	int32 (__cdecl* SetLoadout)(const FScctLoadout* Loadout);

	/** The fixed side kits a new player starts with. */
	void (__cdecl* GetDefaults)(FScctLoadout* Loadout);

	/**
	 * The gear a side can pick (Side 0 spy, 1 merc, -1 the local player's), with level locks and the host's rules.
	 * Returns the full count; items past Capacity are not written.
	 */
	int32 (__cdecl* ListGear)(int32 Side, FScctGearRules* Rules, FScctGearItem* Items, int32 Capacity);

	/** Asks the server to put the saved kit on the local player now. Returns 1 asked, 0 no match (LastError). */
	int32 (__cdecl* RequestGearChange)();

	/** Takes the oldest answer of the server not read yet. Returns 1 with it, 0 when there is none. */
	int32 (__cdecl* PollGearAnswer)(FScctGearAnswer* Answer);

	void (__cdecl* SetProfileOptions)(const FScctProfileOptions* Options);

	/** What the simulation reads now ("follow" values worked out). */
	void (__cdecl* GetProfileOptions)(FScctProfileOptions* Options);

	/** The local pawn's kit by class name ("spy SGadA, SGadB | SStickyShocker"); length 0 without a pawn. */
	int32 (__cdecl* GetLocalInventory)(TCHAR* Buffer, int32 Capacity);

	/** How many times a spawn on this machine read the saved kit, spy and merc (they only grow). */
	void (__cdecl* GetKitRequests)(int32* Spy, int32* Merc);

	/** The last failure inside this table. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};

// The sizes the C# side has (Bridge/Probes/LoadoutProbe and BindingsProbe print them).
static_assert(sizeof(FScctLoadout) == 1280, "FScctLoadout must match BridgeLoadout");
static_assert(sizeof(FScctGearRules) == 32, "FScctGearRules must match BridgeGearRules");
static_assert(sizeof(FScctGearItem) == 236, "FScctGearItem must match BridgeGearItem");
static_assert(sizeof(FScctGearAnswer) == 200, "FScctGearAnswer must match BridgeGearAnswer");
static_assert(sizeof(FScctProfileOptions) == 16, "FScctProfileOptions must match BridgeProfileOptions");
static_assert(sizeof(FScctLoadoutApi) == 112, "FScctLoadoutApi must match LoadoutApi (64-bit)");

// Fetched like every other feature table (Runtime/ScctFeatures.h), declared here so the shared list stays as it is.
template <> struct TScctFeatureInfo<FScctLoadoutApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.LoadoutExports"); }
	static constexpr int32 Version = ScctLoadoutApiVersion;
};
