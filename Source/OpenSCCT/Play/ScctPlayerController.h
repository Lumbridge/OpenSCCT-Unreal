#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ScctPlayerController.generated.h"

/**
 * Reads the keyboard and mouse and hands them to the simulation as its own buttons, axes and exec commands.
 * Unreal moves nothing itself: the simulation turns the input into movement.
 */
UCLASS()
class OPENSCCT_API AScctPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AScctPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** Mouse counts the simulation gets per unit of Unreal mouse movement. */
	UPROPERTY(EditAnywhere, Category = "OpenSCCT")
	float MouseCountsPerUnit = 1.0f;
};
