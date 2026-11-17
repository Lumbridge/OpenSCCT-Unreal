#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"
#include "Runtime/ScctTeammateNamesApi.h"

// The names over the local player's teammates (ToggleTeammateNames), on the full-screen layer and so seen through
// walls, as the Godot client's HudTeammateNames draws them: quiet labels behind the crosshair and the objective
// markers, smaller and fainter with distance (full at 3 m, least from 60 m on), times the player's nametag opacity;
// over a merc teammate a sleeping mark while he is knocked out and a backpack mark while he carries one (grey once
// the player's own share in it is spent). UScctHudSubsystem::ProjectNames places them; this only draws.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	/** HudTeammateNames' sleep colour and the grey of a spent backpack. */
	const FLinearColor NamesSleepColour(0.55f, 0.77f, 1.0f, 1.0f);
	const FLinearColor NamesSpentColour(0.3f, 0.3f, 0.3f, 1.0f);

	/** Three Zs rising to the right, smallest last, in a Size box. */
	void PaintNamesSleepMark(FScctHudPainter& Painter, const FBox2f& Box, const FLinearColor& Colour)
	{
		const float Size = Box.GetSize().X;
		const float Steps[3][3] = { { 0.0f, 0.55f, 0.45f }, { 0.38f, 0.28f, 0.33f }, { 0.68f, 0.05f, 0.25f } };
		for (const float* Step : Steps)
		{
			const FVector2f Min = Box.Min + FVector2f(Step[0], Step[1]) * Size;
			const float Z = Step[2] * Size;
			Painter.Polyline({ Min, Min + FVector2f(Z, 0.0f), Min + FVector2f(0.0f, Z), Min + FVector2f(Z, Z) }, Colour, FMath::Max(1.0f, Size * 0.09f));
		}
	}

	/** A pack with its flap, two straps and a pocket, in a Size box. */
	void PaintNamesBackpackMark(FScctHudPainter& Painter, const FBox2f& Box, const FLinearColor& Colour)
	{
		const float Size = Box.GetSize().X;
		const float Line = FMath::Max(1.0f, Size * 0.08f);
		const FBox2f Body(Box.Min + FVector2f(0.18f, 0.22f) * Size, Box.Min + FVector2f(0.82f, 0.98f) * Size);
		Painter.Fill(Body, Colour * FLinearColor(1.0f, 1.0f, 1.0f, 0.35f), Size * 0.12f, Colour, Line);
		// The handle over the top.
		Painter.Polyline({ Box.Min + FVector2f(0.38f, 0.22f) * Size, Box.Min + FVector2f(0.38f, 0.06f) * Size,
			Box.Min + FVector2f(0.62f, 0.06f) * Size, Box.Min + FVector2f(0.62f, 0.22f) * Size }, Colour, Line);
		// The flap and the pocket.
		Painter.Line(Box.Min + FVector2f(0.18f, 0.45f) * Size, Box.Min + FVector2f(0.82f, 0.45f) * Size, Colour, Line);
		Painter.Fill(FBox2f(Box.Min + FVector2f(0.34f, 0.62f) * Size, Box.Min + FVector2f(0.66f, 0.84f) * Size), FLinearColor::Transparent, Size * 0.05f, Colour, Line);
	}
}

void ScctHudElements::PaintTeammateNames(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.NameLabels.Num() == 0)
	{
		return;
	}

	const FLinearColor Colour = Accent(Model.LocalTeam);
	for (const FScctHudNameLabel& NameLabel : Model.NameLabels)
	{
		float Far = FMath::Clamp((NameLabel.Metres - 3.0f) / 57.0f, 0.0f, 1.0f);
		Far = Far * Far * (3.0f - 2.0f * Far);
		const FSlateFontInfo NameFont = Font(EWeight::Regular, 11.5f - 4.0f * Far, 40);
		const float Alpha = (0.85f - 0.70f * Far) * FMath::Clamp(NameLabel.Opacity, 0.0f, 1.0f);
		if (Alpha <= 0.005f)
		{
			continue;
		}
		const FVector2f TextSize = Painter.Measure(NameLabel.Name, NameFont);
		const FVector2f At(NameLabel.Screen.X, NameLabel.Screen.Y - TextSize.Y - 5.0f);
		const FLinearColor NameColour = Fade(FMath::Lerp(Text(), Colour, 0.55f), Alpha);
		Painter.Fill(HudBox(At.X - TextSize.X * 0.5f - 6.0f, At.Y - 2.0f, TextSize.X + 12.0f, TextSize.Y + 4.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.28f * Alpha), 3.0f);
		Painter.Text(At, NameLabel.Name, NameFont, NameColour, EAlign::Centre);
		// A small tick down to the head, so two names close together still say whose they are.
		Painter.Line(FVector2f(NameLabel.Screen.X, NameLabel.Screen.Y - 4.0f), FVector2f(NameLabel.Screen.X, NameLabel.Screen.Y), Fade(Colour, 0.6f * Alpha), 1.0f);

		// The mercs' marks over the name, side by side when both apply (HudTeammateNames draws them 1.75 times its
		// smaller text's height; this text is taller, so they are 1.4 times it).
		const bool bSleeping = (NameLabel.Flags & EScctTeammateNameFlags::Sleeping) != 0;
		const bool bBackpack = (NameLabel.Flags & EScctTeammateNameFlags::Backpack) != 0;
		if ((NameLabel.Flags & EScctTeammateNameFlags::Merc) == 0 || (!bSleeping && !bBackpack))
		{
			continue;
		}
		const float Size = FMath::RoundToFloat(TextSize.Y * 1.4f);
		const float Gap = 3.0f;
		const float Width = bSleeping && bBackpack ? Size * 2.0f + Gap : Size;
		FVector2f Min(FMath::RoundToFloat(NameLabel.Screen.X - Width * 0.5f), FMath::RoundToFloat(At.Y - 4.0f - Size));
		if (bSleeping)
		{
			PaintNamesSleepMark(Painter, FBox2f(Min, Min + FVector2f(Size)), Fade(NamesSleepColour, Alpha));
			Min.X += Size + Gap;
		}
		if (bBackpack)
		{
			// Each player has a share of his own in a mate's backpack: grey once that share is spent.
			const bool bSupplies = (NameLabel.Flags & EScctTeammateNameFlags::BackpackSupplies) != 0;
			PaintNamesBackpackMark(Painter, FBox2f(Min, Min + FVector2f(Size)), bSupplies ? NameColour : Fade(NamesSpentColour, Alpha));
		}
	}
}
