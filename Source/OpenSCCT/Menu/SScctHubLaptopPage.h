#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FScctHubPads;
class SVerticalBox;
class SWrapBox;
struct FSlateBrush;

/**
 * A match pad's laptop (an OpenSCCT addition; port of the Godot client's game/Scripts/Hub/HubLaptopPanel.cs): the
 * pad's next match as a lobby's session settings, opened when the hub opens the laptop (U at a pad's laptop) and
 * drawn over the hub, which goes on underneath. A grid of the installed versus maps with their pictures (a map
 * without an imported level here is marked: the match would be refused when it starts), a row per setting with
 * < and > to step it (mode, time limit, lives, players, bots, bot skill, practice, sidearms, rifles), the draft in one
 * line, and Map Defaults, Apply (the hub checks the player still stands at the laptop and says so) and Close (Esc).
 * Everything it shows and does goes through the pads table (FScctHubPads, Runtime/ScctHubPadsApi.h).
 */
class SScctHubLaptopPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHubLaptopPage) {}
		SLATE_ARGUMENT(TSharedPtr<FScctHubPads>, Pads)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Reads the draft again; rebuilds the rows and the maps when they changed (always with bForce). */
	void Refresh(bool bForce);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Key) override;

private:
	void RebuildRows();
	void RebuildMaps();
	const FSlateBrush* PictureBrush(const FString& Path);

	TWeakPtr<FScctHubPads> Pads;
	TSharedPtr<SVerticalBox> RowBox;
	TSharedPtr<SWrapBox> MapBox;
	FString Title;
	FString Status;
	FString Summary;
	FString RowsKey;
	FString MapsKey;
	TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
};
