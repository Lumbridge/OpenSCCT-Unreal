#pragma once

#include "CoreMinimal.h"

class FScctVisionData;
class UMaterialInstanceDynamic;
class UWorld;
struct FScctVisionDebugApi;

/**
 * Motion tracking's picture, as the original draws it: everything is the blurred scene as a navy to red ramp scaled by
 * the level's MTVSlope and MTVOffset, and the scene is shown sharp, in pale green, only inside the clean view rectangles
 * of the local merc's motion tracker locks (a lock held long enough to be focused, in front of the camera and not
 * faded out). Nothing marks the pawns: one that moves is seen because the tracker locks it, one that stands still is
 * part of the ramp, and a spy fully cloaked by his camo suit is not drawn at all in this mode (as SimPawnView hides him).
 *
 * Each frame motion tracking is on it sets M_ScctVisionMotion's RampSlope (2 (1 + 3 MTVSlope / 255)), RampOffset
 * (MTVOffset / 255), CleanCount and Rect0..Rect7 (left, top, right, bottom in viewport UV): the bridge boxes the locks
 * from the player's camera in the 640 x 480 HUD frame (FScctVisionData::ReadMotionLocks), laid on the screen as the
 * original's tracker frame is (480 high, centred, at most 16:9 laid out and stretched wider). The HUD's own lock boxes
 * are drawn by the HUD, not here.
 *
 * The log says 'Vision: motion ramp slope/offset x/y, k clean rects' when that changes and every few seconds.
 *
 * Unattended switch: -ScctVisionBots="S;S..." at each S seconds into the match has the local server walk the nearest
 * spy bot back and forth across the merc player's view in front of him and hold the next one still beside it, for 20 s
 * (Hud/ScctVisionDebug.h), so a moving and a standing pawn can be shot in motion tracking unattended; the log says what
 * the server did and, with the summary line, where each pawn in view is and whether a clean rectangle holds it.
 */
class OPENSCCT_API FScctVisionMotion
{
public:
	FScctVisionMotion();

	/** Sets the motion material's ramp and clean rectangles for this frame; bShown false while motion tracking is not on. */
	void Update(UWorld* World, FScctVisionData& Data, UMaterialInstanceDynamic* Material, bool bShown, float DeltaSeconds);

	/** A spy fully cloaked by his camo suit, whom motion tracking does not draw. */
	static bool HidesPawn(float Cloak, bool bMerc) { return !bMerc && Cloak >= 0.999f; }

	/** Forgets the match (the bots switch's clock, the last log). */
	void Reset();

private:
	void RunDebugBots(float DeltaSeconds, bool bHasPawns);

	int32 LastCount = -1;
	int32 LastSlope = -1;
	int32 LastOffset = -1;
	double LastLog = 0.0;

	/** -ScctVisionBots: the seconds into the match to stand the bots, those still to come. */
	TArray<float> BotTimes;
	float MatchSeconds = 0.0f;
	const FScctVisionDebugApi* DebugApi = nullptr;
	bool bTriedDebugApi = false;
	int32 LastReport = 0;
};
