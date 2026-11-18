#include "Menu/SScctStrategicMap3D.h"

#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudSubsystem.h"
#include "Menu/ScctMenuKit.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float IsoElevation = 35.26439f;
	constexpr int32 FacesPerBatch = 16000; // three vertices per face, including platforms with 16-bit SlateIndex

	void MapLabel(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, FVector2f Anchor,
		const FString& Text, const FSlateFontInfo& Font, FLinearColor Colour)
	{
		const FVector2f Extent(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
		const FVector2f At = Anchor - Extent * 0.5f;
		FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(At + FVector2f(1, 1))),
			Text, Font, ESlateDrawEffect::None, FLinearColor::Black);
		FSlateDrawElement::MakeText(Out, Layer + 1, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(At)),
			Text, Font, ESlateDrawEffect::None, Colour);
	}

	void MapLine(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const TArray<FVector2f>& Points, FLinearColor Colour)
	{
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, FLinearColor::Black, true, 4.0f);
		FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, 2.0f);
	}
}

void SScctStrategicMap3D::Construct(const FArguments&, const FScctMapState* InState, const TArray<FScctMapPlace>* InPlaces, TWeakObjectPtr<UWorld> InWorld)
{
	State = InState;
	Places = InPlaces;
	World = InWorld;
	SetClipping(EWidgetClipping::ClipToBounds);
	SetCanTick(false);
	ResetCamera();
}

void SScctStrategicMap3D::ReadFloor(const FScctMapApi& Api)
{
	if (LoadedFloor == State->Floor) return;
	LoadedFloor = State->Floor;
	TArray<FScctMapTriangle> Triangles;
	Triangles.SetNumUninitialized(Api.ListGeometry(nullptr, 0));
	const int32 Written = Api.ListGeometry(Triangles.GetData(), Triangles.Num());
	Triangles.SetNum(FMath::Clamp(Written, 0, Triangles.Num()), EAllowShrinking::No);

	Faces.Reset(Triangles.Num());
	FBox3f Bounds(ForceInit), FloorBounds(ForceInit);
	for (const FScctMapTriangle& T : Triangles)
	{
		const FVector3f N = FVector3f::CrossProduct(T.B - T.A, T.C - T.A).GetSafeNormal();
		if (N.IsNearlyZero()) continue;
		Bounds += T.A;
		Bounds += T.B;
		Bounds += T.C;
		if (N.Z > 0.65f)
		{
			FloorBounds += T.A;
			FloorBounds += T.B;
			FloorBounds += T.C;
		}
		Faces.Add({T, N, FColor::White});
	}
	if (Bounds.IsValid)
	{
		// Frame the floors. Distant enclosure walls stay in the model without shrinking the playable area.
		const FBox3f& Framing = FloorBounds.IsValid ? FloorBounds : Bounds;
		Pivot = Framing.GetCenter();
		float HalfWidth = 1.0f, HalfHeight = 1.0f;
		const float HeightRange = FMath::Max(1.0f, Bounds.Max.Z - Bounds.Min.Z);
		const FLinearColor Shadow(FColor(18, 42, 51)), Highlight(FColor(145, 191, 190));
		for (FFace& Face : Faces)
		{
			const FScctMapTriangle& T = Face.Triangle;
			const FVector3f& N = Face.Normal;
			const float Height = ((T.A.Z + T.B.Z + T.C.Z) / 3.0f - Bounds.Min.Z) / HeightRange;
			const float Light = FMath::Clamp(0.24f + 0.36f * N.Z + 0.15f * N.X - 0.21f * N.Y + 0.21f * Height, 0.0f, 1.0f);
			Face.Colour = FMath::Lerp(Shadow, Highlight, Light).ToFColorSRGB();
			if (FloorBounds.IsValid && N.Z <= 0.65f) continue;
			for (const FVector3f& P : {T.A, T.B, T.C})
			{
				const FVector3f D = P - Pivot;
				// Fit at the reset orientation, so orbit and reset always use the same scale.
				constexpr float InvSqrt2 = 0.70710678f;
				HalfWidth = FMath::Max(HalfWidth, FMath::Abs((D.Y - D.X) * InvSqrt2));
				HalfHeight = FMath::Max(HalfHeight, FMath::Abs(-(D.X + D.Y) * 0.40824829f - D.Z * 0.81649658f));
			}
		}
		Radius = FMath::Max(HalfWidth, HalfHeight);
	}
	Pan = FVector2f::ZeroVector;
	bDepthDirty = bProjectionDirty = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SScctStrategicMap3D::ResetCamera()
{
	Yaw = -135.0f;
	Pitch = IsoElevation;
	Zoom = 1.0f;
	Pan = FVector2f::ZeroVector;
	SetAxes();
}

void SScctStrategicMap3D::SetAxes()
{
	float SY, CY, SP, CP;
	FMath::SinCos(&SY, &CY, FMath::DegreesToRadians(Yaw));
	FMath::SinCos(&SP, &CP, FMath::DegreesToRadians(Pitch));
	Right = FVector3f(SY, -CY, 0);
	Down = FVector3f(CY * SP, SY * SP, -CP);
	Eye = FVector3f(CY * CP, SY * CP, SP);
	bDepthDirty = bProjectionDirty = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SScctStrategicMap3D::Orbit(float YawDelta, float PitchDelta)
{
	Yaw = FMath::UnwindDegrees(Yaw + YawDelta);
	Pitch = FMath::Clamp(Pitch + PitchDelta, 15.0f, 82.0f);
	SetAxes();
}

void SScctStrategicMap3D::PanBy(FVector2f Delta)
{
	const FVector2f Size = GetCachedGeometry().GetLocalSize();
	const float Limit = FMath::Max(1.0f, FMath::Min(Size.X, Size.Y)) * (Zoom + 0.5f) * 0.5f;
	Pan.X = FMath::Clamp(Pan.X + Delta.X, -Limit, Limit);
	Pan.Y = FMath::Clamp(Pan.Y + Delta.Y, -Limit, Limit);
	bProjectionDirty = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SScctStrategicMap3D::ZoomAt(float Steps, FVector2f OffsetFromCentre)
{
	const float Before = Zoom;
	Zoom = FMath::Clamp(Zoom * FMath::Pow(1.2f, Steps), 0.55f, 10.0f);
	PanBy((OffsetFromCentre - Pan) * (1.0f - Zoom / Before));
}

bool SScctStrategicMap3D::Navigate(const FKey& Key)
{
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left) PanBy(FVector2f(32, 0));
	else if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right) PanBy(FVector2f(-32, 0));
	else if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up) PanBy(FVector2f(0, 32));
	else if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down) PanBy(FVector2f(0, -32));
	else if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder) Orbit(-10, 0);
	else if (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder) Orbit(10, 0);
	else if (Key == EKeys::Equals || Key == EKeys::Add || Key == EKeys::Gamepad_RightTrigger) ZoomAt(1, FVector2f::ZeroVector);
	else if (Key == EKeys::Hyphen || Key == EKeys::Subtract || Key == EKeys::Gamepad_LeftTrigger) ZoomAt(-1, FVector2f::ZeroVector);
	else if (Key == EKeys::Home || Key == EKeys::Gamepad_RightThumbstick) ResetCamera();
	else return false;
	return true;
}

