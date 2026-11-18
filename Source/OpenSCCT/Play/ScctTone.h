#pragma once

#include "CoreMinimal.h"

class APostProcessVolume;
class UWorld;

/**
 * The lighting plan's tone curve (scct.Light.Plan 1, Play/ScctLightPlan.h): what display value the picture shows for
 * a given scene light, the same on every map.
 *
 * The original has no film curve: a surface's light term times its texture is the display value, clipped at white,
 * and its composite draws near-black as black (the glow gate). The engine's film curve at the plan's fixed exposure
 * was measured with the light parity tool's ramp (grey patches of known original display values read in the finished
 * picture): the original's 24 showed 3, 48 showed 23, 128 showed 140, 255 showed 221. Under the plan the engine's curve
 * is switched off and one material before the tone pass (Content/OpenSCCT/Materials/M_ScctTone, built by
 * Tools/Materials/build_tone_material.py, which says what the shader does step by step) is the whole transfer:
 *
 *   original 0..7     black (the original's gate; scct.Light.ToneGate 0 takes the gate out)
 *   original 24..200  the same display value (the gate is fully open from 24)
 *   above 200         a shoulder towards white for the overbright range the original clipped (scct.Light.ToneKnee)
 *
 * While goggles are on the gate is out (the original's vision passes read the scene before the composite that gates).
 * With the curve come the picture's other display rules: the bloom keeps only what is brighter than the map's
 * LevelInfo.GlowThreshold (the original's glow bright pass; the released picture blooms everything), lens flares are
 * off (their ghosts put light on black parts of the picture), so is the vignette (a display value is the same anywhere
 * on the screen) and the brightness setting is held to a display gamma of 2.0 to 2.4.
 *
 * Tools/Light/solve_tone.py reads a ramp the parity tool wrote and says whether the curve is met and, where it is not,
 * the scct.Light.ToneGain and scct.Light.ToneGamma that would meet it.
 */
namespace FScctTone
{
	/**
	 * Called every frame by the exposure's ticker (Play/ScctExposure.cpp) with the game's own post-process volume of
	 * the map world: sets the curve's settings and material on it while bPlan, and nothing at all otherwise.
	 */
	void Apply(UWorld* World, APostProcessVolume* Volume, bool bPlan, float ExposureScale);

	/** No map is played: gives back what Apply held outside the volume (the display gamma). */
	void Leave();

	/** The plan's curve is in the picture of the map being played (the plan is on, scct.Light.Tone is, the material was found). */
	OPENSCCT_API bool IsOn();

	/** The least and the most display gamma the brightness setting may ask for: 2.0 to 2.4 while scct.Light.Plan is on, 1.6 to 3.0 otherwise. */
	OPENSCCT_API float GammaMin();
	OPENSCCT_API float GammaMax();

	/** The display gamma to draw with for the setting's value: held to GammaMin..GammaMax while the plan is on, the value itself otherwise. */
	OPENSCCT_API float DisplayGamma(float Setting);
}
