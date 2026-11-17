#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Play/ScctSessionSubsystem.h"
#include "ScctMatchMenuWidget.generated.h"

class SScctMatchOverlay;

/**
 * What a match level shows over the HUD: the in-match menu and the round's results (Menu/SScctMatchMenu.h) and,
 * in an online round, what the visit is waiting for. Built in code. AScctGameMode owns it and opens the menu on
 * the player's ShowMenu binding.
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

	/** Raised when the menu's own "resume" closes it. */
	FSimpleDelegate OnResumed;

	/** Raised when the menu wants to be opened (the results of a local match): the owner opens it. */
	FSimpleDelegate OnWantsOpen;

	/**
	 * The Equipment page over the menu (the ChangeGear binding, Menu/ScctMatchCommands.h): the menu is opened for it
	 * when closed, and closing the page then goes back to the game. False when it cannot open.
	 */
	bool ShowGear(int32 Side = -1);

	/** The round's strategic map, the same way (the DisplayStrategicMap binding). */
	bool ShowStrategicMap();

	TSharedPtr<class SScctGearPage> GetGearPage() const;
	TSharedPtr<class SScctStrategicMapPage> GetStrategicMapPage() const;

	/**
	 * Covers everything with the play error page (Menu/SScctPlayErrorPage.h) for Error; its LEAVE button and
	 * Escape do what LeaveMatch does. A newer error replaces the page.
	 */
	void ShowPlayError(const FScctPlayError& Error);
	bool IsShowingPlayError() const { return PlayErrorSerial != 0; }

	/** Leaves through the play error page shown (once), as its LEAVE button does. */
	void LeaveFromPlayError();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SScctMatchOverlay> Overlay;
	bool bMenuOpen = false;

	/** The serial of the play error shown (FScctPlayError::Serial), 0 for none, and the error (kept for a rebuild). */
	int32 PlayErrorSerial = 0;
	FScctPlayError ShownPlayError;
};
