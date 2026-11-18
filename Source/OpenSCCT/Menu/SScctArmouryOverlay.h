#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;

/**
 * The Armoury page (Menu/SScctArmouryPage.cpp) over whatever the player is in, as the Godot client opens its
 * ArmouryScreen over the lobby (the Cosmetics button) and over the hub (the Armoury key, O): the page under its
 * title with a BACK button, covering the layer it opens on. A look picked here is saved as from the main menu, and
 * the bridge sends it to the lobby or hub the player is in (Social/Appearance.cs FollowLook), worn from the next
 * round in a lobby and at once in a hub. Closing it goes back to what was underneath.
 *
 * It opens over a layer of its host like the Equipment page: the lobby's page layer (Menu/SScctMenuLobby.cpp) or the
 * in-match menu's (Menu/SScctMatchMenuEntries.cpp, where the hub's Armoury key opens the menu for it). Escape comes
 * from the host's Back; Q/E and the shoulders step the page's slots while it has the keys.
 */
class SScctArmouryOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctArmouryOverlay) {}
		/** Raised whenever the page closes. */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/** Opens the Armoury over Layer (the one already open there is returned) and gives it the keys. */
	static TSharedRef<SScctArmouryOverlay> Open(const TSharedRef<SOverlay>& Layer, const FScctMenuHost& Host, const FString& Where, FSimpleDelegate OnClosed = FSimpleDelegate());

	/** The Armoury open over Layer, if any. */
	static TSharedPtr<SScctArmouryOverlay> FindOpen(const TSharedRef<SOverlay>& Layer);

	/** Escape or B over a null-safe layer: closes the Armoury open there. False when none is. */
	static bool Back(const TSharedPtr<SOverlay>& Layer);

	void Construct(const FArguments& InArgs, const FScctMenuHost& Host, const FString& Where);

	void Close();

	virtual FName GetType() const { return FName(TEXT("SScctArmouryOverlay")); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override { return ScctUi::Navigate(InNavigationEvent); }

private:
	TWeakPtr<SOverlay> Layer;
	TSharedPtr<SScctMenuPage> Page;
	FSimpleDelegate OnClosed;
	FString Where;
	bool bClosed = false;
};
