#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;
class UWorld;

/**
 * The scene changes of thermal and EMF vision (part of the goggles, UScctVisionSubsystem): those two modes draw no
 * coronas and no sky, so the frame keeps the mode's clear colour where the sky was and a lamp's bright halo does not
 * read as hot or glowing, as the original's ApplyVision turns the coronas and the sky zone off (Godot: FaithfulView).
 *
 * What it hides, found once per map:
 *   - the level's corona cards (the importer's cards tagged "ScctCoronaOf:" + the light's name);
 *   - the beam and flare cards modelled into the level (the importer's glow cards: the one material with EdgeFade);
 *   - the sky: the dome (any mesh drawn with a sky material) and the skyline brought back from the sky zone (tagged
 *     "ScctBackdrop").
 * Each primitive's own visibility is turned off and given back as it was, so the level state, which shows and hides
 * a corona by its actor and dims a glow card by its material, keeps doing so underneath without either undoing the
 * other. The torch coronas on the pawns are left out by the equipment, which asks ShouldHide each frame.
 */
class OPENSCCT_API FScctVisionBackdrop
{
public:
	/** Hides (bHide) or gives back the coronas and the sky. Cheap when nothing changes; call once a frame. */
	void Update(UWorld* World, bool bHide);

	/** Gives everything back and forgets the map. */
	void Reset();

	/** True while the coronas and the sky are hidden. */
	bool IsHidden() const { return bHidden; }

private:
	void IndexLevel(UWorld* World);

	TWeakObjectPtr<UWorld> IndexedWorld;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Coronas;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Sky;

	/** The primitives this hid that were visible before, to be shown again. */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Taken;

	bool bHidden = false;
};
