#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;

/**
 * A question over the current page with two answers, as the original's yes/no page draws it: the page behind is
 * dimmed and takes no clicks, a glass box in the middle holds the question and the No and Yes buttons. Escape
 * answers No. The box takes the keyboard while it is open and gives it back to whatever had it before; it
 * removes itself from its overlay once answered.
 *
 * Used before the actions that cannot be taken back: quitting the game, leaving a match or a server, putting
 * every option back to its default, leaving the options with changes that were not saved.
 */
class SScctConfirm : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctConfirm)
		: _YesLabel(TEXT("YES"))
		, _NoLabel(TEXT("NO"))
	{}
		SLATE_ARGUMENT(FString, Message)
		SLATE_ARGUMENT(FString, YesLabel)
		SLATE_ARGUMENT(FString, NoLabel)
		SLATE_EVENT(FSimpleDelegate, OnYes)
		SLATE_EVENT(FSimpleDelegate, OnNo)
	SLATE_END_ARGS()

	/**
	 * Opens a box over Host (an overlay the size of the screen or page it is about) and returns it. Only one box is
	 * open per overlay: asking again while one is up returns that one and drops the new question.
	 */
	static TSharedRef<SScctConfirm> Open(const TSharedRef<SOverlay>& Host, const FString& Message, const FString& YesLabel,
		const FString& NoLabel, FSimpleDelegate OnYes, FSimpleDelegate OnNo = FSimpleDelegate());

	/** The box open over Host, if any. */
	static TSharedPtr<SScctConfirm> FindOpen(const TSharedRef<SOverlay>& Host);

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void AnswerYes() { Answer(true); }
	void AnswerNo() { Answer(false); }

private:
	void Answer(bool bYes);

	FSimpleDelegate OnYes;
	FSimpleDelegate OnNo;
	TWeakPtr<SOverlay> Host;
	TWeakPtr<SWidget> FocusBefore;
	bool bFocusTaken = false;
	bool bAnswered = false;
};
