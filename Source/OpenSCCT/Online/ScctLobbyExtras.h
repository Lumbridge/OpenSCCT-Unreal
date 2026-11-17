#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctLobbyApi.h"

/**
 * What the lobby shows every player besides the sides and the setup (the Godot client's LobbyRoomScreen.Scoreboard,
 * RoundScoreboardDialog, BriefingPanel and RankIcons), read from the bridge's lobby table (Runtime/ScctLobbyApi.h,
 * version 2; Bridge/OpenScct.UnrealBridge/Online/LobbyExtras.cs):
 *
 *   the last round's scoreboard   the server's final snapshot of the round this visit last played (OnlineClient.
 *                                 LastScoreboard): both sides' tables with the columns of RoundScoreboardModel, and a
 *                                 player's breakdown; none before the first round of the visit
 *   the briefing                  the setup's map and mode for the local player's side (a spectator reads the mercs'
 *                                 words): page 1 the level picture and the main objective, page 2 the plan with its
 *                                 place numbers, the mode's briefing and the legend
 *   ranks                         a packed rank's badge, words and emblem colour
 *
 * Each call reads the bridge afresh; nothing is cached here.
 */
struct FScctLastScoreboard
{
	FScctLobbyScoreboardInfo Info = {};
	TArray<FScctLobbyScoreRow> Rows;
	FString Title;
	FString Metadata;
	FString Timing;
	FString Spectators;
	/** Each side's column labels and explanations (0 spies, 1 mercs). */
	TArray<FString> Columns[2];
	TArray<FString> ColumnTips[2];
	/** Per row (in Rows' order), the cells of the row's side's columns. */
	TArray<TArray<FString>> Cells;
};

/** One line of a player's breakdown. */
struct FScctScoreDetail
{
	FString Group;
	FString Label;
	FString Value;
};

struct FScctLobbyBriefing
{
	FString Title;
	FString PageTitle;
	FString Objective;
	FString ObjectiveName;
	FString MapBrief;
	FString ModeBrief;
	FString Legend;
	/** Full PNG paths, empty when the map has none. */
	FString Picture;
	FString Plan;
	TArray<FScctLobbyBriefingMark> Marks;
};

namespace ScctLobbyExtras
{
	/** The bridge's lobby table has the extras (version 2 or later). */
	bool IsAvailable();

	/** The last round's scoreboard of this visit, with its sequence (it changes with each new round); false when there is none. */
	bool HasLastScoreboard(int32* OutSequence = nullptr);

	/** Everything the scoreboard dialog shows. False when there is no scoreboard. */
	bool ReadLastScoreboard(FScctLastScoreboard& Out);

	/** The lobby's "Last game · Map" and outcome lines; empty when there is no scoreboard. */
	FString LastScoreboardSummary();

	/** A row's breakdown (Row in FScctLastScoreboard::Rows' order). */
	TArray<FScctScoreDetail> ReadDetails(int32 Row);

	/** The setup's briefing for Side (0 spies, 1 mercs, -1 the local player's). False outside a visit. */
	bool ReadBriefing(FScctLobbyBriefing& Out, int32 Side = -1);

	/** "P3 · 42" (no title), empty for none. */
	FString RankBadge(int32 PackedRank);

	/** "Prestige 3, level 42 - Field Agent", empty for none. */
	FString RankWords(int32 PackedRank);

	/** The emblem's colour (RankIcons.ColourOf); white for none or without the bridge. */
	FLinearColor RankColour(int32 PackedRank);
}
