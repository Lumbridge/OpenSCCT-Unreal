#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Runtime/ScctMapApi.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;

class SScctStrategicMap3D;
class UWorld;

/** The lobby floor plan and the match's interactive cutaway. Both views share the page's floor and team samples. */
class SScctStrategicMapPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctStrategicMapPage)
		: _bOwnKeys(true)
	{}
		/** The page takes the keyboard and closes itself on Escape and the right mouse button (the lobby). In a match the viewport keeps the keyboard: Escape comes through Close. */
		SLATE_ARGUMENT(bool, bOwnKeys)
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, MatchWorld)
		/** Raised whenever the page closes. */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/**
	 * Opens the page over Host (the one already open there is returned): EScctMapContext::Match for the round being
	 * played, EScctMapContext::Lobby for Map (empty: the lobby's own). Null when the bridge cannot open it (logged).
	 */
	static TSharedPtr<SScctStrategicMapPage> Open(const TSharedRef<SOverlay>& Host, int32 Context, const FString& Map, bool bOwnKeys,
		FSimpleDelegate OnClosed = FSimpleDelegate(), TWeakObjectPtr<UWorld> MatchWorld = {});

	/** The page open over Host, if any. */
	static TSharedPtr<SScctStrategicMapPage> FindOpen(const TSharedRef<SOverlay>& Host);

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return bOwnKeys; }
	virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& KeyEvent) override;
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
	bool Shows3D() const;
	FReply ToggleView();
	TSharedPtr<SScctStrategicMap3D> Iso;
	TArray<FKey> MapKeys;
	bool bFloorPlan = false;

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
