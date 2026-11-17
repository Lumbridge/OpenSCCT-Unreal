#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ScctViewPawn.generated.h"

class UCameraComponent;

/**
 * What the local player possesses in Unreal: only a camera. The player's real body is a pawn of the
 * simulation; every frame this camera goes where the simulation's view calculation puts it.
 */
UCLASS()
class OPENSCCT_API AScctViewPawn : public APawn
{
	GENERATED_BODY()

public:
	AScctViewPawn();

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;
};
