#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

struct FScctQuickChatState;

/**
 * The team quick chat on screen (Godot's QuickChatPanel): a glass panel at the right edge, halfway down, with the title
 * (TEAM QUICK CHAT, or TEAM - the category picked), the four choices each with its key (1-4 and its D-pad direction),
 * the keys' hint, and a bar of the time left before the menu closes by itself. It draws the owner's state and takes no
 * input; nothing shows while the menu is closed.
 */
class SScctQuickChatPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctQuickChatPanel) {}
		/** The owner's state, alive as long as the panel (UScctQuickChatSubsystem's). */
		SLATE_ARGUMENT(const FScctQuickChatState*, State)
		/** The local player's team (0 spies, 1 mercs): the panel's accent. */
		SLATE_ATTRIBUTE(int32, LocalTeam)
		/** The clock the state's times are on. */
		SLATE_ATTRIBUTE(double, Clock)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	const FScctQuickChatState* State = nullptr;
	TAttribute<int32> LocalTeam;
	TAttribute<double> Clock;
};
