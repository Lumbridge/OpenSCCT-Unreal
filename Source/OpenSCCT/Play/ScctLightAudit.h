#pragma once

#include "CoreMinimal.h"

struct FScctLightLevel;

/**
 * The lighting plan's audit (Play/ScctLightPlan.h): what the simulation says about the light on every living pawn,
 * written to the log while a match plays, so the picture can be held against it.
 *
 *   scct.Light.Audit 1          one block a second: a SCCT_AUDIT line (the map, the merc's squint, the own spy's shadow
 *                               darkness) and a SCCT_AUDIT_PAWN line per living pawn (its level by the simulation's
 *                               pawn light term, the level on the side facing the camera, how many lights light it and
 *                               how many of those have a blocked line). The first block of a map also says how the
 *                               plan classes the map's lights (SCCT_AUDIT_MAP).
 *   scct.Light.AuditSeconds     the time between blocks (default 1).
 *   scct.Light.StickyCamShadow  1: under scct.Light.Plan 1 the sticky camera's fly-in follows the original's shadow
 *                               rule (it is solved twice a tick only while the own spy's shadow is drawn, so the
 *                               camera flies in at half the speed where the spy stands in the dark). 0 (the default):
 *                               always twice, as the game was released.
 *
 * It reads the bridge's FScctLightPlanApi and changes nothing in the picture. The picture's side of the audit (what
 * the body's material was given) is added with the body lighting; until then the lines are the simulation's half.
 */
namespace FScctLightAudit
{
	/** Starts the audit's ticker; once, from the module's startup. */
	void Start();

	void Stop();

	/** One log line saying how the plan classes the map's lights (also used by the light parity tool). */
	OPENSCCT_API FString DescribeLevel(const FScctLightLevel& Level);
}
