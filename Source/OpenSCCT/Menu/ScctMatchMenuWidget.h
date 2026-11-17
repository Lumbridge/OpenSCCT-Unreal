#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScctMatchMenuWidget.generated.h"

class SScctMatchOverlay;

/**
 * What a match level shows over the HUD: the in-match menu (resume, options, leave the server or go back to the
 * main menu, after asking) and, in an online round, what the visit is waiting for and the round's results. Built in
 * code; the widgets are in the .cpp. AScctGameMode owns it and opens the menu on the player's ShowMenu binding.
 *
 * Unattended switches, acted on the first time the menu opens (use with -ScctMatchMenu=S):
 *   -ScctOptions=<Page>   open the Options screen on that page
 *   -ScctConfirmTest      ask the LEAVE question
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
	 * The menu's close keys (Escape, ShowMenu) while it is open: answers an open question No, or goes BACK on the
	 * Options screen. False when neither is open, and the keys close the menu.
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
