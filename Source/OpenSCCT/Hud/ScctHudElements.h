#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudPainter.h"

// The HUD's elements. Each function paints one block from the model and decides for itself whether it has
// anything to show. The canvas (SScctHudCanvas) calls them in drawing order.
//
// Two layers: the full-screen layer covers the whole viewport (things that belong to the picture: washes, the
// crosshair, world markers) and the safe-area layer stays inside the display's safe zone (everything to read).
namespace ScctHudElements
{
	// --- Full-screen layer ---------------------------------------------------------------------------------
	/** Washes over the picture: knock-out, flash, the red of a hit, the frame of a vision mode or gadget camera. */
	void PaintScreenEffects(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Objective markers in the world. */
	void PaintMarkers(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The names over the local player's teammates (ToggleTeammateNames), drawn through walls. */
	void PaintTeammateNames(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The crosshair with its spread and hit confirmation, what the laser holds, noise and hit arcs around it. */
	void PaintCrosshair(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Binoculars and the rifle scope. */
	void PaintScope(FScctHudPainter& Painter, const FScctHudModel& Model);

	// --- Blinding layer, above everything ------------------------------------------------------------------
	/** The flash-bang white-out (Hud/ScctHudElementsFlash.cpp). */
	void PaintFlashWhiteOut(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The page that replaces the player's view while looking through a gadget's camera: the spy's sticky cam (sweep,
	 * reticle, zoom, gas, key help) or the merc's camera network (camera name, Alert box, zoom, key help, the blank
	 * feed of a shot camera). ScctHudElementsGadgets.cpp.
	 */
	void PaintCameraPages(FScctHudPainter& Painter, const FScctHudModel& Model);

	// --- Safe-area layer -----------------------------------------------------------------------------------
	/** Bottom left: health, lives, stance, the gauges and the vision mode. */
	void PaintStatus(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Bottom right: the weapon with its ammunition and fire mode, and the gadgets. */
	void PaintEquipment(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Bottom centre: what Use does now, or the list to choose from. */
	void PaintPrompt(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * Right side: the sticky cam menu (a spy) or the mine menu (a merc) while it is open, its lines in the original's
	 * colours, the arrow beside the selected one and the unusable ones grey. ScctHudElementsGadgets.cpp.
	 */
	void PaintGadgetMenu(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * Above the status block, a spy's coop icon while a team move with the mate is possible: the content's A_Coop clip
	 * (offer a ladder, climb on the mate, offer a hoist, be boosted up) with its own fades. ScctHudElementsIcons.cpp.
	 */
	void PaintCoopIcon(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * Bottom right while a dead Merc Deathmatch merc picks his respawn weapon: the weapon's tile of the content's weapon
	 * icons and the name of the Use key that cycles it. ScctHudElementsIcons.cpp.
	 */
	void PaintRespawnWeapon(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Top centre: the clock, both teams, objectives done and the countdowns. */
	void PaintRound(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Top left: the mission's objectives and what is being hacked right now. */
	void PaintObjectives(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Top right: the kill feed. */
	void PaintFeed(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Under the clock: briefings and objective headlines. */
	void PaintHeadlines(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Above the status block: the player's own state lines, zones and console lines (chat has its own panel). */
	void PaintLog(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Bottom centre while dead or visiting: who is watched, the wait for the respawn and the team's tip. */
	void PaintSpectator(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The map's name as the round starts, and who won as it ends. */
	void PaintBanner(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The scoreboard, while held; with the objective list under it as the mission status page (DisplaySessionStatus). */
	void PaintScoreboard(FScctHudPainter& Painter, const FScctHudModel& Model);

	// --- Shared by the elements ----------------------------------------------------------------------------
	/** A box from its top left corner and size. */
	inline FBox2f HudBox(float X, float Y, float Width, float Height)
	{
		return FBox2f(FVector2f(X, Y), FVector2f(X + Width, Y + Height));
	}

	/** Lives as the original reads them: from 100 (a player) or 1000 (a team) up they are unlimited. */
	FString LivesText(const FScctHudModel& Model, int32 Lives, int32 InfiniteFrom);

	/** Seconds as m:ss. */
	FString ClockText(int32 Seconds);

	/**
	 * True when the player's own HUD blocks are to be drawn (a live pawn, not hidden by the game, not looking through
	 * a gadget's camera: that page replaces them, as in the original).
	 */
	bool ShowsPlayerHud(const FScctHudModel& Model);
}
