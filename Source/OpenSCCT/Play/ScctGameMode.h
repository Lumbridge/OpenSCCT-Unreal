#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Play/ScctSessionSubsystem.h"
#include "ScctGameMode.generated.h"

/**
 * The game mode of a playable OpenSCCT level. It starts the OpenSCCT match for the level; the rules themselves
 * run in the simulation. URL options, all optional:
 *   ?ScctMap=AquaE      the OpenSCCT map (default: the level's name)
 *   ?Mode=Story         Story, DiskHunt, Deathmatch or MercDeathMatch
 *   ?Side=spy|merc      the local player's side (default spy)
 *   ?Bots=3             how many bots fill the match
 *   ?Difficulty=hard    the bots' skill: easy, normal, hard or expert (or 1 to 4); default normal
 *   ?Sidearms=1         the mercs carry OpenSCCT's sidearms
 *   ?Rifles=1           the mercs may pick OpenSCCT's additional rifles
 *   ?Practice=1         practice rules: no time limit, unlimited lives, 5 s respawn
 *   ?Name=Sam           the local player's name (Unreal's own player-name option; default "Player")
 * A switch counts as on when it is given without a value or with anything but 0, false, no or off.
 */
UCLASS()
class OPENSCCT_API AScctGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AScctGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;

private:
	FScctLocalMatchOptions Match;
};
