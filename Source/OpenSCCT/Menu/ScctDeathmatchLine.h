#pragma once

#include "CoreMinimal.h"

/**
 * The results page's headline of a Merc Deathmatch round, as the Godot client's MatchResult.DeathmatchLine
 * (game/Scripts/UI/MatchResult.cs; Reloaded's rl:qol.scoreboard): the mode has one side, so the page names the
 * player with the most kills instead of a winning side:
 *   "<name> wins with <n> kills"   one player has the most kills
 *   "Draw at <n> kills"            two or more share the most kills
 *   "No kills scored"              nobody scored
 * SScctMatchMenu::BuildResults shows it in place of the winning side's line, in the mercenaries' colour.
 *
 * Unattended switch (off by default):
 *   -ScctResultsKills=5,2,5   the results page takes these kills for its lines, top to bottom as the page lists them
 *                             (lines past the list keep their own), and leads with the deathmatch line whatever
 *                             the mode: a check of the three forms without a fight.
 */
struct FScctDeathmatchLine
{
	/** False outside Merc Deathmatch: the page keeps the winning side's line. */
	bool bShown = false;
	FString Text;
	/** The most kills of a line, and how many lines have that many (0 when nobody scored). */
	int32 Best = 0;
	int32 Leaders = 0;
	/** The local player's line is one of the leaders. */
	bool bLocalLeads = false;

	/** Counts one line of the results, in the order the round gave them. */
	void Add(const FString& Name, int32 Kills, bool bLocal);
	/** Writes Text from the counted lines and sets bShown. */
	void Finish();

	/** -ScctResultsKills: the kills of line Index, or Kills when the switch is not given or the list is shorter. */
	static int32 ForcedKills(int32 Index, int32 Kills);
	static bool IsForced();

private:
	FString Leader;
	bool bAnyLocalLeader = false;
};
