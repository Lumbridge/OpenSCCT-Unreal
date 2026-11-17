#include "Play/ScctViewPawn.h"

#include "Camera/CameraComponent.h"
#include "Play/ScctSessionSubsystem.h"

AScctViewPawn::AScctViewPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the session subsystem has run the frame's simulation ticks.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->bUsePawnControlRotation = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	SetCanBeDamaged(false);
}

void AScctViewPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	if (Session == nullptr || Session->GetView().Valid == 0)
	{
		return;
	}

	const FScctBridgeView& View = Session->GetView();
	SetActorLocationAndRotation(FVector(View.X, View.Y, View.Z), FRotator(View.Pitch, View.Yaw, View.Roll));
	if (View.Fov > 1.0f)
	{
		Camera->SetFieldOfView(View.Fov);
	}
}
