#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/** One sector on the floor, already projected: the centre, then the rim from one edge to the other. */
struct FScctGrabAreaSector
{
	static constexpr int32 Segments = 24;
	static constexpr int32 PointCount = Segments + 2;

	/** In the overlay's own units (viewport pixels over the DPI scale). */
	FVector2f Points[PointCount];

	/** False for a point behind the camera: the triangles and edges that touch it are left out. */
	bool bInFront[PointCount] = {};
};

/** One mercenary as the overlay draws him this frame. */
struct FScctGrabAreaDrawn
{
	/** What Use would do now (EScctGrabAreaState). */
	int32 State = 0;

	/** Where a spy facing straight at him can grab: drawn bright with an edge. */
	FScctGrabAreaSector Facing;

	/** Where he still can with his body turned by up to the front cone: drawn faint underneath. */
	FScctGrabAreaSector Turned;

	/** A short line along the way his body faces. */
	FVector2f LineFrom = FVector2f::ZeroVector;
	FVector2f LineTo = FVector2f::ZeroVector;
	bool bLine = false;
};

/** What the overlay paints, filled by UScctGrabAreaSubsystem every frame. */
struct FScctGrabAreaModel
{
	/** The sectors and the legend are drawn. */
	bool bShown = false;
	TArray<FScctGrabAreaDrawn> Areas;

	/** A short line in the middle of the screen (why the overlay was refused), until NoticeUntil (platform seconds). */
	FString Notice;
	double NoticeUntil = 0.0;
};

/**
 * The practice grab-area overlay (Godot's game/Scripts/Play/GrabAreaOverlay.cs): sectors on the floor around every
 * living mercenary, painted in 2D from projected points so they show through walls and do not depend on the world
 * renderer, and a legend line at the bottom left. Green is a mercenary Use grabs now, yellow one the player is not
 * in position for, red one who cannot be grabbed now. It sits in the viewport just below the HUD, takes no input
 * and has no children.
 */
class SScctGrabAreaOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctGrabAreaOverlay) {}
		SLATE_ARGUMENT(TSharedPtr<const FScctGrabAreaModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	/** The colours of the three states, as the Godot overlay has them (sRGB). */
	static FColor StateColour(int32 State);

private:
	void PaintSector(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32& LayerId, const FScctGrabAreaSector& Sector, FColor Fill, bool bOutline) const;

	TSharedPtr<const FScctGrabAreaModel> Model;
};
