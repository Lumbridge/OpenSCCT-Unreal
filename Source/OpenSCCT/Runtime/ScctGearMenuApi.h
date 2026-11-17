#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctLoadoutApi.h"

// The gear picker, from the C# bridge (Bridge/OpenScct.UnrealBridge/Loadout/GearMenuExports.cs, the Godot client's
// GearMenuModel ported): one draft kit per side built from the saved kit when the picker opens, the catalogue of a
// slot with the level locks and the host's rules, the intel panel's texts and stats, and APPLY, which writes both
// drafts into the saved kit and Saved/Controls/loadout.json. Asking the server to put the kit on is the loadout
// table's RequestGearChange (Runtime/ScctLoadoutApi.h).
//
// This mirrors GearMenuExports.cs field for field; change both together and bump the version. Strings are UTF-16 and
// zero terminated, cut to fit.

constexpr int32 ScctGearMenuApiVersion = 1;

namespace EScctGearMenuContext
{
	constexpr int32 Menus = 0; // the main menu and the offline page: the stock rules, nothing locked
	constexpr int32 Lobby = 1; // a server's lobby: the slots and rules its setup will give
	constexpr int32 Match = 2; // a round: the player's own team decides; the other side's tab has the stock rules
}

namespace EScctGearCategory
{
	constexpr int32 Gadget = 0;
	constexpr int32 Weapon = 1;
	constexpr int32 Sidearm = 2;
}

namespace EScctGearOptionFlags
{
	constexpr int32 Allowed = 1;      // can be picked
	constexpr int32 LevelLocked = 2;  // the player's level is below RequiredLevel
	constexpr int32 HostDisabled = 4; // the host's equipment rules leave it out
	constexpr int32 Equipped = 8;     // a slot of the draft holds it
	constexpr int32 Selected = 16;    // the slot being edited holds it
	constexpr int32 QuickUse = 32;    // thrown with a quick-use key from the spy's first two slots
	constexpr int32 Suppressed = 64;  // a suppressed rifle
}

/** The draft of the side shown. */
struct FScctGearMenuState
{
	static constexpr int32 PathCapacity = FScctLoadout::PathCapacity;
	static constexpr int32 NameCapacity = FScctGearItem::NameCapacity;
	static constexpr int32 RuleCapacity = 256;
	static constexpr int32 Slots = FScctLoadout::Slots;

	/** 0 spy, 1 merc. */
	int32 Side;
	int32 GadgetSlots;
	int32 CurrentLevel;
	int32 Unrestricted;
	int32 SidearmsEnabled;
	int32 RiflesEnabled;
	int32 FromTeam;
	/** A draft of either side differs from the saved kit. */
	int32 Changed;
	/** The side shown is the one played in the running round: APPLY also asks the server for it. */
	int32 OwnSide;
	int32 Context;
	int32 QuickUse[Slots];
	TCHAR Gadgets[Slots][PathCapacity];
	/** The spy's is the Shocker he always carries. */
	TCHAR Weapon[PathCapacity];
	TCHAR Sidearm[PathCapacity];
	TCHAR GadgetNames[Slots][NameCapacity];
	TCHAR WeaponName[NameCapacity];
	TCHAR SidearmName[NameCapacity];
	/** When new gear goes on, as the picker says it. */
	TCHAR Rule[RuleCapacity];
};

/** One card of a catalogue. */
struct FScctGearOption
{
	static constexpr int32 SubCapacity = 48;

	TCHAR Path[FScctLoadout::PathCapacity];
	TCHAR Name[FScctGearItem::NameCapacity];
	/** The card's small line: REQUIRES LEVEL 10, 30 RDS / 600 RPM, QUICK USE... */
	TCHAR Sub[SubCapacity];
	int32 Flags;
	int32 RequiredLevel;
};

/** The intel panel of one piece of gear. */
struct FScctGearIntel
{
	static constexpr int32 NameCapacity = FScctGearItem::NameCapacity;
	static constexpr int32 DescriptionCapacity = 512;
	static constexpr int32 RestrictionCapacity = 96;
	static constexpr int32 NoteCapacity = 160;
	static constexpr int32 FileCapacity = 260;
	static constexpr int32 MaxStats = 10;
	static constexpr int32 StatCapacity = 32;

	TCHAR Name[NameCapacity];
	/** A rifle's role ("POWER / RECOIL"), else empty. */
	TCHAR Role[NameCapacity];
	TCHAR Description[DescriptionCapacity];
	/** Why it cannot be picked ("Requires level 10."), empty when it can. */
	TCHAR Restriction[RestrictionCapacity];
	/** A line under the stats (how the damage was measured). */
	TCHAR Note[NoteCapacity];
	/** Its picture in the converted content (a PNG), empty when there is none. */
	TCHAR IconFile[FileCapacity];
	/** The picture's tile in pixels; IconSize 0 is the whole image. */
	int32 IconU;
	int32 IconV;
	int32 IconSize;
	int32 StatCount;
	TCHAR StatCaption[MaxStats][StatCapacity];
	TCHAR StatValue[MaxStats][StatCapacity];
};

struct FScctGearMenuApi
{
	int32 Version;

	/** Builds both drafts from the saved kit. Side 0 spy, 1 merc, -1 the side played (else the spy). Returns 1 or 0. */
	int32 (__cdecl* Open)(int32 Side, int32 Context);

	/** Shows the other tab (the drafts are kept). */
	int32 (__cdecl* SetSide)(int32 Side);

	int32 (__cdecl* GetState)(FScctGearMenuState* State);

	/** A category's cards (EScctGearCategory; Slot for gadgets). Returns the full count; past Capacity is not written. */
	int32 (__cdecl* ListOptions)(int32 Category, int32 Slot, FScctGearOption* Options, int32 Capacity);

	/** Picks a card for the slot (a gadget another slot holds swaps over). Returns 1 taken, 0 refused. */
	int32 (__cdecl* Choose)(int32 Category, int32 Slot, const TCHAR* Path);

	/** The side shown goes back to its default kit (in the draft). */
	void (__cdecl* Reset)();

	int32 (__cdecl* GetIntel)(const TCHAR* Path, FScctGearIntel* Intel);

	/** Writes both drafts into the saved kit and its file. Returns 1, or 0 with the reason in LastError. */
	int32 (__cdecl* Apply)();

	/** A side's saved kit as one line by name (the lobby's summary). Returns the full length. */
	int32 (__cdecl* Summary)(int32 Side, TCHAR* Buffer, int32 Capacity);

	/** The saved kit as its file holds it. Returns the full length. */
	int32 (__cdecl* SavedJson)(TCHAR* Buffer, int32 Capacity);

	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};

// The sizes the C# side has (Bridge/Probes/GearMenuProbe prints them).
static_assert(sizeof(FScctGearMenuState) == 1912, "FScctGearMenuState must match BridgeGearMenuState");
static_assert(sizeof(FScctGearOption) == 328, "FScctGearOption must match BridgeGearOption");
static_assert(sizeof(FScctGearIntel) == 3544, "FScctGearIntel must match BridgeGearIntel");
static_assert(sizeof(FScctGearMenuApi) == 96, "FScctGearMenuApi must match GearMenuApi (64-bit)");

// Fetched like every other feature table (Runtime/ScctFeatures.h), declared here so the shared list stays as it is.
template <> struct TScctFeatureInfo<FScctGearMenuApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.GearMenuExports"); }
	static constexpr int32 Version = ScctGearMenuApiVersion;
};
