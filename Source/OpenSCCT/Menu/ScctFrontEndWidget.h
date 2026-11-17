#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScctFrontEndWidget.generated.h"

class SScctFrontEnd;

/**
 * The front end in the viewport: the main menu's pages and the online visit's pages. Built in code like the HUD
 * (no Blueprint asset); the pages are in Menu/SScctFrontEnd.h.
 */
UCLASS()
class OPENSCCT_API UScctFrontEndWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Unattended: shows a page by name ("offline", "join", "servers", "options"). */
	void ShowPageNamed(const FString& Name);

	/** Opens the settings page on the tab of that name (\"settings/<tab>\"). */
	void ShowOptions(FName Page);

	/** The QUIT question, as the button asks it. */
	void AskQuit();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SScctFrontEnd> FrontEnd;
};
