#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctBridgeApi.h"

// The Armoury's preview table of the C# bridge (Bridge/OpenScct.UnrealBridge/Social/ArmouryPreviewExports.cs: what the
// Godot client's CosmeticPreview and CosmeticThumbs ask of the content): which model a slot is seen on, an item's
// pattern and flat picture, the content builder's camo texture of a pattern for a retail texture without a session,
// and the stock characters' skeleton with a standing idle pose. These mirror the bridge field for field; change both
// together and bump the version.
//
// Fetched like the other feature tables: ScctFeature<FScctArmouryPreviewApi>() (Runtime/ScctFeatures.h). Text is
// UTF-16, zero terminated. Game thread only.

constexpr int32 ScctArmouryPreviewApiVersion = 1;

/** OpenScct.Net.Cosmetics.CosmeticSlot: the two slots seen on the character (every other slot is a gun). */
namespace EScctCosmeticSlot
{
	constexpr int32 Body = 0;
	constexpr int32 Light = 1;
}

/** What SlotModel says a slot is seen on. */
namespace EScctArmouryView
{
	constexpr int32 Character = 0;
	constexpr int32 Head = 1;
	constexpr int32 Gun = 2;
}

struct FScctArmouryPreviewItem
{
	int32 Side;    // 0 spy, 1 merc
	int32 Slot;    // OpenScct.Net.Cosmetics.CosmeticSlot
	int32 Kind;    // 0 tint, 1 camo texture, 2 light hue
	int32 HasHue;  // 1 when HueTurn is set (a light item)
	float HueTurn; // radians from the side's retail glow colour to the item's
	float Hue;     // degrees
	int32 Reserved0;
	int32 Reserved1;
};
static_assert(sizeof(FScctArmouryPreviewItem) == 32, "FScctArmouryPreviewItem must match the bridge's ArmouryPreviewItem");

struct FScctArmouryPreviewApi
{
	int32 Version;
	/** An item by id: its record, its pattern ("woodland", "tint_5a6ea8"; empty for a light) and its flat picture's file (empty: none built). Returns 0 for an unknown id. */
	int32 (__cdecl* GetItem)(const TCHAR* Id, FScctArmouryPreviewItem* Item, TCHAR* Pattern, int32 PatternCapacity, TCHAR* Thumb, int32 ThumbCapacity);
	/** Writes the model a slot of a side is seen on ("SPerso.ATT_01", "SWeaponFirst.M160"). Returns an EScctArmouryView. */
	int32 (__cdecl* SlotModel)(int32 Side, int32 Slot, TCHAR* Buffer, int32 Capacity);
	/** Writes the full path of the camo texture of a pattern for a retail texture of a package folder ("SPerso"). Returns its length, 0 when none was built. */
	int32 (__cdecl* CosmeticTexture)(const TCHAR* Pattern, const TCHAR* Package, const TCHAR* Retail, TCHAR* OutPath, int32 Capacity);
	/** The stock character's bones (0 spy, 1 merc), in the order of GetIdlePose. Returns the bone count, 0 without the content. */
	int32 (__cdecl* GetSkeleton)(int32 Kind, FScctBridgeBoneInfo* Bones, int32 Capacity);
	/** The stock character standing idle at Time seconds (the clip loops). Returns the bone count, 0 without the content. */
	int32 (__cdecl* GetIdlePose)(int32 Kind, float Time, FScctBridgeBone* Bones, int32 Capacity);
};
