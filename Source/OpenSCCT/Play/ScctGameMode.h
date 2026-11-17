#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ScctGameMode.generated.h"

/**
 * The game mode of a playable OpenSCCT level. It starts the OpenSCCT match for the level; the rules themselves
 * run in the simulation. URL options: ?ScctMap=AquaE (default: the level's name), ?Side=spy|merc, ?Bots=3,
 * ?Mode=Story.
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
	FString ScctMap;
	FString ScctMode = TEXT("Story");
	bool bMerc = false;
	int32 Bots = 3;
};
