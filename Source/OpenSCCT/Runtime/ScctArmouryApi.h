#pragma once

#include "CoreMinimal.h"

// The Armoury's table of the C# bridge (Bridge/OpenScct.UnrealBridge/Social/ArmouryExports.cs and Appearance.cs: the
// Godot client's ArmouryScreen model and AccountService.Appearance): the cosmetics catalogue with the account's
// unlocks, the chosen look per side, and the appearance sync with the account. These mirror the bridge field for
// field; change both together and bump the version.
//
// Fetched like the other feature tables: ScctFeature<FScctArmouryApi>() (Runtime/ScctFeatures.h). Text is UTF-16,
// zero terminated. Game thread only.

constexpr int32 ScctArmouryApiVersion = 1;

namespace EScctArmouryFlags
{
	constexpr int32 HasMaster = 1;
	constexpr int32 AccountKnown = 2;
	constexpr int32 Managed = 4;   // the master syncs the appearance with revision-checked writes
	constexpr int32 Conflict = 8;  // the account's appearance changed elsewhere while this PC has unsent choices
	constexpr int32 HideMine = 16;
}

struct FScctArmouryInfo
{
	int32 Revision;    // the account service's: goes up whenever anything here changed
	int32 Flags;       // EScctArmouryFlags
	int32 Items;       // the catalogue's size
	int32 TitleSerial; // goes up each time the account's title was taken (EScctArmouryText::Title is then stored)
	int32 Reserved0;
	int32 Reserved1;
};
static_assert(sizeof(FScctArmouryInfo) == 24, "FScctArmouryInfo must match the bridge's ArmouryInfoBlock");

namespace EScctArmouryItemFlags
{
	constexpr int32 Unlocked = 1;
	constexpr int32 Chosen = 2;
}

struct FScctArmouryItem
{
	int32 Side;    // 0 spy, 1 merc
	int32 Slot;    // OpenScct.Net.Cosmetics.CosmeticSlot
	int32 Kind;    // 0 tint, 1 camo texture, 2 light hue
	int32 Rgb;     // the swatch's colour, 0xRRGGBB
	int32 Rgb2;    // a camo's darkest colour (else Rgb)
	int32 Flags;   // EScctArmouryItemFlags
	int32 Current; // the unlocking achievement's progress (Target 0: none known)
	int32 Target;
};
static_assert(sizeof(FScctArmouryItem) == 32, "FScctArmouryItem must match the bridge's ArmouryItemBlock");

enum class EScctArmouryText : int32
{
	Status = 0,     // the appearance sync's line
	Chosen = 1,     // the chosen ids of side A, joined by commas
	SlotName = 2,   // the name of slot B of side A
	Slots = 3,      // the slots of side A in the page's order, numbers joined by commas
	SharedLook = 4, // the look of side A sent to servers (hex)
	Title = 5,      // the account's title to store
	OwnLook = 6,    // every chosen id of both sides joined by "+"
};

enum class EScctArmouryCommand : int32
{
	Reload = 0,   // take the account's appearance (resolves a conflict)
	Reapply = 1,  // send this PC's choices over the account's (resolves a conflict)
	Refresh = 2,  // ask the master for the account (its unlocks)
	HideMine = 3, // "Hide my cosmetics" (Value 0 or 1)
};

struct FScctArmouryApi
{
	int32 Version;
	int32 (__cdecl* GetInfo)(FScctArmouryInfo* Info);
	/** The catalogue's item at Index with its id, name and tooltip. Returns 0 past the end. */
	int32 (__cdecl* GetItem)(int32 Index, FScctArmouryItem* Item, TCHAR* Id, int32 IdCapacity, TCHAR* Name, int32 NameCapacity, TCHAR* Tip, int32 TipCapacity);
	/** Returns the text's full length. */
	int32 (__cdecl* GetText)(int32 Kind, int32 A, int32 B, TCHAR* Buffer, int32 Capacity);
	/** The saved choice of a side at start (ids joined by commas). */
	void (__cdecl* SetChosen)(int32 Side, const TCHAR* Ids);
	/** Puts an item (null or empty: retail) on a slot of a side. Returns 1 when taken; 0 for a locked item unless Force. */
	int32 (__cdecl* Choose)(int32 Side, int32 Slot, const TCHAR* Id, int32 Force);
	void (__cdecl* Command)(int32 Command, int32 Value);
};
