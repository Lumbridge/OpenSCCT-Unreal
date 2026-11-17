#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The teammate names' interface of the C# bridge (the Godot client's HudTeammateNames without its drawing): which of the
// local player's teammates get a label, with name, health and the mercs' sleeping and backpack marks. It mirrors
// Bridge/OpenScct.UnrealBridge/Hud/TeammateNamesExports.cs; change both together and bump the version. Fetch it with
// ScctFeature<FScctTeammateNamesApi>() like any other feature table.

constexpr int32 ScctTeammateNamesApiVersion = 1;

namespace EScctTeammateNameFlags
{
	constexpr int32 Sleeping = 1;         // knocked out (replicates with the pawn)
	constexpr int32 Backpack = 2;         // carries a backpack
	constexpr int32 BackpackSupplies = 4; // the backpack still holds charges of the local player's own share
	constexpr int32 Merc = 8;             // a mercenary: the sleeping and backpack marks are the mercs' only
}

/** One teammate a label goes over (GetTeammates). */
struct FScctTeammateNameRow
{
	int32 ActorId;          // the pawn's id in the bridge's pawn list (FScctBridgePawn::ActorId)
	int32 NameId;           // the player's name as a HUD text id (FScctHudApi::GetText)
	int32 Health;           // as replicated from the authority
	int32 Flags;            // EScctTeammateNameFlags
	float X, Y, Z;          // at the last tick (the drawn pawn's interpolated place is preferred when it is known)
	float CollisionHeight;
};

struct FScctTeammateNamesApi
{
	int32 Version;
	// The local player's living teammates of his own pawn type with a player name (not himself, not a spy clone);
	// returns the count written. 0 while he is dead, spectating, the game has ended or in a Merc Deathmatch.
	int32 (__cdecl* GetTeammates)(FScctTeammateNameRow* Rows, int32 Capacity);
};

static_assert(sizeof(FScctTeammateNameRow) == 32, "FScctTeammateNameRow must mirror TeammateNameRow");
static_assert(sizeof(FScctTeammateNamesApi) == 16, "FScctTeammateNamesApi must mirror TeammateNamesApi");

template <>
struct TScctFeatureInfo<FScctTeammateNamesApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.TeammateNamesExports"); }
	static constexpr int32 Version = ScctTeammateNamesApiVersion;
};
