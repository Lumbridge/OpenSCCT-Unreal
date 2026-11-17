#pragma once

#include "CoreMinimal.h"

// The player's own entry points that belong to no other feature. Mirrors Bridge/OpenScct.UnrealBridge/PlayExports.cs
// field for field; change both together and bump the version. Every text is UTF-16, zero terminated, cut to fit;
// the entries that write a text return its full length.
//
// Asking for the table (ScctFeature<FScctPlayApi>()) also puts the local player's gear on every level the bridge
// loads from then on, so it is asked for before a match starts or a server is joined.

constexpr int32 ScctPlayApiVersion = 1;

/** FScctPlayApi::SetSidearm: OpenSCCT's sidearms, in the simulation's own numbering. */
enum class EScctSidearm : int32
{
	None = -1,
	Suppressed = 0,
	Automatic = 1,
	Magnum44 = 2,
	DesertEagle = 3,
	Unsuppressed = 4,
};

struct FScctPlayApi
{
	int32 Version;

	/** The sidearm the local merc brings where the host allows sidearms (EScctSidearm). Read when a kit is handed out. */
	void (__cdecl* SetSidearm)(int32 Kind);

	/** Sends a typed chat line to everyone, or to the team. The server relays it back into the HUD's messages. Returns 1 when sent. */
	int32 (__cdecl* SendChat)(const TCHAR* Text, int32 bTeamOnly);

	/** One of the side's tips with the player's keys in it. Pick chooses it (wrapped); negative: a random one. Works without a session. */
	int32 (__cdecl* GetTip)(int32 bMerc, int32 Pick, TCHAR* Buffer, int32 Capacity);

	/** Replaces the key tokens of a game text ("\USE") by the side's keys. */
	int32 (__cdecl* ExpandKeys)(int32 bMerc, const TCHAR* Text, TCHAR* Buffer, int32 Capacity);

	/** The name every page shows for a map ("AquaE" gives the catalog's name), or the id itself. Works without a session. */
	int32 (__cdecl* GetMapTitle)(const TCHAR* Map, TCHAR* Buffer, int32 Capacity);

	/** The world table's name of the followed map actor with this simulation ActorId; 0 when it is not one. */
	int32 (__cdecl* GetActorName)(int32 ActorId, TCHAR* Buffer, int32 Capacity);

	/** The full path of the map's loading picture for the side (a .png of the player's content); 0 when it has none. Works without a session. */
	int32 (__cdecl* GetLoadingImage)(const TCHAR* Map, int32 bMerc, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctPlayApi) == 8 + 7 * sizeof(void*), "FScctPlayApi must match PlayApi (64-bit)");
