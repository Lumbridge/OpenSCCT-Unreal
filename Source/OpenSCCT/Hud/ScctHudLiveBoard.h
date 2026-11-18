#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctLiveScoreboardApi.h"

/**
 * The running round's scoreboard as the HUD draws it (Runtime/ScctLiveScoreboardApi.h): read by the HUD subsystem
 * (ScctHudLiveBoard.cpp) while the scoreboard or the mission status page is up, painted by
 * ScctHudElements::PaintLiveScoreboard (ScctHudElementsLiveBoard.cpp). Not valid when the bridge has no such table or
 * no board yet; the scoreboard then falls back to the four plain columns of the HUD's own player list.
 */
struct FScctHudLiveBoard
{
	bool bValid = false;
	FScctLiveScoreboardInfo Info = {};
	/** The local player's side first, then the other side. */
	TArray<FScctLobbyScoreRow> Rows;
	/** Per row, the rank's badge text ("P3 · 42"), empty for none. */
	TArray<FString> RankTags;
	/** Per row, the cells of the row's side's columns. */
	TArray<TArray<FString>> Cells;
	/** Each side's column labels (0 spies, 1 mercs); the last column is the ping. */
	TArray<FString> Columns[2];
	FString Title;
	FString Clock;
	/** "Combat stats delayed at least 30s · Next update 12.4s", read every frame while the board is up. */
	FString Timing;
	FString Spectators;
	FString Omitted;
};
