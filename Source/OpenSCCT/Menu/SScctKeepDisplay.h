#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * After the display mode, the resolution or the monitor changed: "Keep this display?" with a countdown, over
 * everything in the game viewport. KEEP keeps it; GO BACK, Escape, or the countdown running out puts the last kept
 * display back, so a mode the screen cannot show (a black screen, a window off the desktop) undoes itself without a
 * click. The box takes the keyboard while it is open and gives it back afterwards.
 */
class SScctKeepDisplay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctKeepDisplay)
		: _Seconds(15.0f)
	{}
		SLATE_ARGUMENT(float, Seconds)
		SLATE_ARGUMENT(FString, Description)
		SLATE_EVENT(FSimpleDelegate, OnKeep)
		SLATE_EVENT(FSimpleDelegate, OnRevert)
	SLATE_END_ARGS()

	/**
	 * Opens the box over the game viewport; false when there is no viewport to show it in. Asking again while one is
	 * open replaces its question and starts the countdown again (the earlier change is kept as part of the new one).
	 */
	static bool Open(const FString& Description, float Seconds, FSimpleDelegate OnKeep, FSimpleDelegate OnRevert);

	static bool IsOpen();

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void Answer(bool bKeep);

private:
	FSimpleDelegate OnKeep;
	FSimpleDelegate OnRevert;
	TWeakPtr<SWidget> FocusBefore;
	double Deadline = 0.0;
	bool bFocusTaken = false;
	bool bAnswered = false;
};
