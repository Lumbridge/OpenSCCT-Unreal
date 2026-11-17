#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UScctOnlineSubsystem;
class UWorld;

/**
 * The results of a round (the Godot client's ResultsScreen): "YOU WIN" / "YOU LOSE" from the local player's own line
 * of the results (a Revenge game swaps the sides, RoundResultSide), the winning side or Merc Deathmatch's leader, the
 * reason with the objectives taken, every player's side, score, kills, deaths and objectives, and the ways on.
 *
 *   ONLINE   from the server's results (Runtime/ScctOnlineApi.h GetResults, Runtime/ScctRoundApi.h for the
 *            objectives and the demo): the games left in the series and the server's countdown, who the next game
 *            waits for, the personal demo's state, and NEXT (ready for the next game of the series, or for the
 *            lobby), BACK TO LOBBY (the player who controls the session: everyone, at once) and LEAVE (asked again,
 *            with a warning while the demo is still being saved)
 *   OFFLINE  from the local match's HUD snapshot once the round ended and the HUD's end banner played: REMATCH (the
 *            same level with the same options) and MAIN MENU
 *
 * It shows by itself while there are results. Its owner (Menu/ScctMatchMenuWidget.cpp) makes it take the mouse while
 * the in-match menu is open (SetInteractive) and opens the menu when results arrive (OnWantsInput); without the menu
 * it only shows.
 *
 * Unattended switches:
 *   -ScctResultsAction=next|lobby|leave|rematch|menu   press that button S seconds after the results showed (each
 *                                                      time they show), through the same code a click runs
 *   -ScctResultsActionAfter=S                          (default 3)
 *   -ScctEndMatch=S                                    S seconds into a local match, run its clock out on its server
 *                                                      (Runtime/ScctRoundApi.h EndLocalRound), so it ends by time
 */
class SScctResultsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctResultsPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		/** The level presents an online round. */
		SLATE_ARGUMENT(bool, bOnlineLevel)
		/** LEAVE online (the server) and MAIN MENU offline: the owner's way out of the match. */
		SLATE_EVENT(FSimpleDelegate, OnLeave)
		/** Results arrived: the owner gives the panel the mouse (opens the menu). */
		SLATE_EVENT(FSimpleDelegate, OnWantsInput)
		/** The results went away (the next round or the lobby): the owner may close the menu it opened for them. */
		SLATE_EVENT(FSimpleDelegate, OnGone)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Results are on screen. */
	bool IsShowing() const { return bShowing; }

	/** The buttons take the mouse and the keys (the in-match menu is open). */
	void SetInteractive(bool bInteractive) { bTakesInput = bInteractive; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** One player's line, from either source. */
	struct FRow
	{
		FString Name;
		int32 Team = 0;
		int32 Score = 0, Kills = 0, Deaths = 0;
		int32 Objectives = -1; // -1 when not known
		bool bLocal = false;
		bool bLeftEarly = false;
	};

	/** What the panel shows, from either source. */
	struct FData
	{
		int32 Winner = 255;    // 0 spies, 1 mercs, 255 none
		int32 LocalTeam = 0;   // the side the local player played this round
		int32 Reason = 255;    // 0 objectives or limit, 1 time, 2 a side out of lives, 255 stopped
		int32 ObjectivesTaken = 0, ObjectivesNeeded = 0;
		int32 Mode = 0;        // EGameMode: 6 Deathmatch, 7 Merc Deathmatch
		int32 GamesLeft = 0;   // online: games of the series still to play
		bool bSpectator = false;
		TArray<FRow> Rows;
	};

private:
	bool ReadOnline(FData& Out) const;
	bool ReadOffline(FData& Out) const;
	void Fill();
	TSharedRef<SWidget> BuildButtons();

	void Next();
	void ToLobby();
	void AskLeave();
	void Rematch();
	void MainMenu();
	void RunUnattended(float DeltaSeconds);

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TWeakObjectPtr<UWorld> World;
	bool bOnlineLevel = false;
	FSimpleDelegate OnLeave;
	FSimpleDelegate OnWantsInput;
	FSimpleDelegate OnGone;

	TSharedPtr<SVerticalBox> Column;
	FData Data;
	bool bShowing = false;
	bool bTakesInput = false;
	bool bAskingLeave = false;
	int32 SeenRevision = -1;
	float ShownFor = 0.0f;
	float LevelTime = 0.0f;

	FString UnattendedAction;
	float UnattendedAfter = 3.0f;
	bool bUnattendedDone = false;
	float EndMatchAt = -1.0f;
};
