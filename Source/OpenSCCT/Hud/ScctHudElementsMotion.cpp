#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudTextures.h"

// The merc's motion tracking, from the rows the bridge built for this frame's view (ScctHudMotion.cpp): the goggles'
// lock boxes (four Vision_lock corners zooming from the whole frame onto the target, then held with the balayage_H sweep
// climbing the box once a second), the laser target's bracket with the friend marks, Hold Fire and the typed name, and
// a friend's speed and distance under his box. Every row is drawn as it comes, in its canvas colour, which the original's
// canvas draws doubled.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;

namespace
{
	/** A canvas colour as the original's HUD shows it: each channel doubled (capped), the alpha as is, in sRGB. */
	FLinearColor CanvasColour(const FScctHudMotionRow& Row)
	{
		return FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>(FMath::Min(255, Row.R * 2)),
			static_cast<uint8>(FMath::Min(255, Row.G * 2)),
			static_cast<uint8>(FMath::Min(255, Row.B * 2)))).CopyWithNewOpacity(Row.A / 255.0f);
	}

	const TCHAR* TextureOf(int32 Kind)
	{
		switch (Kind)
		{
		case EScctHudMotionKind::Lock: return TEXT("Vision_lock");
		case EScctHudMotionKind::Sweep: return TEXT("balayage_H");
		default: return TEXT("Vision_friend");
		}
	}

	/** A row whose picture the content lacks: a corner as two strokes along its outer edges, a band or mark as a line. */
	void PaintPlain(FScctHudPainter& Painter, const FScctHudMotionRow& Row, const FLinearColor& Colour)
	{
		const FVector2f Start(Row.X, Row.Y);
		const FVector2f Far(Row.X + Row.XL, Row.Y + Row.YL);
		if (Row.Kind == EScctHudMotionKind::Lock)
		{
			const FVector2f Along(Row.X + Row.XL * 0.5f, Row.Y);
			const FVector2f Down(Row.X, Row.Y + Row.YL * 0.5f);
			Painter.Line(Start, Along, Colour, 2.0f);
			Painter.Line(Start, Down, Colour, 2.0f);
		}
		else if (Row.Kind == EScctHudMotionKind::Sweep)
		{
			Painter.Line(Start, FVector2f(Far.X, Start.Y), Colour, 2.0f);
		}
		else
		{
			Painter.Line(Start, Far, Colour, 2.0f);
		}
	}
}

void ScctHudElements::PaintMotionTracking(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.MotionRows.Num() == 0)
	{
		return;
	}

	const UScctHudTextures::FEntry* Textures[3] = {
		UScctHudTextures::Find(TEXT("SHudTextures"), TextureOf(0)),
		UScctHudTextures::Find(TEXT("SHudTextures"), TextureOf(1)),
		UScctHudTextures::Find(TEXT("SHudTextures"), TextureOf(2)),
	};
	for (const FScctHudMotionRow& Row : Model.MotionRows)
	{
		const FLinearColor Colour = CanvasColour(Row);
		if (Row.Kind == EScctHudMotionKind::Text)
		{
			// The canvas fonts are not in the content: the HUD's own font at the line height the frame gives (9 or 12
			// frame units), placed by its top left as the canvas places it.
			FString Words(Row.Text);
			const float Size = FMath::Max(Row.YL * 0.72f, 6.0f);
			Painter.Text(FVector2f(Row.X, Row.Y), Words, Font(EWeight::Regular, Size), Colour, EAlign::Left, false);
			continue;
		}

		const UScctHudTextures::FEntry* Texture = Textures[FMath::Clamp(Row.Kind, 0, 2)];
		if (Texture == nullptr)
		{
			PaintPlain(Painter, Row, Colour);
			continue;
		}
		const FBox2f Box(FVector2f(Row.X, Row.Y), FVector2f(Row.X + Row.XL, Row.Y + Row.YL));
		Painter.Image(Box, Texture->Brush, Texture->Region(Row.U, Row.V, Row.U + Row.UL, Row.V + Row.VL), Colour);
	}
}
