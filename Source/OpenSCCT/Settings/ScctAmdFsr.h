#pragma once

#include "CoreMinimal.h"

namespace ScctAmdFsr
{
	bool IsUpscalingSupported();
	bool IsFrameGenerationHardwareSupported();
	bool IsFrameGenerationSupported();
	float GetScreenPercentage(int32 QualityMode);

	// AMD quality modes: 0 Native AA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance.
	// The vendor applies its recommended screen percentage when the mode or enable state changes.
	bool SetUpscaling(bool bEnabled, int32 QualityMode);
	bool SetFrameGeneration(bool bEnabled);
}
