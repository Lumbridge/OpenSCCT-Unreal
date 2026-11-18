#pragma once

#include "CoreMinimal.h"

class FScctHudModel;
class FScctHudPainter;

/**
 * What the merc visor effects remember from one frame to the next (MercHudPage's latches in the Godot client). The
 * screen effects subsystem owns one, runs ScctVisorEffects::Update on it once a frame and points the model at it, so
 * the painters can read it.
 */
struct FScctVisorState
{
	// The EMP shutdown (spec/hud/screen-effects.md rule 12).
	int32 ShutDowns = INDEX_NONE; // the last count seen; the first value is only remembered
	bool bLatch = false;          // a shutdown or its reboot runs
	bool bRebooting = false;
	float T0 = 0.0f;              // whole second the shutdown began
	float T1 = 0.0f;              // whole second the reboot began
	float StartedAt = 0.0f;       // level time the shutdown and the reboot were noticed (their drawings run from these)
	float RebootAt = 0.0f;

	// The foam (rule 15): each splash gets its own blobs.
	bool bFoamSplash = false;
	uint32 FoamSeed = 0;
	float FoamSplashAt = -1.0f;

	// The droplets (rule 16): when the pawn came into the rain and when it left it.
	bool bWet = false;
	float WetSince = -1.0f;
	float DryAt = -1.0f;

	// The sticky shocker (rule 13).
	bool bShocked = false;
	float ShockSince = 0.0f;

	// The gas mask (Hud/ScctGasMaskOverlay.h): its frame is up while the mask is the selected gadget, the breathing
	// runs while it is worn; level times each began.
	bool bMaskShown = false;
	float MaskShownAt = 0.0f;
	bool bMaskWorn = false;
	float MaskWornAt = 0.0f;

	// The poison (Hud/ScctPoisonOverlay.h): its two layers are up while the pawn is poisoned and the health gauge shows;
	// level time they came up (their 0.9 s loop runs from it).
	bool bPoisonShown = false;
	float PoisonShownAt = 0.0f;

	// -ScctVisorFx: which effect is faked and from when.
	FString Forced;
	float ForceDelay = 2.0f;
	float ForcedClock = -1.0f; // seconds since the HUD first had a state, negative before
	float ForcedAt = -1.0f;    // level time the faked effect started
	int32 ForcedShutDowns = 0;
	bool bForcedFoamOn = false;
};

/**
 * The merc's visor effects, drawn over the picture as the merc page's effect areas are: the EMP shutdown and its
 * reboot, the perturbation static of a hit or a jammer, the extinguisher foam, the sticky shocker's arcs and the rain
 * droplets (Godot game/Scripts/Hud/MercHudPage.cs DriveShutDown, DrivePerturbation, DriveFoam 'Mousse',
 * DriveDroplets 'gouttes', DriveStickyShock).
 *
 * The original plays Magma animations for these, which the Unreal client cannot play, so each is a procedural
 * drawing with the original's timing and coverage: scanline static, foam blobs seeded per splash, droplets that run
 * down with a trail, electric arcs. They are driven only by the snapshot: EffectFlags (Perturbed, ShutDown, FoamSplash,
 * FoamOn, StickyShocking, StickyShockHead, Wet), ShutDowns, FoamStopTime, FoamStaySeconds and TimeSeconds.
 *
 * The gas mask's frame and breathing are painted with them, under the others (Hud/ScctGasMaskOverlay.h). The poison's
 * layers keep their latch here too, but belong to both sides and are painted on their own (Hud/ScctPoisonOverlay.h).
 *
 * -ScctVisorFx=shutdown|perturb|foam|shock|wet|mask|poison[@seconds] fakes that effect in the snapshot a few seconds (2 by
 * default) after the HUD opens, as the bridge would set it, so each can be shot unattended. Log lines start with "Visor:".
 */
namespace ScctVisorEffects
{
	/** Seconds the merc HUD stays down after an EMP (merc shutdown handler 0x10b2c430). */
	constexpr float ShutDownSeconds = 12.0f;

	/** Seconds the reboot runs after that. */
	constexpr float RebootSeconds = 2.0f;

	/** Seconds the foam takes to clear once its stay is over (SetHudProjection(1, 5, 10): 10 x 10 steps of 0.1 s). */
	constexpr float FoamCleanSeconds = 10.0f;

	/** Reads -ScctVisorFx into the state. */
	void ParseCommandLine(FScctVisorState& Visor);

	/** Fakes the forced effect into the snapshot, then moves the latches on for this frame. */
	void Update(FScctVisorState& Visor, FScctHudModel& Model, float DeltaSeconds);

	/** Forgets every latch (a new match). The forced effect stays asked for. */
	void Reset(FScctVisorState& Visor);

	/** Paints the visor effects of the local merc (nothing for a spy). */
	void Paint(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** True during the 12 s an EMP keeps the merc HUD down: the player's own HUD blocks are not drawn. */
	bool IsHudShutDown(const FScctHudModel& Model);
}
