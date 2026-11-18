#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMeshComponent;
class UWorld;

/**
 * The weapon lasers as thermal and EMF vision show them (the Godot client's vision pass of the beam material,
 * svm_laser.gdshader on SpyLaserView and MercLaserView; docs/RENDERING.md#vision-modes of the simulation's repository):
 *   - thermal (36): the merc's laser sight and the spy pistol's beam are both hot (SLaserBeam, HeatIntensity 1);
 *   - EMF (38): the merc's laser sight is the dark grey streak of the detector beams, so a spy in EMF sees where a merc
 *     aims; the spy pistol's beam keeps its plain red look.
 * The red glow of a beam is light added to the picture, which the goggles' materials would read as brightness, so a
 * beam the goggles draw is drawn only here: cylinders that go into the custom depth stencil alone, marked with the
 * values the detector beams use (UScctVisionSubsystem::HeatStencil and StencilEmfStreak), which the mode's material
 * paints where nothing of the scene stands in front (the first-person gun covers the beam's first centimetres as it
 * covers the red one).
 *
 * The cylinders hang on an actor of their own, not on the pawn's proxy: the goggles mark every primitive of a proxy as
 * part of the body (UScctVisionSubsystem::MarkCharacters), and a beam is drawn whether or not the mode draws the body.
 * A beam runs away from the eye, so it is laid in pieces, each as wide as its distance from the eye asks for: one
 * cylinder a centimetre across would be far less than a pixel at its far end.
 *
 * Use, once a frame: BeginFrame, Show for every beam the goggles draw, EndFrame (which hides the rest).
 */
class FScctLaserVision
{
public:
	/** The custom depth stencil of a weapon beam in a vision mode, 0 when the mode leaves the beam its plain look. */
	static int32 StencilFor(int32 VisionMode, bool bMerc);

	void BeginFrame();

	/** Lays the beam of a pawn for this frame. Eye is the camera, for the widths. */
	void Show(UWorld& World, int32 ActorId, const FVector& Start, const FVector& End, const FVector& Eye, int32 Stencil);

	/** Hides the beams Show was not called for since BeginFrame. */
	void EndFrame();

	/** The match is over: everything goes. */
	void Drop();

	/** Beams laid by the last frame (for the log and the checks). */
	int32 NumShown() const { return Shown; }

private:
	static constexpr int32 PieceCount = 4;

	struct FBeam
	{
		TWeakObjectPtr<UStaticMeshComponent> Pieces[PieceCount];
		bool bSeen = false;
		bool bVisible = false;
	};

	UStaticMeshComponent* MakePiece(UWorld& World);

	TWeakObjectPtr<AActor> Host;
	TMap<int32, FBeam> Beams;
	int32 Shown = 0;
};
