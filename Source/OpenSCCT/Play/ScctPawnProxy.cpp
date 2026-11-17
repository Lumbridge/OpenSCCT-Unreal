#include "Play/ScctPawnProxy.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AScctPawnProxy::AScctPawnProxy()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	// The simulation does all collision; Unreal's physics must not see these.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Body->SetStaticMesh(Cylinder.Object);
	}
}

void AScctPawnProxy::Present(const FScctBridgePawn& Pawn, bool bHideBody)
{
	SetActorLocationAndRotation(FVector(Pawn.X, Pawn.Y, Pawn.Z), FRotator(0.0, Pawn.Yaw, 0.0));

	// The engine cylinder is 100 across and 100 tall; the pawn's cylinder is given as radius and half height.
	SetActorScale3D(FVector(Pawn.CollisionRadius / 50.0, Pawn.CollisionRadius / 50.0, Pawn.CollisionHeight / 50.0));

	const bool bVisible = (Pawn.Flags & EScctPawnFlags::Visible) != 0 && !bHideBody;
	SetActorHiddenInGame(!bVisible);
}
