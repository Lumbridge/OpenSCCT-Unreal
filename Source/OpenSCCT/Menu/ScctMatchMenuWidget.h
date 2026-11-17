#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScctMatchMenuWidget.generated.h"

class SScctMatchOverlay;

/**
 * What a match level shows over the HUD: the in-match menu and, in an online round, what the visit is waiting for
 * and the round's results. Built in code; the widgets are in the .cpp. AScctGameMode owns it and opens the menu on
 * the player's ShowMenu binding.
 *
 * The menu's entries are the Godot client's In-Game Menu for who the player is (InGameMenuModel.Layout through
 * Runtime/ScctMatchMenuApi.h, read again four times a second while it is open): RESUME, CHANGE GEAR, SWITCH TO
 * SPY / MERC (a practice round), STRATEGIC MAP, OPTIONS, HELP (Menu/SScctHelpPage.h), RESIGN (an online round with
 * lives left: the "!resign" command), RETURN TO LOBBY (the online host: everyone goes back), MAIN MENU / LEAVE THE
 * SERVER / END SESSION, and QUIT GAME. A tutorial has no gear, map or team entries. Resign, Return to Lobby, leaving
 * and quitting ask first (Menu/SScctConfirm.h). In its first half second the menu ignores its close keys.
 *
 * Unattended switches, acted on the first time the menu opens (use with -ScctMatchMenu=S):
 *   -ScctOptions=<Page>   open the Options screen on that page
 *   -ScctConfirmTest      ask the LEAVE question
 *   -ScctMatchMenuAct=<entry>[,yes]  a second after it opens, press an entry (SwitchTeam, Resign, Lobby, Leave,
 *                         Quit, Help, Gear, Map, Options, Resume); ",yes" answers its question yes. The log has the
 *                         entries ("ScctMatchMenu: entries [...]"), each press, question and answer, and team changes.
 */
UCLASS()
class OPENSCCT_API UScctMatchMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetMenuOpen(bool bOpen);
	bool IsMenuOpen() const { return bMenuOpen; }

	/** The menu's last button: leaves the server (online round) or goes back to the main menu (local match). */
	void LeaveMatch();

	/** Opens the Options screen over the match (the menu must be open), on one of its pages. */
	void ShowOptions(FName Page);

	/**
	 * Opens the Equipment page (Menu/SScctGearPage.h) on a side's tab (-1 the side played). When the menu is closed
	 * it is opened first through OnOpenRequested, and closing the page then goes back to the game. False when it
	 * could not open.
	 */
	bool ShowGear(int32 Side);

	/** The Equipment page open over the match, if any. */
	TSharedPtr<class SScctGearPage> GetGearPage() const;

	/**
	 * Opens the strategic map (Menu/SScctStrategicMapPage.h) of the round. When the menu is closed it is opened first
	 * through OnOpenRequested, and closing the map then goes back to the game. False when it could not open.
	 */
	bool ShowStrategicMap();

	/** The strategic map open over the match, if any. */
	TSharedPtr<class SScctStrategicMapPage> GetStrategicMapPage() const;

	/** Asked to open the menu (the game mode binds it to its own SetMenuOpen, which hands the keys and the mouse over). */
	FSimpleDelegate OnOpenRequested;

	/**
	 * The menu's close keys (Escape, ShowMenu) while it is open: answers an open question No, or goes BACK on the
	 * Options screen or the Equipment page. False when none is open, and the keys close the menu.
	 */
	bool Back();

	/** Raised when the menu's own "resume" closes it. */
	FSimpleDelegate OnResumed;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SScctMatchOverlay> Overlay;
	bool bMenuOpen = false;
};
