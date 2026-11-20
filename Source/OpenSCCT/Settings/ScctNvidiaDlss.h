#pragma once

#include "CoreMinimal.h"

namespace ScctNvidiaDlss
{
	enum class ESuperResolutionPreset : uint8 { Quality, Balanced, Performance, UltraPerformance, DLAA };

	bool IsSuperResolutionSupported();
	bool IsFrameGenerationSupported();
	bool SupportsSuperResolutionPreset(ESuperResolutionPreset Preset);
	// The vendor capability query also runs when another provider owns this process's swapchain.
	bool SupportsFrameGenerationHardwareMode(int32 Multiplier);
	bool SupportsFrameGenerationMode(int32 Multiplier);

	// On success, the caller applies OutScreenPercentage alongside its temporal AA and dynamic-resolution settings.
	// Disabling DLSS leaves screen percentage ownership with the ordinary graphics settings.
	bool SetSuperResolution(bool bEnabled, ESuperResolutionPreset Preset, FIntPoint OutputResolution, float& OutScreenPercentage);
	bool SetFrameGeneration(int32 Multiplier); // 0 disables generation. 2–6 denote presented frames per rendered frame.
	bool GetFrameTiming(float& OutPresentedHz, int32& OutPresentedFrames);
}
