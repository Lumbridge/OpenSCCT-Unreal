#pragma once

#include "CoreMinimal.h"

// Rain splashing on a pawn (SFlicFlocEmitter): what its script set into its three sub-emitters (the body splashes,
// the head drips after the rain, the puff), for the effects to spread over the drawn body as the original does.
// Mirrors Bridge/OpenScct.UnrealBridge/Events/FlicFlocExports.cs; change both together.

constexpr int32 ScctFlicFlocApiVersion = 1;

struct FScctFlicFloc
{
	int32 PawnActorId;     // FScctBridgePawn::ActorId of the pawn it sits on, 0 when none
	int32 PawnKind;        // 0 spy, 1 merc: which bone table the splashes use
	float SplashRate;      // sub-emitter 0 (body splashes): spawns a second
	int32 SplashRespawn;   // RespawnDeadParticles
	int32 SplashBudget;    // VariableMaxParticles (while not 0, the most the rate may still spawn)
	float DripRate;        // sub-emitter 1 (head drips after the rain)
	int32 DripRespawn;
	int32 DripBudget;
	int32 SplashStarts;    // restarts of sub-emitter 0 (count changes)
	int32 DripStarts;      // restarts of sub-emitter 1
	int32 PuffTriggers;    // triggers of sub-emitter 2
	float PawnVelX, PawnVelY, PawnVelZ; // the pawn's velocity, cm a second
};

static_assert(sizeof(FScctFlicFloc) == 56, "FScctFlicFloc must match BridgeFlicFloc");

struct FScctFlicFlocApi
{
	int32 Version;

	/** The script state of the SFlicFlocEmitter with this effect actor id; 1, or 0 when it is not one. */
	int32 (__cdecl* GetFlicFloc)(int32 ActorId, FScctFlicFloc* OutState);

	/**
	 * The check of -ScctLevelDo "rain": stands every living pawn of the local match's server in the map's rain volume
	 * with the widest floor. 1 when queued, 0 without a local server. Nothing in normal play calls it.
	 */
	int32 (__cdecl* StandInRain)();
};
