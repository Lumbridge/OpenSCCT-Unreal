#pragma once

#include "CoreMinimal.h"

class FScctHudPainter;
struct FScctHudState;
struct FScctVisorState;

/**
 * The merc's gas mask over the view: the MaskGas area of the original's merc page (P_HudDEF), which the Godot client
 * plays as a Magma animation (game/Scripts/Hud/Gadgets/GadgetAreaDrivers.cs GasMaskDriver, spec/equipment/gadget-hud.md
 * rules 18 to 21). The Unreal client cannot play Magma, so the area's layers are listed here with the rectangles,
 * alphas and key frames the page gives them (640 x 480 page units, 10 frames a second) and drawn with the same
 * textures, read at run time from the player's content (Hud.maskgaz, maskgaz02, sniper_secondborder, zoom_rond01,
 * stickshock, stickshock02):
 *
 *   - the frame (MaskGas/Mask/Frame) comes up while the gas mask is the selected gadget, as in the original (on
 *     selection, not on activation): nothing for the first 0.1 s, then it fades in over 0.4 s. It is the black mask
 *     surround in four quarters, the two eyepiece rims, a fine grain over the glass and a slow haze that pulses;
 *   - the breathing (MaskGas/Mask/RespirationMask) runs while the mask is worn (bGasMask): fog that grows up the
 *     glass and thins again, every 1.9 s.
 *
 * As the Godot client stretches a centre-anchored page, a layer edge at or past the 4:3 page's side reaches the side
 * of a wider screen. The air gauge (MaskGas/Jauge) is the HUD's own gauge beside the health block. The fast-breathing
 * loop (RespirationMask2) shows for one update only in the original and is not drawn.
 *
 * Driven by the snapshot alone: GasMaskAirSeconds (0 or more while the area shows) and the GasMaskWorn effect flag.
 * -ScctVisorFx=mask fakes both (selected, then worn 2 s later). Log lines start with "Visor: gas mask".
 */
namespace ScctGasMaskOverlay
{
	/** Notes when the frame came up and when the mask went on (the visor state's Mask fields). */
	void Update(FScctVisorState& Visor, const FScctHudState& State);

	/** Paints the mask for the local merc; nothing while the gas mask is not the selected gadget. */
	void Paint(FScctHudPainter& Painter, const FScctVisorState& Visor, const FScctHudState& State);
}
