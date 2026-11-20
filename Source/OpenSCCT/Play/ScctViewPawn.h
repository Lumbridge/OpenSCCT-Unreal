#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Play/ScctMercCameraMotion.h"
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
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void EndViewTarget(APlayerController* PC) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Moves the camera to the frame's view. The session calls this right after it has run the frame's
	 * simulation ticks and before the engine caches the camera, so the view is never a frame behind the world.
	 */
	void ApplyView(const FScctBridgeView& View);

	bool UsesMercCamera() const { return bMercCameraActive; }
	bool HasMercCameraLens() const { return bMercLensActive; }

	/** Moves a point on the original aim canvas to its pixel location on the displayed camera. */
	FVector2f ProjectMercAim(FVector2f AimPoint, FVector2f CanvasSize) const;

private:
	void SetMercCameraActive(bool bEnabled);
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	FScctMercCameraMotion MercCameraMotion;
	int32 MercCameraSession = 0;
	bool bMercCameraActive = false;
	bool bMercLensActive = false;

};
