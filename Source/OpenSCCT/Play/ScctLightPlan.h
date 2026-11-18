#pragma once

#include "CoreMinimal.h"

/**
 * The one switch between the lighting the game was released with and the lighting plan's (light and dark as the
 * original has them: the baked light as the world's light, characters lit by the simulation's own pawn term, a fixed
 * exposure and a measured tone curve).
 *
 *   scct.Light.Plan 1    fixed exposure and baked map lighting (default)
 *   scct.Light.Plan 0    legacy lighting
 *
 * Launch with -ScctLightPlan=0 or the engine's -dpcvars=scct.Light.Plan=0 to use legacy lighting. A level reads
 * Active() when it loads, so reload the map after changing the console variable. Settings that can follow a
 * change immediately read Requested().
 *
 * Everything the plan changes asks this; nothing else decides between the two pictures.
 */
namespace FScctLightPlan
{
	/** Reads -ScctLightPlan= and starts latching the value at each map load; once, from the module's startup. */
	void Start();

	/** What scct.Light.Plan says now. */
	OPENSCCT_API int32 Requested();

	/** What scct.Light.Plan said when the level being played began to load: the lighting this level is dressed for. */
	OPENSCCT_API int32 Active();

	/** Whether the current level uses fixed lighting. */
	inline bool IsOn() { return Active() > 0; }
}
