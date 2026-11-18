#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UTextureRenderTarget2D;

/** What the grenade preview's picture widget paints, filled by UScctNadePreviewSubsystem every frame. */
struct FScctNadePreviewPipModel
{
	/** The framed picture of the landing point is drawn. */
	bool bPicture = false;

	/** Under the picture: the gadget, the charge and the flight time. */
	FString Caption;

	/** The frame's colour: the line's (ready, charging, blocked). */
	FLinearColor Frame = FLinearColor::White;

	/** A short line in the middle of the screen (why the preview was refused), until NoticeUntil (platform seconds). */
	FString Notice;
	double NoticeUntil = 0.0;
};

/**
 * The picture-in-picture of the grenade preview (console command nadepreview): the landing point as a scene capture
 * sees it, in a thin frame at the right edge of the screen, vertically centred, with a caption under it. That spot is
 * free of the HUD: the radar and objectives are top left, the feed top right, the status block bottom left, the
 * equipment bottom right. It fills the viewport it is put in (just below the HUD), takes no input and has no children.
 */
class SScctNadePreviewPip : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctNadePreviewPip) {}
		SLATE_ARGUMENT(TSharedPtr<const FScctNadePreviewPipModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The render target the picture shows; null takes it away. Its owner keeps it alive while it is set. */
	void SetTarget(UTextureRenderTarget2D* Target);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TSharedPtr<const FScctNadePreviewPipModel> Model;
	FSlateBrush Picture;
	bool bHasTarget = false;
};
