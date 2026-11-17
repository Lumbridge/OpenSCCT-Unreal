#include "Hud/ScctHudPainter.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudStyle.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/SlateBrush.h"

FScctHudPainter::FScctHudPainter(FSlateWindowElementList& InElements, const FGeometry& InGeometry, int32 InLayer)
	: Elements(InElements)
	, Geometry(InGeometry)
	, Layer(InLayer)
{
}

FPaintGeometry FScctHudPainter::BoxGeometry(const FBox2f& Box) const
{
	return Geometry.ToPaintGeometry(Box.GetSize(), FSlateLayoutTransform(Box.Min));
}

void FScctHudPainter::Blur(const FBox2f& Box, float Strength, float Radius)
{
	Strength *= Opacity;
	if (Strength < 0.5f || Box.GetSize().GetMin() <= 0.0f)
	{
		return;
	}

	// As SBackgroundBlur does it: the blur pass works in window pixels, on a smaller copy when the kernel is wide.
	const float Scale = Geometry.GetAccumulatedLayoutTransform().GetScale();
	const FVector2f TopLeft = Geometry.LocalToAbsolute(Box.Min);
	const FVector2f PixelSize = Box.GetSize() * Scale;
	const FPaintGeometry PaintGeometry(TopLeft, PixelSize, Scale);

	int32 KernelSize = FMath::RoundToInt32(Strength * 3.0f);
	int32 Downsample = 0;
	if (KernelSize > 9)
	{
		Downsample = KernelSize >= 64 ? 4 : 2;
		KernelSize /= Downsample;
	}
	KernelSize = FMath::Clamp(KernelSize | 1, 3, 255);

	int32 Width = FMath::RoundToInt32(PixelSize.X);
	int32 Height = FMath::RoundToInt32(PixelSize.Y);
	float ComputedStrength = Strength;
	if (Downsample > 0)
	{
		Width = FMath::DivideAndRoundUp(Width, Downsample);
		Height = FMath::DivideAndRoundUp(Height, Downsample);
		ComputedStrength /= Downsample;
	}
	if (Width <= 0 || Height <= 0)
	{
		return;
	}

	// The blur pass draws with its viewport set to the box, and the renderer wants the scissor inside that
	// viewport: without a clip of the same box the scissor is the whole window and D3D12 raises an ensure
	// ("Scissor invalid with current Viewport"). SBackgroundBlur clips to itself for the same reason.
	Elements.PushClip(FSlateClippingZone(Geometry.MakeChild(Box.GetSize(), FSlateLayoutTransform(Box.Min))));
	FSlateDrawElement::MakePostProcessBlur(Elements, ++Layer, PaintGeometry,
		FVector4f(static_cast<float>(KernelSize), ComputedStrength, static_cast<float>(Width), static_cast<float>(Height)),
		Downsample, FVector4f(Radius * Scale));
	Elements.PopClip();
}

void FScctHudPainter::Fill(const FBox2f& Box, const FLinearColor& Colour, float Radius, const FLinearColor& Outline, float OutlineWidth)
{
	if (Box.GetSize().GetMin() <= 0.0f)
	{
		return;
	}

	// The draw element copies what it needs from the brush, so a brush on the stack is enough. The fill's colour
	// is the element's tint (a brush's own tint is for widgets to apply); the outline's comes from the brush.
	const FSlateRoundedBoxBrush Brush(FLinearColor::White, Radius, ScctHudStyle::Fade(Outline, Opacity), OutlineWidth);
	FSlateDrawElement::MakeBox(Elements, ++Layer, BoxGeometry(Box), &Brush, ESlateDrawEffect::None, ScctHudStyle::Fade(Colour, Opacity));
}

void FScctHudPainter::Gradient(const FBox2f& Box, const FLinearColor& From, const FLinearColor& To, bool bVertical, float Radius)
{
	if (Box.GetSize().GetMin() <= 0.0f)
	{
		return;
	}

	TArray<FSlateGradientStop> Stops;
	Stops.Emplace(FVector2f::ZeroVector, ScctHudStyle::Fade(From, Opacity));
	Stops.Emplace(Box.GetSize(), ScctHudStyle::Fade(To, Opacity));
	FSlateDrawElement::MakeGradient(Elements, ++Layer, BoxGeometry(Box), MoveTemp(Stops), bVertical ? Orient_Horizontal : Orient_Vertical,
		ESlateDrawEffect::None, FVector4f(Radius));
}

void FScctHudPainter::Glass(const FBox2f& Box, float Radius, float Tint)
{
	Blur(Box, 16.0f, Radius);
	Fill(Box, FLinearColor(0.004f, 0.007f, 0.011f, Tint), Radius, FLinearColor(1.0f, 1.0f, 1.0f, 0.09f), 1.0f);
}

FVector2f FScctHudPainter::Measure(const FString& String, const FSlateFontInfo& Font) const
{
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return FVector2f(FontMeasure->Measure(String, Font));
}

FVector2f FScctHudPainter::Text(const FVector2f& Position, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour, EAlign Align, bool bShadow)
{
	if (String.IsEmpty())
	{
		return FVector2f::ZeroVector;
	}

	const FVector2f TextSize = Measure(String, Font);
	FVector2f TopLeft = Position;
	if (Align == EAlign::Centre)
	{
		TopLeft.X -= TextSize.X * 0.5f;
	}
	else if (Align == EAlign::Right)
	{
		TopLeft.X -= TextSize.X;
	}
	// Whole units keep thin type crisp.
	TopLeft.X = FMath::RoundToFloat(TopLeft.X);
	TopLeft.Y = FMath::RoundToFloat(TopLeft.Y);

	const FLinearColor Final = ScctHudStyle::Fade(Colour, Opacity);
	if (bShadow)
	{
		// A soft dark copy underneath keeps text readable over a bright picture.
		const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.55f * Final.A);
		FSlateDrawElement::MakeText(Elements, ++Layer, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TopLeft + FVector2f(0.0f, 1.0f))),
			String, Font, ESlateDrawEffect::None, Shadow);
	}
	FSlateDrawElement::MakeText(Elements, ++Layer, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TopLeft)),
		String, Font, ESlateDrawEffect::None, Final);
	return TextSize;
}

