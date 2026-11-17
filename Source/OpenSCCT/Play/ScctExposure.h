#pragma once

#include "CoreMinimal.h"

/**
 * How bright the player's picture is exposed right now, for game code that makes light.
 *
 * The levels are played with automatic exposure over a wide range: the eye opens up in a dark cellar and closes
 * down in a lit hall. A flash of a fixed strength is then a faint flicker in the hall and a white-out of the whole
 * screen in the cellar. Lights that game code flashes (muzzle flashes, sparks, explosions) are therefore scaled
 * by the exposure of the last drawn frame, so a flash is as bright in the picture wherever it goes off. The
 * glowing materials do the same in their shaders (the engine's eye adaptation node, Tools/Import/scct_materials.py).
 *
 * The value is read on the render thread (a scene view extension) and handed over through an atomic.
 */
namespace FScctExposure
{
	/** Starts reading the exposure; safe to call again. Needs the engine (call it from a world subsystem). */
	OPENSCCT_API void Start();

	/** Stops reading it (module shutdown). */
	OPENSCCT_API void Stop();

	/** The exposure scale of the last game view drawn (scene luminance times this is the picture); 1 until a frame was drawn. */
	OPENSCCT_API float Get();

	/**
	 * How bright the last picture was as a whole, before the tone curve: the scene's average luminance (the
	 * engine's exposure histogram over the whole picture, its brightest and darkest tenths left out) times the
	 * exposure. 0.18 is a picture the eye has settled on; less is one too dark for the exposure's range (a cellar
	 * with the lamps shot out), more one too bright. 0 until a frame was measured.
	 */
	OPENSCCT_API float PictureLevel();

	/**
	 * What to multiply a flash light's strength by so it reads the same at any exposure. The strengths in the code
	 * are those of a moderately lit room (ReferenceExposure); bounded both ways so a wrong reading cannot blind
	 * or hide a flash.
	 */
	OPENSCCT_API float FlashScale();

	/**
	 * Candelas of a rifle's muzzle flash in a moderately lit room, before FlashScale (scct.Flash.Candelas). One
	 * value for every muzzle flash light, the local player's view model and every flash seen from outside, so a
	 * shot lights the room the same whoever fires it and from wherever it is seen.
	 */
	OPENSCCT_API float MuzzleFlashCandelas();

	/** The same factor with the caller's own bounds, for a light that must keep up over the whole exposure range. */
	OPENSCCT_API float PictureScale(float Least, float Most);
}
