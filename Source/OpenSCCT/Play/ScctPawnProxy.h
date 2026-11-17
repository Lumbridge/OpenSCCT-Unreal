#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Runtime/ScctBridgeApi.h"
#include "ScctPawnProxy.generated.h"

class UStaticMeshComponent;

/**
 * The Unreal body of one simulated pawn. The simulation owns where it is and what it does; this actor only
 * draws it. For now it is a stand-in shape sized to the pawn's collision cylinder; the character model and the
 * simulation's animation channels replace it.
 */
UCLASS()
class OPENSCCT_API AScctPawnProxy : public AActor
{
	GENERATED_BODY()

public:
	AScctPawnProxy();

	/** Places the body for this frame. bHideBody is set for the local player seen through their own eyes. */
	void Present(const FScctBridgePawn& Pawn, bool bHideBody);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;
};
