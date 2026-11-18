#pragma once

#include "CoreMinimal.h"

/**
 * The one switch between the lighting the game was released with and the lighting plan's (light and dark as the
 * original has them: the baked light as the world's light, characters lit by the simulation's own pawn term, a fixed
 * exposure and a measured tone curve).
 *
 *   scct.Light.Plan 0    the picture as it was before the plan, exactly (the default)
 *   scct.Light.Plan 1    the plan's lighting
 *
 * Set it on the command line (-ScctLightPlan=1, or the engine's -dpcvars=scct.Light.Plan=1) or in the console. A
 * level is dressed for one of the two when it loads, so a change in the console shows at the next map load at the
 * latest; code that can follow a change at once reads Requested(), code that dresses a level reads Active().
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

	/** Active() is the plan's lighting, not the released one. */
	inline bool IsOn() { return Active() > 0; }
}
