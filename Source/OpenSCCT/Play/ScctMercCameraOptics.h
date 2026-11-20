#pragma once

#include "CoreMinimal.h"

struct FPostProcessSettings;

namespace ScctMercCameraOptics
{
	/** Switch the local pawn camera's lens. The return value indicates whether the camera has a working lens. */
	bool Apply(FPostProcessSettings& Settings, bool bActive);

	/** Compensate for the lens magnification within the merc's configured horizontal field-of-view limit. */
	float CompensateFov(float OriginalFov, float HorizontalFov, float HorizontalCap);

	/** Map a projected HUD anchor through the same lens as the scene. Both arguments use viewport pixels. */
	FVector2D WarpScreenPoint(FVector2D Screen, const FIntRect& Rect);
}
