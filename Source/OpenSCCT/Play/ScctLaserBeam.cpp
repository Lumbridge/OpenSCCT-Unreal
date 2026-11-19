#include "Play/ScctLaserBeam.h"

#include "Components/StaticMeshComponent.h"

void ScctLaserBeam::Place(UStaticMeshComponent& Beam, const FVector& Start, const FVector& End, const FVector& Eye, double Width,
	bool bAttachedMuzzle)
{
	const FVector Along = End - Start;
	const double Length = Along.Size();
	if (Length <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector Direction = Along / Length;
	const FVector Normal = FVector::VectorPlaneProject(Eye - Start, Direction).GetSafeNormal();
	const FQuat Rotation = Normal.IsNearlyZero()
		? FRotationMatrix::MakeFromX(Direction).ToQuat()
		: FRotationMatrix::MakeFromXZ(Direction, Normal).ToQuat();
	// The engine's plane spans −50 to +50 cm on X and Y. Its centerline runs exactly from Start to End.
	Beam.SetWorldTransform(FTransform(Rotation, Start + Along * 0.5, FVector(Length / 100.0, Width / 100.0, 1.0)));
	// M_ScctLaserBeam reads the length for its muzzle and tip fades.
	Beam.SetCustomPrimitiveDataFloat(0, static_cast<float>(Length));
	Beam.SetCustomPrimitiveDataFloat(1, bAttachedMuzzle ? 1.0f : 0.0f);
}
