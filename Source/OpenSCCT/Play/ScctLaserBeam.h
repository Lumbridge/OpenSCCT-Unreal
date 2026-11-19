#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

namespace ScctLaserBeam
{
	inline constexpr TCHAR PlanePath[] = TEXT("/Engine/BasicShapes/Plane.Plane");
	inline constexpr TCHAR MaterialPath[] = TEXT("/Game/OpenSCCT/Weapons/Materials/M_ScctLaserBeam.M_ScctLaserBeam");

	/** Aligns the plane's X axis with the beam and faces its Z axis toward the eye. Width is in centimetres. */
	void Place(UStaticMeshComponent& Beam, const FVector& Start, const FVector& End, const FVector& Eye, double Width);
}
