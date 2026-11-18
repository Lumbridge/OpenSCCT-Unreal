#pragma once

#include "CoreMinimal.h"

/**
 * How bright the player's picture is exposed right now, for game code that makes light.
 *
 * The levels are played with automatic exposure: the eye closes down in a lit hall and opens up for a dim room,
 * though no further than a lamp-lit room needs (a dark place stays dark: see the range in ScctExposure.cpp). A flash of a fixed strength is then a faint flicker in the hall and a white-out of the whole
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
	 * exposure. SettledPictureLevel() is a picture the eye has settled on; less is one too dark for the exposure's
	 * range (a night street, a cellar with the lamps shot out), more one too bright. 0 until a frame was measured.
	 */
	OPENSCCT_API float PictureLevel();

	/**
	 * The PictureLevel of a place the exposure has settled on: middle grey (0.18) moved by the bias the maps are
	 * played with (scct.Exposure.Bias). The maps' exposure range and that bias are set in ScctExposure.cpp, over
	 * the levels' own post-process volumes.
	 */
	OPENSCCT_API float SettledPictureLevel();

	/**
	 * What to multiply a flash light's strength by so it reads the same at any exposure. The strengths in the code
	 * are those of a moderately lit room (ReferenceExposure); bounded both ways so a wrong reading cannot blind
	 * or hide a flash.
	 */
	OPENSCCT_API float FlashScale();

	/**
	 * The exposure FlashScale and WallHold go by: the one the picture had settled on before the flashes, not the
	 * one the eye has closed to while a burst lit the room (which would raise each next flash in turn until the
	 * walls burned white). Follows Get() while no flash has been lit for a few seconds; 1 until a frame was drawn.
	 */
	OPENSCCT_API float FlashExposure();

	/** A flash light has just been lit (after its strength was worked out): FlashExposure holds still for a while. */
	OPENSCCT_API void NoteFlash();

	/**
	 * Candelas of a rifle's muzzle flash in a moderately lit room, before FlashScale (scct.Flash.Candelas). One
	 * value for every muzzle flash light, the local player's view model and every flash seen from outside, so a
	 * shot lights the room the same whoever fires it and from wherever it is seen.
	 */
	OPENSCCT_API float MuzzleFlashCandelas();

	/**
	 * What to multiply a flash light's strength (Candelas, already scaled by FlashScale) by so the nearest surface
	 * around At does not burn white: FlashScale keeps a flash as bright in the picture at any exposure for a wall
	 * a metre or two away, but a light's spot on a wall grows with the inverse square of its distance, so with the
	 * gun against a wall the spot was many times brighter, and on a bright map the walls already near the top of
	 * the picture clipped to white. The light is held so its brightest spot on any surface within its reach adds
	 * no more than scct.Flash.WallLevel to the exposed picture; 1 when it is far enough already. Traces the level
	 * (TraceLevel) in 26 directions around At, out to Radius cm. OutNearest gets the distance found (cm, Radius
	 * when nothing is near).
	 */
	OPENSCCT_API float WallHold(const FVector& At, float Candelas, float Radius, float* OutNearest = nullptr);

	/**
	 * The level's own geometry (walls, floors, static meshes, movers; not pawns) on the line from Start to End, in
	 * the simulation's collision through the bridge (Runtime/ScctFxTraceApi.h, the effects' trace): Unreal's copy of the level has no collision, the simulation
	 * owns it. True when something is met, OutDistance the distance to it (cm); false with no hit or no session.
	 */
	OPENSCCT_API bool TraceLevel(const FVector& Start, const FVector& End, float& OutDistance);

	/** The same factor with the caller's own bounds, for a light that must keep up over the whole exposure range. */
	OPENSCCT_API float PictureScale(float Least, float Most);
}
