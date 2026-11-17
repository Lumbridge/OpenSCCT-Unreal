#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Runtime/ScctBridgeApi.h"
#include "ScctViewPawn.generated.h"

class UCameraComponent;

/**
 * What the local player possesses in Unreal: only a camera. The player's real body is a pawn of the
 * simulation; every frame the session puts this camera where the simulation's view calculation says.
 */
UCLASS()
class OPENSCCT_API AScctViewPawn : public APawn
{
	GENERATED_BODY()

public:
	AScctViewPawn();

	/**
	 * Moves the camera to the frame's view. The session calls this right after it has run the frame's
	 * simulation ticks and before the engine caches the camera, so the view is never a frame behind the world.
	 */
	void ApplyView(const FScctBridgeView& View);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;
};
