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

	/**
	 * The merc's motion tracking: the goggles' lock boxes around moving targets (corners zooming in, the sweep band, the
	 * speed and distance of a friend) and the laser target's bracket with its typed name, friend marks and Hold Fire.
	 * ScctHudElementsMotion.cpp.
	 */
	void PaintMotionTracking(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The team pings: each teammate's mark as a disc with its kind's icon above the spot and its distance, or at the
	 * screen's edge with an arrow when out of view; and the ping wheel while the TeamPing key is held.
	 * ScctHudElementsTeamPing.cpp.
	 */
	void PaintTeamPings(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The names over the local player's teammates (ToggleTeammateNames), drawn through walls. */
	void PaintTeammateNames(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The crosshair with its spread and hit confirmation, what the laser holds, noise and hit arcs around it. */
	void PaintCrosshair(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The merc's rifle scope (BinocularMode 2), which replaces the merc's HUD: the content's P_BinocularsDEF (PaintScopePage),
	 * or a plain ring and sights when the content has no such page. The spy's binoculars have their own page below.
	 */
	void PaintScope(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The content's P_BinocularsDEF as the bridge evaluates it for this moment (Model.ScopeLayers): the scope rings, the
	 * sights and axes at the aim point, the breath gauge, the range finder, the zoom label and the key help. The page
	 * keeps its 4:3 shape at the picture's height in the middle of the screen, and a picture that reaches the page's
	 * left or right side reaches the screen's (ScctHudElementsBinoculars.cpp). PaintScope calls it.
	 */
	void PaintScopePage(FScctHudPainter& Painter, const FScctHudModel& Model);

	// --- Blinding layer, above everything ------------------------------------------------------------------
	/** The flash-bang white-out (Hud/ScctHudElementsFlash.cpp). */
	void PaintFlashWhiteOut(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The spy's binocular page (BinocularMode 1), which replaces his HUD: the content's P_BinocularsATT (goggle frame,
	 * sound meter, zoom scale with its x2 / x4 / x6 label and marker, key help, the Hack box) stretched over the screen,
	 * and over it the objective targets: the one under the reticle in the lock panel with its name, distance and the
	 * Lock or Locked hint, the others as their icon with pulsing brackets, those off the screen as an arrow at its edge.
	 * ScctHudElementsBinoculars.cpp.
	 */
	void PaintSpyBinoculars(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The binocular target the lock panel is for on a screen of this size (the first on-screen target whose point is just
	 * right of and below the original's target point, 0.46 / 0.45 of the screen), as an index of Model.BinocularMarkers,
	 * or -1 when there is none.
	 */
	int32 BinocularLockCandidate(const FScctHudModel& Model, const FVector2f& Size);

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

	/**
	 * Bottom right, left of the equipment, a merc's backpack panels: his own share and his two mates' shares (each
	 * gadget's icon with the charges the backpack holds for it) while he holds the backpack, or only the share a mate
	 * carries for him while the refill entry is focused. ScctHudElementsBackpack.cpp.
	 */
	void PaintBackpack(FScctHudPainter& Painter, const FScctHudModel& Model);

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

	/**
	 * Top left: the radar (teammates, enemies and alarms around the player, Higher and Lower icons, arrows at the edge
	 * for enemies and alarms out of range, the heart sensor's cone) and the place name above it, the name in every
	 * mode. ScctHudElementsRadar.cpp.
	 */
	void PaintRadar(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The height the radar and its place name take at the top left; the objectives panel starts below it. */
	float RadarReservedHeight(const FScctHudModel& Model);

	/** Top left, under the radar: the mission's objectives and what is being hacked right now. */
	void PaintObjectives(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Top right: the kill feed (below the readouts when they stand there). */
	void PaintFeed(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The frame-rate and network readouts (Hud.FpsMode, Hud.NetMode) stacked in their corner, with a detailed readout's
	 * graphs. ScctHudElementsTelemetry.cpp.
	 */
	void PaintTelemetry(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The height the readouts take at the top right, with the gap under them; 0 when they are elsewhere or hidden. */
	float TelemetryReservedTopRight(const FScctHudModel& Model);

	/** Under the clock: briefings and objective headlines. */
	void PaintHeadlines(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Above the status block: the player's own state lines, zones and console lines (chat has its own panel). */
	void PaintLog(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * Lower centre: the tutorial message (a tutorial map's hint, an offline session's zone tip) in its box for as long as
	 * the game keeps it up; the briefings wait meanwhile. ScctHudElementsTutorial.cpp.
	 */
	void PaintTutorial(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** Bottom centre while dead or visiting: who is watched, the wait for the respawn and the team's tip. */
	void PaintSpectator(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The map's name as the round starts, and who won as it ends. */
	void PaintBanner(FScctHudPainter& Painter, const FScctHudModel& Model);

	/** The scoreboard, while held; with the objective list under it as the mission status page (DisplaySessionStatus). */
	void PaintScoreboard(FScctHudPainter& Painter, const FScctHudModel& Model);

	/**
	 * The scoreboard as the Godot client has it, when the bridge has the running round's board (Model.LiveBoard): one
	 * table per side, the local player's first, each with rank, name, the public columns (lives, objectives, kills and
	 * deaths), the combat columns the server releases on its 30 second delay and the ping, under the map, the clock and
	 * the line that counts down to the next release; the mission's objectives under it on the status page. False when
	 * there is no such board, and PaintScoreboard then draws its four plain columns. ScctHudElementsLiveBoard.cpp.
	 */
	bool PaintLiveScoreboard(FScctHudPainter& Painter, const FScctHudModel& Model);

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
	 * The kill feed's icon for one line, drawn as vector shapes in a square of Extent units around Centre: the cause's
	 * family (firearms, headshot, explosives, mines, fire, electricity, melee, falls, gas, hazards) for a combat line, the
	 * outcome (death, sleep, knock-out, join, leave, last life, match) otherwise. ScctHudActivityIcons.cpp.
	 */
	void PaintActivityIcon(FScctHudPainter& Painter, const FVector2f& Centre, float Extent, const FScctHudLine& Line, const FLinearColor& Colour);

	/** The kill feed's cause line: how it happened ("Rifle headshot") and who set it off ("Shot by ..."); empty when nothing is known. */
	FString ActivityCauseText(const FScctHudLine& Line);

	/**
	 * True when the player's own HUD blocks are to be drawn (a live pawn, not hidden by the game, not looking through
	 * a gadget's camera or the spy's binoculars: those pages replace them, as in the original).
	 */
	bool ShowsPlayerHud(const FScctHudModel& Model);
}
