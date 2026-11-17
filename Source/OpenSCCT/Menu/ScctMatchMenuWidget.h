#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScctMatchMenuWidget.generated.h"

class SScctMatchOverlay;

/**
 * What a match level shows over the HUD: the in-match menu (resume, leave the server or go back to the main
 * menu) and, in an online round, what the visit is waiting for and the round's results. Built in code; the
 * widgets are in the .cpp. AScctGameMode owns it and opens the menu on Escape.
 */
UCLASS()
class OPENSCCT_API UScctMatchMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetMenuOpen(bool bOpen);
	bool IsMenuOpen() const { return bMenuOpen; }

	/** Raised when the menu's own "resume" closes it. */
	FSimpleDelegate OnResumed;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SScctMatchOverlay> Overlay;
	bool bMenuOpen = false;
};
