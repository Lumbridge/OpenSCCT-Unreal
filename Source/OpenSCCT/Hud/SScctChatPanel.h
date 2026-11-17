#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctChatTranscript.h"
#include "Widgets/SCompoundWidget.h"

class SEditableText;

/**
 * The in-match chat on screen (Godot's ChatPanel), over the HUD and under the match menu.
 *
 * PASSIVE (the composer closed): the last lines of the transcript at the lower left, above the HUD's status block
 * and its log, each shown for Hud.ChatFadeSeconds and then fading over a second; Hud.ChatPassiveLines wrapped lines
 * at most (0 hides them). It takes no input.
 *
 * OPEN (Talk or TeamTalk): a panel with the history of the transcript (the connection's last 64 lines), filtered by
 * the tabs in its header (ALL, TEAM, SYSTEM; a click or Ctrl+Tab), the number of lines it lists, the composer (the
 * channel the line goes to, the text box, the characters left of 100) and a row of the keys. The mouse wheel and
 * PgUp/PgDn scroll it; it follows the newest line unless scrolled back, and then says new lines came. Enter sends
 * (an empty line just closes), Tab switches the channel between ALL and TEAM, Esc closes. The function keys are
 * taken while it is open, so nothing behind it opens; F5/F6 (or whatever the owner says Talk and TeamTalk are on)
 * pick the channel.
 *
 * The owner (UScctChatSubsystem) sends the lines, moves the input between the game and the panel and fills the
 * transcript; the panel only edits, shows and reports.
 */
class SScctChatPanel : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnSubmit, const FString& /*Text*/, bool /*bTeam*/);
	DECLARE_DELEGATE(FOnClosed);

	SLATE_BEGIN_ARGS(SScctChatPanel) {}
		SLATE_ARGUMENT(TSharedPtr<const FScctChatTranscript>, Transcript)
		/** The local player's team (0 spies, 1 mercs): the accent of the team channel. */
		SLATE_ATTRIBUTE(int32, LocalTeam)
		/** The clock the transcript's lines are stamped with (FScctChatEntry::ReceivedAt). */
		SLATE_ATTRIBUTE(double, Clock)
		/** A line was entered (not empty). The panel has closed already. */
		SLATE_EVENT(FOnSubmit, OnSubmit)
		/** The panel closed and the key that closed it is up again: the game may have the keys back. */
		SLATE_EVENT(FOnClosed, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	enum class EFilter : uint8
	{
		All,
		Team,
		System,
	};

	void Open(bool bTeam);

	/** Closes without sending. The owner hears OnClosed once the key that closed it (if any) is up. */
	void Close();

	bool IsOpen() const { return bOpen; }

	/** Open, or closed but still waiting for the closing key to come up. */
	bool HoldsInput() const { return bOpen || bReleasing; }

	bool IsTeam() const { return bTeam; }
	void SetTeam(bool bInTeam) { bTeam = bInTeam; }

	/** The keys of the Talk and TeamTalk bindings, which pick the channel while the panel is open. */
	void SetChannelKeys(const TArray<FKey>& InTalkKeys, const TArray<FKey>& InTeamTalkKeys);

	void SetFilter(EFilter InFilter);
	EFilter GetFilter() const { return Filter; }

	FString GetDraft() const;

	/** For unattended checks: puts text in the box as if typed (cut to 100 characters). */
	void SetDraft(const FString& Text);

	/** Sends what is in the box, as Enter does. */
	void Submit();

	/** Lines back from the newest; positive scrolls up. */
	void ScrollBy(int32 Lines);

	/** The text box, for the input mode's focus. */
	TSharedPtr<SWidget> GetFocusTarget() const;

	/** The lines the history lists now (with the filter), as the header counts them. */
	int32 CountListed() const;

	// SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	static constexpr int32 MaxChars = 100;

private:
	/** Where the parts of the open panel go, in the widget's units. */
	struct FLayout
	{
		FBox2f Panel;
		FBox2f Header;
		FBox2f History;
		FBox2f Composer;
		FBox2f Entry;
		FBox2f Hint;
		float PassiveBottom = 0.0f;
		float Left = 0.0f;
		float Width = 0.0f;
	};
	static FLayout Layout(const FVector2f& Size);

	void FinishClose(const FKey& ClosingKey);
	void HandleTextChanged(const FText& Text);
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitType);
	bool Shows(const FScctChatEntry& Entry) const;
	FMargin EntryPadding() const;

	TSharedPtr<const FScctChatTranscript> Transcript;
	TAttribute<int32> LocalTeam;
	TAttribute<double> Clock;
	FOnSubmit OnSubmit;
	FOnClosed OnClosed;
	TSharedPtr<SEditableText> Entry;

	bool bOpen = false;
	bool bTeam = false;
	EFilter Filter = EFilter::All;
	TArray<FKey> TalkKeys, TeamTalkKeys;

	/** Closed by a key that is still down: the input stays here until it comes up (or a moment passes). */
	bool bReleasing = false;
	FKey ReleaseKey;
	double ReleaseUntil = 0.0;

	/** Wrapped lines back from the newest; the history follows the newest line at 0 (a paint keeps a scrolled view where it was as lines arrive). */
	mutable int32 Scroll = 0;

	// Measured in the last paint, for scrolling and clicks.
	mutable int32 PaintedLines = 0;
	mutable int32 VisibleLines = 1;
	/** The newest line the last open paint listed; -1 when the listing is new (just opened, another tab). */
	mutable int64 PaintedLastId = -1;
	mutable bool bNewBelow = false;
	mutable TArray<TPair<FBox2f, EFilter>> TabBoxes;
};
