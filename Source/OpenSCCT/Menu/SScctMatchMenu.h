#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"
#include "Runtime/ScctMatchMenuApi.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SOverlay;
class SScctDialogLayer;
class SScctGearPage;
class SScctStrategicMapPage;
class UScctOnlineSubsystem;
class UWorld;

/**
 * The menu over a match (Escape, Start): the pause page, the settings and the round's results.
 *
 *   PAUSE     Resume; Change gear (the Equipment page, Menu/SScctGearPage.h); Switch team in a practice round (the
 *             side changes in the round: online the lobby's SetTeam, offline the practice side change); Strategic
 *             map (Menu/SScctStrategicMapPage.h); Options (the settings page, the match goes on behind it); Help (the
 *             side's keys and the round's goals, Menu/SScctHelpPage.h); Change side and Restart (a local match outside
 *             practice: the level starts again); Resign (online, with lives left); Return to lobby (online, for the
 *             player who controls the server); Leave (the server, or the match for the main menu); Quit game. Which
 *             of the Godot client's entries show is InGameMenuModel.Layout for who the player is
 *             (Runtime/ScctMatchMenuApi.h), read again four times a second; each that ends something asks first, a
 *             resign question is taken back once resigning is no longer allowed, and the menu takes no close key in
 *             its first half second (SSMenu_LANMainPage)
 *   SETTINGS  Menu/SScctSettingsPanel.h, as in the front end (or Help & feedback, Menu/SScctHelpFeedbackPage.h, from
 *             the pause page's HELP & FEEDBACK entry)
 *   RESULTS   YOU WIN / YOU LOSE from the side the local player played (online the player's own results line: a Revenge game
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
 * "results", "gear", "map", "help"), and -ScctMatchMenuAct=<entry>[,yes] presses a pause entry a second after the
 * menu first opens (Gear, SwitchTeam, Map, Options, Help, Resign, Lobby, Leave, Quit, Resume) and answers its question
 * yes; the log has the entries, each press, question and answer ("ScctMatchMenu: ..."). The results are driven with:
 *   -ScctResultsAction=next|lobby|leave|rematch|menu   press that button S seconds after the results showed (on
 *                                                      each online results screen, once a run offline), through
 *                                                      the same code a click runs
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

	/**
	 * The Equipment page over the menu, for Side (-1 the side played). bOnly: opened on its own (the ChangeGear
	 * binding), so closing it goes back to the game; from the menu it goes back to the menu. Null when it cannot open.
	 */
	TSharedPtr<SScctGearPage> ShowGear(int32 Side, bool bOnly);
	TSharedPtr<SScctGearPage> GetGear() const;

	/** The round's strategic map over the menu; bOnly as for ShowGear (the DisplayStrategicMap binding). */
	TSharedPtr<SScctStrategicMapPage> ShowStrategicMap(bool bOnly);
	TSharedPtr<SScctStrategicMapPage> GetStrategicMap() const;

	/** The Armoury over the menu (Menu/SScctArmouryOverlay.h); bOnly as for ShowGear (the hub's Armoury key). */
	bool ShowArmoury(bool bOnly);

private:
	enum class EView : uint8 { Pause, Settings, Results };

	void Show(EView NewView, const FString& Sub = FString());
	void ShowNamed(const FString& Name);
	void GoBack();
	void FocusDefault();

	TSharedRef<SWidget> BuildPause();
	TSharedRef<SWidget> BuildResults();
	TSharedRef<SWidget> BuildMatchCard();

	bool ReadResults(int32& OutWinner, FString& OutReason, TArray<TArray<FString>>& OutRows, TArray<int32>& OutTeams, TArray<bool>& OutLocal, TArray<int32>* OutRanks = nullptr) const;
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

	void ShowHelp();
	/** Escape or B while a page over the menu is open: that page goes back. False when none is open. */
	bool BackFromOverlayPage();
	void CloseOverlayPages();
	/** InGameMenuModel.UpdateContext: who the player is, read again; the pause page is laid out anew when its entries change. */
	void RefreshContext(bool bLog);
	/** The pause page's entries from the context (EScctMatchMenuButton, and Quit game). */
	TArray<int32> GetEntries() const;
	FString EntryCaption(int32 Entry) const;
	void Act(int32 Entry);
	/** A question about an entry, taken back when the entry goes (a resign once out of lives). */
	void Ask(int32 Entry, const FString& Title, const FString& Question, const FString& Yes, TFunction<void()> OnYes);
	void TickUnattended(float DeltaSeconds);

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TWeakObjectPtr<UWorld> World;
	bool bOnlineLevel = false;
	FSimpleDelegate OnResume;
	FSimpleDelegate OnLeave;
	FSimpleDelegate OnWantsOpen;

	TSharedPtr<SBox> Body;
	TSharedPtr<SScctDialogLayer> Dialogs;
	TSharedPtr<SScctMenuPage> Settings;
	/** The settings view shows Help & feedback (Menu/SScctHelpFeedbackPage.h) instead of the settings page. */
	bool bFeedbackView = false;
	TWeakPtr<SWidget> DefaultFocus;
	/** The pause page's Options entry: the focus goes back to it when the settings close. */
	TWeakPtr<SWidget> OptionsEntry;
	FScctMenuHost Host;

	/** The Equipment page, the strategic map and the help page open over the menu here. */
	TSharedPtr<SOverlay> PageLayer;
	/** The page over the menu was opened on its own (a binding): its closing goes back to the game. */
	bool bPageOnly = false;

	FScctMatchMenuContext Context = {};
	bool bContextKnown = false;
	TArray<int32> ShownEntries;
	/** The entry whose question is open; -1 none. */
	int32 Asking = -1;
	float OpenedSeconds = 0.0f;
	float ContextTimer = 0.0f;
	float UnattendedSeconds = 0.0f;
	bool bUnattendedActed = false;
	bool bUnattendedYes = false;

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
