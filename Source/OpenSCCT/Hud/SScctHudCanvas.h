#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FScctHudModel;

/**
 * One layer of the HUD: a widget that paints its elements straight from the model every frame. It takes no
 * input and has no children; the elements are in Hud/ScctHudElements.h.
 */
class SScctHudCanvas : public SLeafWidget
{
public:
	enum class ELayer : uint8
	{
		/** Over the whole picture: washes, world markers, the crosshair. */
		FullScreen,
		/** Inside the display's safe zone: everything to read. */
		SafeArea,
	};

	SLATE_BEGIN_ARGS(SScctHudCanvas) {}
		SLATE_ARGUMENT(TSharedPtr<const FScctHudModel>, Model)
		SLATE_ARGUMENT(ELayer, Layer)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TSharedPtr<const FScctHudModel> Model;
	ELayer Layer = ELayer::SafeArea;
};
