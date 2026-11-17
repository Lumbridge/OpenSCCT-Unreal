#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FScctHubHud;
class SEditableTextBox;
class SVerticalBox;
struct FScctHubHudModel;

/**
 * The hub HUD's overlay (FScctHubHud owns it): name tags over heads and the world signs, projected where they stand
 * at paint time so they move with the camera of the frame; the chat panel and its composer; the hub's keys, the U
 * prompt and the pad queue line; the emote menu; the player list with its voice volume and mutes; the travel menu.
 * The text that is only looked at is painted directly; the panels that take the mouse are widgets.
 */
class SScctHubHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHubHud) {}
		SLATE_ARGUMENT(TSharedPtr<FScctHubHud>, Hud)
		SLATE_ARGUMENT(TSharedPtr<FScctHubHudModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Gives the composer's text field the keyboard. */
	void FocusComposer();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedRef<SWidget> BuildPlayerList();
	TSharedRef<SWidget> BuildTravel();
	TSharedRef<SWidget> BuildComposer();
	void RebuildPlayerRows();
	void RebuildDestinations();
	FReply OnComposerKey(const FGeometry& Geometry, const FKeyEvent& Key);

	int32 PaintSigns(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	int32 PaintTags(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	int32 PaintTexts(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;
	int32 PaintChat(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const;

	TWeakPtr<FScctHubHud> Hud;
	TSharedPtr<FScctHubHudModel> Model;
	TSharedPtr<SVerticalBox> SpyRows;
	TSharedPtr<SVerticalBox> MercRows;
	TSharedPtr<SVerticalBox> DestinationRows;
	TSharedPtr<SEditableTextBox> Composer;
	int32 BuiltRosterRevision = -1;
	int32 BuiltDestinationRevision = -1;
	bool bWasComposing = false;
};
