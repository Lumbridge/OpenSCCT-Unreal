#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SScctDialogLayer;
class UScctOnlineSubsystem;
class UWorld;

/**
 * The menu over a match (Escape, Start): the pause page, the settings and the round's results.
 *
 *   PAUSE     Resume; Options (the settings page, the match goes on behind it); Change side (local match: the
 *             level starts again with the other side); Return to lobby (online, for the player who controls the
 *             server); Leave (the server, or the match for the main menu), each that ends something asked first
 *   SETTINGS  Menu/SScctSettingsPanel.h, as in the front end
 *   RESULTS   YOU WIN / YOU LOSE from the side the local player played (online his own results line: a Revenge game
 *             swaps the sides), the winner and every player's score: online from the server's results with the
 *             objectives and who left early (Runtime/ScctRoundApi.h), offline from the HUD's scoreboard once the
 *             round ended. They open the menu when they come: offline Play again, Switch team and Main menu; online
 *             the games left in the series and the server's countdown, who the next game waits for, the personal
 *             demo's state, and Next (ready for the next game, or for the lobby), Back to lobby (the player who
 *             controls the session: everyone, at once) and Leave (asked again, with a warning while the demo is
 *             still being saved). The menu closes again when the visit moves on.
 *
 * UScctMatchMenuWidget (Menu/ScctMatchMenuWidget.cpp) holds it with the visit's banner; AScctGameMode opens and
 * closes it. Unattended runs drive it with -ScctMatchMenu=S and -ScctMenuDo (pages "pause", "settings/<tab>",
 * "results"), and the results with:
 *   -ScctResultsAction=next|lobby|leave|rematch|menu   press that button S seconds after the results showed (once
 *                                                      a run), through the same code a click runs
 *   -ScctResultsActionAfter=S                          (default 3)
 *   -ScctEndMatch=S                                    S seconds into a local match, run its clock out on its server
 *                                                      (Runtime/ScctRoundApi.h EndLocalRound), so it ends by time
 */
class SScctMatchMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMatchMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		/** The level presents an online round. */
		SLATE_ARGUMENT(bool, bOnlineLevel)
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnLeave)
		/** The menu wants to be open (the offline results): the owner opens it as Escape would. */
		SLATE_EVENT(FSimpleDelegate, OnWantsOpen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The owner opened or closed the menu. */
	void SetOpen(bool bOpen);
	bool IsOpen() const { return bOpen; }

	/** The results are on screen (the visit's banner stays away then). */
	bool IsShowingResults() const { return View == EView::Results && bResultsReady; }

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override { return ScctUi::Navigate(InNavigationEvent); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	enum class EView : uint8 { Pause, Settings, Results };

	void Show(EView NewView, const FString& Sub = FString());
	void ShowNamed(const FString& Name);
	void GoBack();
	void FocusDefault();

	TSharedRef<SWidget> BuildPause();
	TSharedRef<SWidget> BuildResults();
	TSharedRef<SWidget> BuildMatchCard();

	bool ReadResults(int32& OutWinner, FString& OutReason, TArray<TArray<FString>>& OutRows, TArray<int32>& OutTeams, TArray<bool>& OutLocal) const;
	void Restart(bool bSwapSide);
	int32 GetResultsLocalTeam() const;
	void LogResults() const;
	void ResultsNext();
	void ResultsToLobby();
	void ResultsAskLeave();
	void RunResultsUnattended();
	FString GetMapId() const;
	bool IsMercLocal() const;
	bool CanChangeSide() const;

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TWeakObjectPtr<UWorld> World;
	bool bOnlineLevel = false;
	FSimpleDelegate OnResume;
	FSimpleDelegate OnLeave;
	FSimpleDelegate OnWantsOpen;

	TSharedPtr<SBox> Body;
	TSharedPtr<SScctDialogLayer> Dialogs;
	TSharedPtr<SScctMenuPage> Settings;
	TWeakPtr<SWidget> DefaultFocus;
	/** The pause page's Options entry: the focus goes back to it when the settings close. */
	TWeakPtr<SWidget> OptionsEntry;
	FScctMenuHost Host;

	EView View = EView::Pause;
	bool bOpen = false;
	bool bResultsReady = false;
	/** The results opened the menu (it closes with them online). */
	bool bResultsOpenedMenu = false;
	float ResultsShownFor = 0.0f;
	float LevelTime = 0.0f;

	// Unattended.
	FString ResultsAction;
	float ResultsActionAfter = 3.0f;
	bool bResultsActionDone = false;
	float EndMatchAt = -1.0f;
	int32 ResultsRevision = -1;
	float Shown = 0.0f;
};
