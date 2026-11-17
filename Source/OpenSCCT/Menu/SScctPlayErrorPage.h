#pragma once

#include "CoreMinimal.h"
#include "Play/ScctSessionSubsystem.h"
#include "Widgets/SCompoundWidget.h"

/**
 * The play error page: what stopped play, or kept it from starting, over the level that has no match left, in the
 * Godot client's words (Play/PlayRoot.cs):
 *   - the match could not start:        "Could not start play on 'X': reason"
 *   - the connection was lost/refused:  "Could not play 'X': reason. Press Escape to leave."
 *   - the match's own server failed:    "Server stopped: reason"
 * with the map's title above and a LEAVE button; Escape leaves as well. The match overlay shows it
 * (Menu/ScctMatchMenuWidget.cpp) when the level's session subsystem raises OnPlayError; leaving is what the
 * in-match menu's last button does (the front end, or leaving the server).
 */
class OPENSCCT_API SScctPlayErrorPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctPlayErrorPage) {}
		/** The level presents an online round: the button says what leaving means there. */
		SLATE_ARGUMENT(bool, bOnline)
		SLATE_EVENT(FSimpleDelegate, OnLeave)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FScctPlayError& Error);

	/** The page's sentence for an error, as the Godot client words it (also what the log says was shown). */
	static FString Describe(const FScctPlayError& Error, const FString& MapTitle);

	/** The map's title from the player's content ("ENHANCED AQUARIUS"), or the folder name when it has none. */
	static FString MapTitleOf(const FString& Map);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	FReply OnLeaveClicked();

public:
	/** Leaves once, whichever of the button, Escape or the game mode asks first. */
	void Leave();

private:

	FSimpleDelegate OnLeave;
	bool bLeft = false;
};
