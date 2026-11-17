#include "Test/SScctGrabAreaOverlay.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Test/ScctGrabAreaApi.h"

namespace
{
	const TCHAR* const Legend =
		TEXT("grabarea: green = Use grabs now, yellow = not in position, red = cannot be grabbed (just got up, shocked or busy). ")
		TEXT("Bright = facing him, faint = with your body turned.");

	FColor WithAlpha(FColor Colour, float Alpha)
	{
		Colour.A = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Alpha * 255.0f), 0, 255));
		return Colour;
	}

	/** Slate tints are linear; the overlay's colours are written as sRGB like the Godot one's. */
	FLinearColor Tint(FColor Colour)
	{
		return FLinearColor::FromSRGBColor(Colour);
	}

	void PaintText(FSlateWindowElementList& OutDrawElements, int32& LayerId, const FGeometry& Geometry, const FVector2f& TopLeft, const FString& Text, const FSlateFontInfo& Font)
	{
		const FVector2f Size(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
		// A dark copy underneath keeps it readable over a bright picture.
		FSlateDrawElement::MakeText(OutDrawElements, ++LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft + FVector2f(1.0f, 1.0f))),
			Text, Font, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.7f));
		FSlateDrawElement::MakeText(OutDrawElements, ++LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			Text, Font, ESlateDrawEffect::None, FLinearColor::White);
	}
}

void SScctGrabAreaOverlay::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
}

FVector2D SScctGrabAreaOverlay::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// It fills the viewport it is put in.
	return FVector2D::ZeroVector;
}

FColor SScctGrabAreaOverlay::StateColour(int32 State)
{
	switch (State)
	{
	case EScctGrabAreaState::GrabNow:
		return FLinearColor(0.2f, 1.0f, 0.3f).ToFColor(false);
	case EScctGrabAreaState::OutOfPosition:
		return FLinearColor(1.0f, 0.85f, 0.2f).ToFColor(false);
	default:
		return FLinearColor(1.0f, 0.25f, 0.2f).ToFColor(false);
	}
}

void SScctGrabAreaOverlay::PaintSector(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32& LayerId, const FScctGrabAreaSector& Sector, FColor Fill, bool bOutline) const
{
	// A fan of triangles from the centre, each left out when one of its corners is behind the camera.
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	Vertices.Reserve(FScctGrabAreaSector::PointCount);
	for (int32 Index = 0; Index < FScctGrabAreaSector::PointCount; ++Index)
	{
		Vertices.Add(FSlateVertex::Make(Transform, Sector.Points[Index], FVector2f(0.5f, 0.5f), Fill));
	}
	for (int32 Index = 1; Index <= FScctGrabAreaSector::Segments; ++Index)
	{
		if (Sector.bInFront[0] && Sector.bInFront[Index] && Sector.bInFront[Index + 1])
		{
			Indices.Add(0);
			Indices.Add(static_cast<SlateIndex>(Index));
			Indices.Add(static_cast<SlateIndex>(Index + 1));
		}
	}
	if (Indices.Num() > 0)
	{
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, ++LayerId, Handle, Vertices, Indices, nullptr, 0, 0);
	}

	if (!bOutline)
	{
		return;
	}

	// The edge all the way round: centre, the rim, back to the centre.
	const FLinearColor Edge = Tint(WithAlpha(Fill, 0.9f));
	for (int32 Index = 0; Index < FScctGrabAreaSector::PointCount; ++Index)
	{
		const int32 Next = Index == FScctGrabAreaSector::PointCount - 1 ? 0 : Index + 1;
		if (Sector.bInFront[Index] && Sector.bInFront[Next])
		{
			TArray<FVector2f> Points = { Sector.Points[Index], Sector.Points[Next] };
			FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Edge, true, 2.0f);
		}
	}
}

int32 SScctGrabAreaOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!Model.IsValid())
	{
		return LayerId;
	}

	const FVector2f Size(AllottedGeometry.GetLocalSize());
	if (Model->bShown)
	{
		for (const FScctGrabAreaDrawn& Area : Model->Areas)
		{
			const FColor Colour = StateColour(Area.State);
			PaintSector(AllottedGeometry, OutDrawElements, LayerId, Area.Turned, WithAlpha(Colour, 0.12f), false);
			PaintSector(AllottedGeometry, OutDrawElements, LayerId, Area.Facing, WithAlpha(Colour, 0.3f), true);
			if (Area.bLine)
			{
				TArray<FVector2f> Points = { Area.LineFrom, Area.LineTo };
				FSlateDrawElement::MakeLines(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, FLinearColor::White, true, 2.0f);
			}
		}

		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 11);
		const float LineHeight = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(Font);
		PaintText(OutDrawElements, LayerId, AllottedGeometry, FVector2f(16.0f, Size.Y - 16.0f - LineHeight), Legend, Font);
	}

	if (!Model->Notice.IsEmpty() && FPlatformTime::Seconds() < Model->NoticeUntil)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 14);
		const FVector2f TextSize(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Model->Notice, Font));
		// Below the middle, clear of the HUD's banners and headlines in the upper half.
		PaintText(OutDrawElements, LayerId, AllottedGeometry, FVector2f((Size.X - TextSize.X) * 0.5f, Size.Y * 0.64f), Model->Notice, Font);
	}
	return LayerId;
}
