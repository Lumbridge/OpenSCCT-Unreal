#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctMapApi.h"
#include "Widgets/SLeafWidget.h"

/** Orthographic cutaway of the round's static surfaces. All projection inputs use simulation XYZ. */
class SScctStrategicMap3D : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctStrategicMap3D) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, const FScctMapState* InState, const TArray<FScctMapPlace>* InPlaces, TWeakObjectPtr<UWorld> InWorld);
	void ReadFloor(const FScctMapApi& Api);
	bool HasGeometry() const { return !Faces.IsEmpty(); }
	void ResetCamera();
	bool Navigate(const FKey& Key);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(620, 620); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle&, bool) const override;
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override;

private:
	struct FFace
	{
		FScctMapTriangle Triangle;
		FVector3f Normal;
		FColor Colour;
	};
	struct FVisibleFace
	{
		int32 Index;
		float Depth;
	};
	struct FBatch
	{
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
	};

	void Orbit(float YawDelta, float PitchDelta);
	void ZoomAt(float Steps, FVector2f OffsetFromCentre);
	void PanBy(FVector2f Delta);
	void SetAxes();
	FVector2f Project(FVector3f Point, FVector2f Size) const;
	void PrepareBatches(const FGeometry& Geometry) const;
	void PaintMarkers(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;

	const FScctMapState* State = nullptr;
	const TArray<FScctMapPlace>* Places = nullptr;
	TWeakObjectPtr<UWorld> World;
	TArray<FFace> Faces;
	FVector3f Pivot = FVector3f::ZeroVector;
	float Radius = 1.0f;
	int32 LoadedFloor = INDEX_NONE;

	float Yaw = 0, Pitch = 0, Zoom = 1;
	FVector2f Pan = FVector2f::ZeroVector;
	FVector3f Right, Down, Eye;
	FKey DragButton;
	bool bPanning = false;

	// Sort only on orbit/floor changes. Keep projected batches across the marker refreshes.
	mutable bool bDepthDirty = true, bProjectionDirty = true;
	mutable TArray<FVisibleFace> Visible;
	mutable TArray<FBatch> Batches;
	mutable FVector2f CachedSize = FVector2f::ZeroVector;
	mutable FSlateRenderTransform CachedTransform;
};
