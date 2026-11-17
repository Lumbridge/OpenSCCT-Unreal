#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * The brightness test pattern (the original's "Brightness Configuration", GUI.Mire, opened from the Options
 * screen's Display page): on black, three dark squares in faint frames; the left one carries a check just above
 * black, the other two crosses darker still. The player turns the brightness until the left check can just be seen
 * and the crosses stay black.
 *
 * The squares are drawn through the game's own brightness (Display.Brightness, the display gamma the 3D picture
 * gets): moving the slider under them changes them exactly as it changes the picture, so the pattern measures the
 * game's brightness on this screen, not only the screen's. The value is applied as it moves and saved when the page
 * closes (BACK, Escape or Enter); Left and Right step it.
 *
 * It covers the whole viewport, over the front end or a match, and gives the keyboard back to whatever had it.
 */
class SScctBrightnessPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctBrightnessPage) {}
	SLATE_END_ARGS()

	/** Opens the page over the game viewport (once: a second call while it is open does nothing). */
	static void Open();

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void Close();

private:
	void Step(int32 By);

	TWeakPtr<SWidget> FocusBefore;
	bool bFocusTaken = false;
	bool bClosed = false;
};
