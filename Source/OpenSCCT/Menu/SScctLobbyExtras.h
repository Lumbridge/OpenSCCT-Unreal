#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;

/**
 * The lobby's dialogs for every player (Online/ScctLobbyExtras.h has the data), each opened over a layer of the lobby
 * page (Menu/SScctMenuLobby.cpp):
 *
 *   Match scoreboard   the Godot client's RoundScoreboardDialog from the lobby's "View scoreboard": the last round's
 *                      map and outcome, when and where it was played and for how long, the timing line, both sides'
 *                      tables (the local player's side first, the RoundScoreboardModel columns, the local row lit, a
 *                      rank badge in its emblem colour) and the breakdown of one player picked with a stepper
 *   Briefing           BriefingPanel: the map and mode, page 1 with the level picture and MISSION MAIN OBJECTIVE, page
 *                      2 with the briefing plan (its place numbers drawn over it when the plan was drawn through the
 *                      snapshot camera), the mode's briefing and the legend; NEXT / PREVIOUS flips, CLOSE closes
 *
 * Escape or B closes the dialog on show (ScctLobbyPanels::Back, which the lobby asks first).
 */
namespace ScctLobbyPanels
{
	/** The last round's scoreboard over Layer. False when there is none (or the bridge lacks the table). */
	bool OpenScoreboard(const TSharedPtr<SOverlay>& Layer);

	/** The setup's briefing over Layer, for the local player's side. False outside a visit. */
	bool OpenBriefing(const TSharedPtr<SOverlay>& Layer);

	/** Closes the dialog on show over Layer. False when none is open. */
	bool Back(const TSharedPtr<SOverlay>& Layer);

	bool IsOpen(const TSharedPtr<SOverlay>& Layer);
}
