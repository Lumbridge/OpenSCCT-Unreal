#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class APostProcessVolume;
class FScctHudModel;
class FScctHudPainter;
class UMaterialInstanceDynamic;
class UWorld;
struct FScctHudState;
struct FScctVisorState;

/**
 * The poison over the view: the two screen-sized pictures of the original's jauge_poison area (the poison instance of
 * the health gauge), which the Godot client plays as a Magma animation (game/Scripts/Hud/HudPage.cs DriveHealth,
 * spec/hud/poison.md). The Unreal client cannot play Magma, so the two layers are listed here with the rectangles,
 * colours and key frames the page gives them (640 x 480 page units, 10 frames a second), drawn with the same texture
 * read at run time from the player's content (Hud.stickshock, dark in the middle and grey round the edge):
 *
 *   - effet01, alpha blended at half strength: it grows from frame 0's rectangle to frame 10's in a straight line.
 *     The HUD paints it (Paint);
 *   - effet02_dodge, blended as the original's Dodge (the picture under it times 1 + texture x colour, in the colours
 *     of the screen), still. Slate has no such blend, so FScctPoisonDodge puts it in the picture with a post-process
 *     material, M_ScctPoisonDodge (Tools/Materials/Build-PoisonMaterial.ps1). It therefore brightens the scene and not
 *     the HUD, and lies under effet01 rather than over it.
 *
 * The area starts at frame 1 when the poison begins (frame 0 holds a stop) and frame 10 sends it back to frame 1: a
 * loop of 0.9 s that runs for as long as the pawn is poisoned, goes with the cure and starts again at frame 1 with a
 * new infection. The layers show only while the health gauge does (a live pawn, the health element not switched off).
 *
 * The spy uses the original health-area position (566, 230). The merc uses the viewport's top left as the origin of
 * the dodge texture, so both layers cover the screen throughout the loop. Each side keeps its own health-area tint.
 *
 * Driven by the snapshot alone: EScctHudFlags::Poisoned. -ScctVisorFx=poison fakes it (10 s poisoned, 2 s cured, and
 * so on). Log lines start with "Visor: poison".
 */
namespace ScctPoisonOverlay
{
	/** Notes when the overlay came up (the visor state's Poison fields). */
	void Update(FScctVisorState& Visor, const FScctHudState& State);

	/** Paints effet01 for the local player; nothing while the overlay is down. */
	void Paint(FScctHudPainter& Painter, const FScctHudModel& Model);
}

/**
 * effet02_dodge in the picture: an unbound post volume of its own carrying M_ScctPoisonDodge, made the first time a
 * player is poisoned, so a match without poison has no extra blendable at all. The world owns the volume.
 */
class FScctPoisonDodge
{
public:
	/** Brightens the view for this frame, or takes the layer out of it when the overlay is down. */
	void Present(UWorld* World, const FScctVisorState& Visor, const FScctHudState* State);

private:
	bool Ensure(UWorld* World);

	TWeakObjectPtr<APostProcessVolume> Volume;
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	bool bTried = false;
	bool bOn = false;
	int32 Side = INDEX_NONE;
};
