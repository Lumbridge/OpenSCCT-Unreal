#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;

/**
 * The page over the hub while it loads (the Godot client's HubLoadingScreen): "Entering" and the hub's name, the side
 * the player arrives as, and what the visit is doing. It covers the whole screen, so the front end, the blocking level
 * load and the hub's first frames (before the hub has placed the player) are never seen half made.
 *
 * UScctHubSubsystem puts it in the game viewport, where it outlives the change of level: up a few frames before the
 * hub level is opened, faded once the player has stood in the drawn hub for a few frames, and never longer than the
 * subsystem allows.
 *
 * Artwork and tips are read from the installed content.
 */
class SScctHubLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHubLoadingScreen) : _bMerc(false), _Picture(nullptr) {}
		/** The hub's name as the hub says it (or its address before the welcome). */
		SLATE_ATTRIBUTE(FText, HubName)
		/** The line under the heading: what the visit is doing. */
		SLATE_ATTRIBUTE(FText, Status)
		SLATE_ARGUMENT(bool, bMerc)
		SLATE_ARGUMENT(UTexture2D*, Picture)
		SLATE_ARGUMENT(FText, Tip)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Starts fading out; the page lets clicks through from now on. */
	void FadeOut();

	bool IsFading() const { return bFading; }

	/** Faded out completely: it can be taken off the screen. */
	bool IsGone() const { return bFading && Opacity <= 0.0f; }

private:
	FSlateBrush PictureBrush;
	bool bFading = false;
	float Opacity = 1.0f;
};
