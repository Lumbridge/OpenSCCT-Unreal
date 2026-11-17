#include "Play/ScctViewPawn.h"

#include "Camera/CameraComponent.h"

AScctViewPawn::AScctViewPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->bUsePawnControlRotation = false;

	// The simulation's field of view is the horizontal angle of a 4:3 picture, and wider screens keep its
	// vertical angle (they see more at the sides). Unreal keeps the vertical angle it derives from this ratio.
	Camera->SetAspectRatio(4.0f / 3.0f);
	Camera->SetConstraintAspectRatio(false);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	SetCanBeDamaged(false);
}

void AScctViewPawn::ApplyView(const FScctBridgeView& View)
{
	if (View.Valid == 0)
	{
		return;
	}

	SetActorLocationAndRotation(FVector(View.X, View.Y, View.Z), FRotator(View.Pitch, View.Yaw, View.Roll));
	if (View.Fov > 1.0f)
	{
		Camera->SetFieldOfView(View.Fov);
	}
}
