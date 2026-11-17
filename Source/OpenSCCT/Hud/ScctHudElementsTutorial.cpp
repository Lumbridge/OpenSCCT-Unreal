#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"

// The tutorial message box (the original's TutoMessage area of the spy and merc pages, Godot's DriveTutorial): the text
// of SHud.sTutoMap, a tutorial map's hint or an offline session's zone tip, held open for as long as the game keeps it.
// The zone owns its lifetime, so the box has no timeout of its own; it eases in and out with Model.TutorialShown.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

void ScctHudElements::PaintTutorial(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.TutorialShown <= 0.01f || Model.TutorialText.IsEmpty() || !Model.bHasState
		|| (Model.State.Flags & EScctHudFlags::HideHud) != 0 || Model.State.GadgetView != 0 || Model.State.BinocularMode != 0)
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const float CentreX = Size.X * 0.5f;
	const FLinearColor Accent = ScctHudStyle::Accent(Model.LocalTeam);
	constexpr float MaxTextWidth = 520.0f, PadX = 20.0f, PadY = 13.0f, MarkWidth = 30.0f;

	const FSlateFontInfo TextFont = Font(EWeight::Regular, 12.0f, 10);
	TArray<FString> Lines;
	Painter.Wrap(Model.TutorialText, TextFont, MaxTextWidth, 6, Lines);
	if (Lines.Num() == 0)
	{
		return;
	}
	float Widest = 0.0f;
	for (const FString& Line : Lines)
	{
		Widest = FMath::Max(Widest, Painter.Measure(Line, TextFont).X);
	}
	const float LineHeight = Painter.Measure(TEXT("Ag"), TextFont).Y + 3.0f;
	const float Width = PadX * 2.0f + MarkWidth + Widest;
	const float Height = PadY * 2.0f + Lines.Num() * LineHeight - 3.0f;
	const float In = EaseOut(Model.TutorialShown);
	const float X = CentreX - Width * 0.5f;
	// Lower centre, clear of the crosshair and above the prompt and the status block.
	const float Y = Size.Y * 0.70f - Height + (1.0f - In) * 12.0f;

	Painter.SetOpacity(Model.TutorialShown);
	Painter.Glass(HudBox(X, Y, Width, Height), Corner, 0.70f);
	Painter.GlowLine(FVector2f(X + 1.0f, Y + 9.0f), FVector2f(X + 1.0f, Y + Height - 9.0f), Accent, 2.0f);

	// The hint mark: an "i" in a ring, in the side's colour.
	const FVector2f Mark(X + PadX + 8.0f, Y + PadY + LineHeight * 0.5f - 1.0f);
	Painter.Arc(Mark, 8.0f, 0.0f, 360.0f, Fade(Accent, 0.9f), 1.5f);
	Painter.Fill(HudBox(Mark.X - 1.0f, Mark.Y - 4.5f, 2.0f, 2.0f), Accent, 1.0f);
	Painter.Fill(HudBox(Mark.X - 1.0f, Mark.Y - 1.0f, 2.0f, 5.5f), Accent, 0.5f);

	float TextY = Y + PadY;
	for (const FString& Line : Lines)
	{
		Painter.Text(FVector2f(X + PadX + MarkWidth, TextY), Line, TextFont, Text());
		TextY += LineHeight;
	}
	Painter.SetOpacity(1.0f);
}
