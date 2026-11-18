#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class AScctArmouryPreviewStage;

/**
 * The Armoury's turning 3D view (the Godot client's CosmeticPreview): draws the render target of a stage of its own
 * (Menu/ScctArmouryPreviewStage.h), which it makes in the world the menu is over and removes when the page goes. The
 * model turns slowly; the mouse drags it round, and it starts turning again 2.5 s after the drag. Nothing is drawn
 * while the stage has no model (the imported characters are missing): the page shows the item's flat picture then.
 */
class SScctArmouryPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctArmouryPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctArmouryPreview() override;

	/** Shows a slot of a side as AScctArmouryPreviewStage::Show takes it. */
	void Show(int32 Side, int32 Slot, const FString& PatternItem, const FString& LightItem);

	bool HasModel() const;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	AScctArmouryPreviewStage* Stage();

	TWeakObjectPtr<AScctArmouryPreviewStage> StagePtr;
	FSlateBrush Brush;
	bool bDragging = false;
	float Rest = 0.0f;
};

/** The flat pictures of cosmetics for the item list (the Godot client's CosmeticThumbs): made once per file and kept. */
namespace ScctArmouryThumbs
{
	/** The brush of an item's flat picture (the content builder's thumb of its pattern, or its camo texture), or null when the content has none: the colour swatch is shown then. */
	const FSlateBrush* For(const FString& ItemId);
}