FReply SScctStrategicMap3D::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	const FKey Button = Event.GetEffectingButton();
	if (Button != EKeys::LeftMouseButton && Button != EKeys::MiddleMouseButton) return FReply::Unhandled();
	ScctUi::NoteInput(Button);
	DragButton = Button;
	bPanning = Button == EKeys::MiddleMouseButton || Event.IsShiftDown();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SScctStrategicMap3D::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (!HasMouseCapture() || Event.GetEffectingButton() != DragButton) return FReply::Unhandled();
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SScctStrategicMap3D::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!HasMouseCapture()) return FReply::Unhandled();
	const FVector2f Delta = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()) - Geometry.AbsoluteToLocal(Event.GetLastScreenSpacePosition());
	if (bPanning) PanBy(Delta);
	else Orbit(Delta.X * 0.4f, Delta.Y * 0.25f);
	return FReply::Handled();
}

FReply SScctStrategicMap3D::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	ScctUi::NoteInput(EKeys::MouseWheelAxis);
	const FVector2f Offset = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()) - Geometry.GetLocalSize() * 0.5f;
	ZoomAt(Event.GetWheelDelta(), Offset);
	return FReply::Handled();
}

FVector2f SScctStrategicMap3D::Project(FVector3f Point, FVector2f Size) const
{
	const FVector3f D = Point - Pivot;
	const float Scale = FMath::Min(Size.X, Size.Y) * (0.47f * Zoom / Radius);
	return Size * 0.5f + Pan + FVector2f(D | Right, D | Down) * Scale;
}

void SScctStrategicMap3D::PrepareBatches(const FGeometry& Geometry) const
{
	const FVector2f Size = Geometry.GetLocalSize();
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
	if (!bProjectionDirty && CachedSize == Size && CachedTransform == Transform) return;
	if (bDepthDirty)
	{
		Visible.Reset(Faces.Num());
		for (int32 I = 0; I < Faces.Num(); ++I)
		{
			const FFace& Face = Faces[I];
			if ((Face.Normal | Eye) <= 0.0f) continue;
			const FScctMapTriangle& T = Face.Triangle;
			Visible.Add({I, (T.A + T.B + T.C) | Eye});
		}
		Visible.Sort([](const FVisibleFace& A, const FVisibleFace& B) { return A.Depth < B.Depth; });
		bDepthDirty = false;
	}

	Batches.SetNum(FMath::DivideAndRoundUp(Visible.Num(), FacesPerBatch));
	for (int32 Index = 0; Index < Batches.Num(); ++Index)
	{
		FBatch& Batch = Batches[Index];
		const int32 Start = Index * FacesPerBatch;
		const int32 Count = FMath::Min(Visible.Num() - Start, FacesPerBatch);
		Batch.Vertices.Reset(Count * 3);
		Batch.Indices.Reset(Count * 3);
		for (int32 I = Start; I < Start + Count; ++I)
		{
			const FFace& Face = Faces[Visible[I].Index];
			const FScctMapTriangle& T = Face.Triangle;
			for (const FVector3f& P : { T.A, T.B, T.C })
			{
				Batch.Indices.Add(static_cast<SlateIndex>(Batch.Vertices.Num()));
				Batch.Vertices.Add(FSlateVertex::Make(Transform, Project(P, Size), FVector2f(0.5f, 0.5f), Face.Colour));
			}
		}
	}
	CachedSize = Size;
	CachedTransform = Transform;
	bProjectionDirty = false;
}

