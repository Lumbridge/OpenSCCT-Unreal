#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Runtime/ScctMapApi.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;

/**
 * The Strategic map page (gaps 45 and 131), as the Godot client's StrategicMapScreen lays it out (GUI.StrategicMap):
 *
 *   Picture   the shown floor's picture (svmap/<map>/ui/strategicmap/floor<n>.png, read from the converted content
 *             when the page first shows it), the map's numbered places on it, and in a round the "you are here"
 *             marker (a dot with a view cone the way the player faces) and a marker for each team mate on that floor,
 *             blinking bright and dim every half second; "No map picture" when the floor is not converted
 *   Floors    "Floor n / m" with Up and Down, for a map with more than one floor; the page opens on the player's
 *   Legend    the map's legend (its rooms by number), beside the picture
 *   Back      closes it
 *
 * The rules are the bridge's (Runtime/ScctMapApi.h over the Godot StrategicMapModel). In a match it opens on the
 * DisplayStrategicMap binding (F7 by default; Menu/ScctMatchCommands.h, when the player's state allows it), from
 * STRATEGIC MAP in the in-match menu, and closes on the same key, Escape or Back. In a lobby it opens from the
 * lobby's STRATEGIC MAP as a plain overview of the lobby's map, without markers; Escape and the right mouse button
 * close it there.
 *
 * It opens over a host overlay, dimming what is behind, like the Equipment page (Menu/SScctGearPage.h).
 */
class SScctStrategicMapPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctStrategicMapPage)
		: _bOwnKeys(true)
	{}
		/** The page takes the keyboard and closes itself on Escape and the right mouse button (the lobby). In a match the viewport keeps the keyboard: Escape comes through Close. */
		SLATE_ARGUMENT(bool, bOwnKeys)
		/** Raised whenever the page closes. */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/**
	 * Opens the page over Host (the one already open there is returned): EScctMapContext::Match for the round being
	 * played, EScctMapContext::Lobby for Map (empty: the lobby's own). Null when the bridge cannot open it (logged).
	 */
	static TSharedPtr<SScctStrategicMapPage> Open(const TSharedRef<SOverlay>& Host, int32 Context, const FString& Map, bool bOwnKeys,
		FSimpleDelegate OnClosed = FSimpleDelegate());

	/** The page open over Host, if any. */
	static TSharedPtr<SScctStrategicMapPage> FindOpen(const TSharedRef<SOverlay>& Host);

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return bOwnKeys; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	/** The D-pad and the stick stay on this page: the menu's entries under it are not where they go. */
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override { return FNavigationReply::Stop(); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Up and Down, as the buttons do. True when the floor changed. */
	bool FloorUp();
	bool FloorDown();

	/** Closes the page. */
	void Close();

	const FScctMapState& GetState() const { return State; }

	/** One line for the log: the floor, the marker's U, V and the player's point in the simulation. */
	FString Describe() const;

	virtual FName GetType() const { return FName(TEXT("SScctStrategicMapPage")); }

private:
	void Refresh();

	TWeakPtr<SOverlay> Host;
	TWeakPtr<SWidget> FocusBefore;
	FSimpleDelegate OnClosed;
	bool bOwnKeys = true;
	bool bFocusTaken = false;
	bool bClosed = false;
	float Timer = 0.0f;

	FScctMapState State = {};
	TArray<FScctMapPlace> Places;
	FString Legend;
};
