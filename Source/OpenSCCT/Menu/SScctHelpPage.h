#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;

/**
 * The in-match Help page (gap 179), next to Options in the in-match menu as the Godot client's InGameMenuPage.Help:
 *
 *   Controls   the side being played: every action the player can bind, in the Godot client's order under its
 *              category (movement, combat, gadgets, vision, communication, interface), with the keys the player has
 *              on it now (the bindings in play, so a key rebound on the settings page shows here at once); an
 *              action without a key is left out and counted at the end
 *   Goals      the round's mode and what it asks of each side, the lives left, and the mission's objectives as the
 *              player's side reads them, those already done marked (Runtime/ScctMatchMenuApi.h GetGoals)
 *   Back       closes it, back to the menu
 *
 * The Godot page also links to the project's help and feedback pages; that part is not here. It opens over a host
 * overlay, dimming what is behind, like the strategic map (Menu/SScctStrategicMapPage.h). In a match the viewport
 * keeps the keyboard, so Escape reaches it through the menu's Back.
 */
class SScctHelpPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHelpPage) {}
		/** Raised whenever the page closes. */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/** Opens the page over Host (the one already open there is returned). */
	static TSharedRef<SScctHelpPage> Open(const TSharedRef<SOverlay>& Host, FSimpleDelegate OnClosed = FSimpleDelegate());

	/** The page open over Host, if any. */
	static TSharedPtr<SScctHelpPage> FindOpen(const TSharedRef<SOverlay>& Host);

	void Construct(const FArguments& InArgs);

	void Close();

	virtual FName GetType() const { return FName(TEXT("SScctHelpPage")); }

private:
	TWeakPtr<SOverlay> Host;
	FSimpleDelegate OnClosed;
	bool bClosed = false;
};
