#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"

class FSlateWindowElementList;

/**
 * Draws the HUD's shapes into a Slate element list: glass panels, text, lines and arcs. Every HUD element paints
 * through this, so they share one look and nothing else touches Slate's draw calls.
 *
 * Positions and sizes are in the local units of the widget being painted. Each call draws above the one before
 * (Slate only promises an order between layers, so every shape takes a new layer).
 */
class FScctHudPainter
{
public:
	enum class EAlign : uint8
	{
		Left,
		Centre,
		Right,
	};

	FScctHudPainter(FSlateWindowElementList& InElements, const FGeometry& InGeometry, int32 InLayer);

	/** The size of the area being painted. */
	FVector2f Size() const { return FVector2f(Geometry.GetLocalSize()); }

	/** The layer of the last shape drawn: what OnPaint returns. */
	int32 GetLayer() const { return Layer; }

	/** Everything drawn from now on is multiplied by this (a block fading in or out sets it, then puts 1 back). */
	void SetOpacity(float InOpacity) { Opacity = FMath::Clamp(InOpacity, 0.0f, 1.0f); }
	float GetOpacity() const { return Opacity; }

	/** Blurs what has been drawn so far inside the box (the game picture and anything painted below). */
	void Blur(const FBox2f& Box, float Strength, float Corner = 0.0f);

	/** A filled box, with rounded corners and an outline when asked. */
	void Fill(const FBox2f& Box, const FLinearColor& Colour, float Corner = 0.0f, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.0f);

	/** A box that runs from one colour to another, left to right or top to bottom. */
	void Gradient(const FBox2f& Box, const FLinearColor& From, const FLinearColor& To, bool bVertical, float Corner = 0.0f);

	/** The HUD's panel: blurred background, dark tint, a hairline edge. */
	void Glass(const FBox2f& Box, float Corner, float Tint = 0.42f);

	/** One line of text; returns its size. The position is the top of the line at its left, centre or right. */
	FVector2f Text(const FVector2f& Position, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour, EAlign Align = EAlign::Left, bool bShadow = true);

	FVector2f Measure(const FString& String, const FSlateFontInfo& Font) const;

	/** Text broken at spaces to fit Width, at most MaxLines lines (the last one ends in an ellipsis if cut). Returns the height used. */
	float Paragraph(const FVector2f& Position, float Width, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour, EAlign Align = EAlign::Left, int32 MaxLines = 4, float LineGap = 3.0f);

	/** The lines Paragraph would draw. */
	void Wrap(const FString& String, const FSlateFontInfo& Font, float Width, int32 MaxLines, TArray<FString>& OutLines) const;

	void Line(const FVector2f& From, const FVector2f& To, const FLinearColor& Colour, float Thickness = 1.0f);
	void Polyline(TArray<FVector2f> Points, const FLinearColor& Colour, float Thickness = 1.0f);

	/**
	 * Part of a circle. Angles are in degrees, clockwise from the top (as a clock reads), and the arc runs from
	 * StartDegrees to EndDegrees. Glow adds a soft wider stroke underneath.
	 */
	void Arc(const FVector2f& Centre, float Radius, float StartDegrees, float EndDegrees, const FLinearColor& Colour, float Thickness = 1.5f, float Glow = 0.0f);

	/** A line with a soft wider stroke underneath: the HUD's "lit" accent. */
	void GlowLine(const FVector2f& From, const FVector2f& To, const FLinearColor& Colour, float Thickness = 1.5f);

private:
	FPaintGeometry BoxGeometry(const FBox2f& Box) const;

	FSlateWindowElementList& Elements;
	const FGeometry& Geometry;
	int32 Layer;
	float Opacity = 1.0f;
};