void FScctHudPainter::Wrap(const FString& String, const FSlateFontInfo& Font, float Width, int32 MaxLines, TArray<FString>& OutLines) const
{
	OutLines.Reset();
	TArray<FString> Words;
	String.ParseIntoArrayWS(Words);

	FString Current;
	for (int32 Index = 0; Index < Words.Num(); ++Index)
	{
		const FString Candidate = Current.IsEmpty() ? Words[Index] : Current + TEXT(" ") + Words[Index];
		if (Current.IsEmpty() || Measure(Candidate, Font).X <= Width)
		{
			Current = Candidate;
			continue;
		}

		if (OutLines.Num() == MaxLines - 1)
		{
			// No room for another line: the rest is cut here.
			Current += TEXT("…");
			OutLines.Add(MoveTemp(Current));
			return;
		}
		OutLines.Add(MoveTemp(Current));
		Current = Words[Index];
	}
	if (!Current.IsEmpty())
	{
		OutLines.Add(MoveTemp(Current));
	}
}

float FScctHudPainter::Paragraph(const FVector2f& Position, float Width, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour, EAlign Align, int32 MaxLines, float LineGap)
{
	TArray<FString> Lines;
	Wrap(String, Font, Width, MaxLines, Lines);

	const float LineHeight = Measure(TEXT("Ag"), Font).Y + LineGap;
	FVector2f Cursor = Position;
	for (const FString& Line : Lines)
	{
		Text(Cursor, Line, Font, Colour, Align);
		Cursor.Y += LineHeight;
	}
	return Lines.Num() > 0 ? Lines.Num() * LineHeight - LineGap : 0.0f;
}

void FScctHudPainter::Polyline(TArray<FVector2f> Points, const FLinearColor& Colour, float Thickness)
{
	if (Points.Num() < 2)
	{
		return;
	}
	FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None,
		ScctHudStyle::Fade(Colour, Opacity), true, Thickness);
}

void FScctHudPainter::Line(const FVector2f& From, const FVector2f& To, const FLinearColor& Colour, float Thickness)
{
	Polyline({ From, To }, Colour, Thickness);
}

void FScctHudPainter::GlowLine(const FVector2f& From, const FVector2f& To, const FLinearColor& Colour, float Thickness)
{
	Line(From, To, ScctHudStyle::Fade(Colour, 0.10f), Thickness + 6.0f);
	Line(From, To, ScctHudStyle::Fade(Colour, 0.22f), Thickness + 2.5f);
	Line(From, To, Colour, Thickness);
}

void FScctHudPainter::Image(const FBox2f& Box, const FSlateBrush& Brush, const FBox2f& UV, const FLinearColor& Tint)
{
	// A mirrored box is drawn the right way round with the region's U swapped.
	FBox2f Drawn = Box;
	FBox2f Region = UV;
	if (Drawn.Max.X < Drawn.Min.X)
	{
		Swap(Drawn.Min.X, Drawn.Max.X);
		Swap(Region.Min.X, Region.Max.X);
	}
	if (Drawn.Max.Y < Drawn.Min.Y)
	{
		Swap(Drawn.Min.Y, Drawn.Max.Y);
		Swap(Region.Min.Y, Region.Max.Y);
	}
	const FLinearColor Final = ScctHudStyle::Fade(Tint, Opacity);
	if (Drawn.GetSize().GetMin() <= 0.0f || Final.A <= 0.002f || Brush.GetResourceObject() == nullptr)
	{
		return;
	}

	// The draw element copies the brush, so a copy on the stack carries the region.
	FSlateBrush Part = Brush;
	Part.SetUVRegion(Region);
	FSlateDrawElement::MakeBox(Elements, ++Layer, BoxGeometry(Drawn), &Part, ESlateDrawEffect::None, Final);
}

void FScctHudPainter::Arc(const FVector2f& Centre, float Radius, float StartDegrees, float EndDegrees, const FLinearColor& Colour, float Thickness, float Glow)
{
	const float Sweep = EndDegrees - StartDegrees;
	if (FMath::Abs(Sweep) < 0.05f || Radius <= 0.0f)
	{
		return;
	}

	// Enough segments that the curve reads as round at any size.
	const int32 Segments = FMath::Clamp(FMath::CeilToInt32(FMath::Abs(Sweep) * FMath::Max(Radius, 8.0f) / 240.0f), 4, 96);
	TArray<FVector2f> Points;
	Points.Reserve(Segments + 1);
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(StartDegrees + Sweep * Index / Segments);
		Points.Emplace(Centre.X + FMath::Sin(Angle) * Radius, Centre.Y - FMath::Cos(Angle) * Radius);
	}

	if (Glow > 0.0f)
	{
		Polyline(Points, ScctHudStyle::Fade(Colour, 0.10f * Glow), Thickness + 6.0f);
		Polyline(Points, ScctHudStyle::Fade(Colour, 0.22f * Glow), Thickness + 2.5f);
	}
	Polyline(MoveTemp(Points), Colour, Thickness);
}
