#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * The page over the hub while it loads (the Godot client's HubLoadingScreen): "Entering" and the hub's name, the side
 * the player arrives as, and what the visit is doing. It covers the whole screen, so the front end, the blocking level
 * load and the hub's first frames (before the hub has placed the player) are never seen half made.
 *
 * UScctHubSubsystem puts it in the game viewport, where it outlives the change of level: up a few frames before the
 * hub level is opened, faded once the player has stood in the drawn hub for a few frames, and never longer than the
 * subsystem allows.
 *
 * Not ported: the hub map's loading picture and the side's tip (neither has a source in this game yet).
 */
class SScctHubLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHubLoadingScreen) : _bMerc(false) {}
		/** The hub's name as the hub says it (or its address before the welcome). */
		SLATE_ATTRIBUTE(FText, HubName)
		/** The line under the heading: what the visit is doing. */
		SLATE_ATTRIBUTE(FText, Status)
		SLATE_ARGUMENT(bool, bMerc)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Starts fading out; the page lets clicks through from now on. */
	void FadeOut();

	bool IsFading() const { return bFading; }

	/** Faded out completely: it can be taken off the screen. */
	bool IsGone() const { return bFading && Opacity <= 0.0f; }

private:
	bool bFading = false;
	float Opacity = 1.0f;
};