int32 SScctStrategicMap3D::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle&, bool) const
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	FSlateDrawElement::MakeBox(Out, Layer++, Geometry.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(FColor(12, 23, 28)));
	PrepareBatches(Geometry);
	const auto Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
	for (const FBatch& Batch : Batches)
	{
		FSlateDrawElement::MakeCustomVerts(Out, Layer++, Handle, Batch.Vertices, Batch.Indices, nullptr, 0, 0);
	}
	PaintMarkers(Geometry, Out, Layer);
	return Layer + 8;
}

void SScctStrategicMap3D::PaintMarkers(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const
{
	const FVector2f Size = Geometry.GetLocalSize();
	const FScctMapState& S = *State;
	const FSlateFontInfo Font = ScctHudStyle::Font(ScctHudStyle::EWeight::Bold, 10);
	for (const FScctMapPlace& Place : *Places)
	{
		if (Place.Floor == S.Floor)
		{
			MapLabel(Geometry, Out, Layer, Project(Place.World, Size), FString::FromInt(Place.Number), Font, FLinearColor::White);
		}
	}

	// Reuse the local HUD's resolved objective rows, including zone, mode, visibility and visor rules.
	if (!S.Live) return;
	const UScctHudSubsystem* Hud = World.IsValid() ? World->GetSubsystem<UScctHudSubsystem>() : nullptr;
	if (Hud)
	{
		const FScctHudModel& M = Hud->GetModel();
		if (ScctHudElements::ShowsPlayerHud(M) && (M.State.Side != 0 || (M.State.Flags & EScctHudFlags::EnhancedReality) != 0))
		{
			for (const FScctHudObjective& Objective : M.Objectives)
			{
				if (Objective.Z < S.FloorMinZ || Objective.Z >= S.FloorMaxZ) continue;
				const FVector2f At = Project(FVector3f(Objective.X, Objective.Y, Objective.Z), Size);
				FLinearColor Colour = ScctHudStyle::Accent(M.State.Side);
				if (Objective.Flags & (EScctHudObjectiveFlags::Alert | EScctHudObjectiveFlags::Triggering)) Colour = ScctHudStyle::Danger();
				else if (Objective.Flags & EScctHudObjectiveFlags::Locked) Colour = FLinearColor(FColor(220, 220, 40));
				if (Objective.Flags & EScctHudObjectiveFlags::Taken) Colour.A = 0.4f;
				MapLine(Geometry, Out, Layer + 2, {At + FVector2f(0,-6), At + FVector2f(6,0), At + FVector2f(0,6), At + FVector2f(-6,0), At + FVector2f(0,-6)}, Colour);
				if (Objective.Kind != 3)
				{
					MapLabel(Geometry, Out, Layer + 4, At + FVector2f(0, -15), M.Text(Objective.NameId), Font, Colour);
				}
			}
		}
	}

	// Without a calibrated picture the 3D view can still place the same authorized team samples on their floor.
	const bool bCalibrated = S.HasCamera && S.FloorCount > 0;
	const FLinearColor Mate = S.Bright ? FLinearColor(FColor(254, 183, 75)) : FLinearColor(FColor(168, 112, 45));
	for (int32 I = 0; I < S.MateCount; ++I)
	{
		if (S.MateFloor[I] != S.Floor || (bCalibrated && !S.MateShown[I])) continue;
		const FVector2f At = Project(FVector3f(S.MateX[I], S.MateY[I], S.MateZ[I]), Size);
		TArray<FVector2f> Ring;
		for (int32 N = 0; N <= 16; ++N)
		{
			const float Angle = UE_TWO_PI * N / 16.0f;
			Ring.Add(At + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * 6.0f);
		}
		MapLine(Geometry, Out, Layer + 6, Ring, Mate);
	}
	if (S.HasYou && S.YouFloor == S.Floor && (!bCalibrated || S.YouShown))
	{
		const FVector2f At = Project(FVector3f(S.YouX, S.YouY, S.YouZ), Size);
		const float Angle = S.YouYaw * (UE_TWO_PI / 65536.0f);
		const FVector3f Heading(FMath::Cos(Angle), FMath::Sin(Angle), 0);
		const FVector2f Forward = FVector2f(Heading | Right, Heading | Down).GetSafeNormal();
		const FVector2f Wing(-Forward.Y, Forward.X);
		MapLine(Geometry, Out, Layer + 6, {At + Forward * 11, At - Forward * 6 + Wing * 6, At - Forward * 3,
			At - Forward * 6 - Wing * 6, At + Forward * 11}, S.Bright ? FLinearColor::White : FLinearColor(FColor(170, 186, 185)));
	}
}
