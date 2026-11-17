#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The merc backpack panels' interface of the C# bridge, beside the HUD's own table (Runtime/ScctHudApi.h): the merc's
// own share and his two mates' shares while he holds the backpack, or the share a mate carries for him while the refill
// entry is focused, each row with its gadget's icon (a material of the content's Options package), as the Godot
// client's BackpackDriver reads them; and unattended checks' ways to give and select the backpack and to set up a
// refill. It mirrors Bridge/OpenScct.UnrealBridge/Hud/HudBackpackExports.cs; change both together and bump the
// version. Fetch it with ScctFeature<FScctHudBackpackApi>() and call it once a frame after FScctBridgeApi::Update.

constexpr int32 ScctHudBackpackApiVersion = 1;

/** Which panels show (GetBackpack). */
struct FScctHudBackpackState
{
	int32 Mode;          // 0 hidden, 1 the merc holds his backpack, 2 the refill entry is focused (the mate's share)
	int32 Rows;          // the team's gadget slots clamped to 1..4: the rows each panel shows
	int32 Panels;        // bit 0 the own panel (in mode 2 the share), bit 1 mate A, bit 2 mate B
	int32 SelfLabelId;   // HUD text ids: the content's "You" and "Teammate" panel titles
	int32 MateLabelId;
	int32 MateANameId;   // HUD text ids of mate A's and mate B's player names (mode 2: the mate carrying the share)
	int32 MateBNameId;
	int32 RowCount;      // rows written
};

/** One gadget row of a panel. */
struct FScctHudBackpackRow
{
	int32 Panel;         // 0 the own panel or the share, 1 mate A, 2 mate B
	int32 Row;           // 0..3: the original's Ico1..Ico4 (top, left, right, bottom)
	int32 GadgetId;      // SGadget.GetUniqueIDByClass, 0 for an empty slot
	int32 Charges;       // below 0 not filled (grey), 0 empty (red)
	int32 Max;           // what a full share holds for that gadget, -1 unknown
	int32 U0, V0, U1, V1; // the icon's texel region of the texture below
	TCHAR Package[16];   // the icon's texture, zero ended, as UScctHudTextures::Find takes it
	TCHAR Texture[32];
};

struct FScctHudBackpackApi
{
	int32 Version;
	// Fills the state and up to Capacity rows after the last tick; returns the rows written.
	int32 (__cdecl* GetBackpack)(FScctHudBackpackState* State, FScctHudBackpackRow* Rows, int32 Capacity);
	// Unattended checks only: selects the local merc's backpack with the gadget keys; 1 when it is selected.
	int32 (__cdecl* SelectBackpack)();
	// Unattended checks only (local match): for Seconds the mate carrying the player's share stands in front of him,
	// facing him, one of his charges is spent and the refill entry is kept focused; 1 when queued.
	int32 (__cdecl* ForceRefill)(float Seconds);
	// Unattended checks only (local match): the local merc (non-zero: every merc of his side) gets the backpack in his
	// last gadget slot, as a gear change would; 1 when queued.
	int32 (__cdecl* GiveBackpacks)(int32 AllMercs);
};

// The sizes the C# side has (HudProbe prints them).
static_assert(sizeof(FScctHudBackpackState) == 32, "FScctHudBackpackState must mirror HudBackpackState");
static_assert(sizeof(FScctHudBackpackRow) == 132, "FScctHudBackpackRow must mirror HudBackpackRow");
static_assert(sizeof(FScctHudBackpackApi) == 40, "FScctHudBackpackApi must mirror HudBackpackApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctHudBackpackApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudBackpackExports"); }
	static constexpr int32 Version = ScctHudBackpackApiVersion;
};
