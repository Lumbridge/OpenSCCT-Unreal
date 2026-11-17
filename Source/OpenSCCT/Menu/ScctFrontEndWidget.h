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

	/** Opens the Options screen on one of its pages (SScctFrontEnd::ShowOptions). */
	void ShowOptions(FName Page);

	/** The QUIT question, as the button asks it. */
	void AskQuit();

	/** The Equipment page, as the offline page's or the lobby's EQUIPMENT opens it (SScctFrontEnd::ShowGear). */
	TSharedPtr<class SScctGearPage> ShowGear(int32 Side = -1);

	/** The strategic map of a map (empty: the lobby's), as the lobby's STRATEGIC MAP opens it (SScctFrontEnd::ShowStrategicMap). */
	TSharedPtr<class SScctStrategicMapPage> ShowStrategicMap(const FString& Map = FString());

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SScctFrontEnd> FrontEnd;
};
