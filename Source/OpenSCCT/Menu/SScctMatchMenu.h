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
 *   RESULTS   the winner and every player's score: online from the server's results, offline from the HUD's
 *             scoreboard once the round ended. Shown without taking the mouse while the menu is closed; offline it
 *             opens the menu (Play again, Main menu)
 *
 * UScctMatchMenuWidget (Menu/ScctMatchMenuWidget.cpp) holds it with the visit's banner; AScctGameMode opens and
 * closes it. Unattended runs drive it with -ScctMatchMenu=S and -ScctMenuDo (pages "pause", "settings/<tab>",
 * "results").
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
	bool bOfflineResultsOpened = false;
	int32 ResultsRevision = -1;
	float Shown = 0.0f;
};
