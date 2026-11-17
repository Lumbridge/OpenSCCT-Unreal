#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** What the drill's coach panel shows this frame. UScctDrillSubsystem fills it; the panel only draws it. */
struct FScctDrillCoachModel
{
	/** VISION CANCELLING or MERCENARY GRABBING. */
	FString Title;

	/** The coach's instruction and feedback line. */
	FString Feedback;

	/** Vision-cancelling: baseline, best and the last three attempts. Spy-grab: grabs and distance. */
	FString Results;

	/** The drill has a moving target and a grab area (spy-grab). */
	bool bSpyGrab = false;

	/** The buttons can be clicked: the mouse is free (the in-match menu is open). */
	bool bButtonsLive = false;

	FString RetryLabel;
	FString MovingLabel;
	FString GrabAreaLabel;

	TFunction<void()> OnRetry;
	TFunction<void()> OnMoving;
	TFunction<void()> OnGrabArea;

	/** Vision-cancelling's return portal, projected for this frame's camera: a ring on the floor and its label. */
	static constexpr int32 RingPoints = 33;
	bool bPortal = false;
	FVector2f Ring[RingPoints] = {};
	bool bRingInFront[RingPoints] = {};
	bool bLabel = false;
	FVector2f Label = FVector2f::ZeroVector;
};

/**
 * The TestRange drills' coach (the Godot client's game/Scripts/Tutorials/InteractiveLessonCoach.cs): a panel at the
 * top left with the drill's name, the coach's feedback, the results and the drill's buttons (Retry or Return to
 * ledge, Target walking/stopped and Grab area in spy-grab), plus the green return portal of vision-cancelling drawn
 * on the floor at the foot of the tower. The picture is never blocked: only the buttons take the mouse, and the
 * mouse is the game's until the in-match menu frees it.
 */
class SScctDrillCoach : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctDrillCoach) {}
		SLATE_ARGUMENT(TSharedPtr<FScctDrillCoachModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintPortal(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const;

	TSharedPtr<FScctDrillCoachModel> Model;
};
