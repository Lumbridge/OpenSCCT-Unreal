#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"

// The messages: the kill feed, the headlines under the clock and the log above the status block.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	/** A line appears quickly and leaves slowly. */
	float LineAlpha(const FScctHudLine& Line)
	{
		return FMath::Clamp(Line.Age / 0.2f, 0.0f, 1.0f) * FMath::Clamp((Line.Life - Line.Age) / 0.7f, 0.0f, 1.0f);
	}

	bool IsKill(const FScctHudLine& Line)
	{
		const EScctHudActivity Kind = static_cast<EScctHudActivity>(Line.Kind);
		return (Kind == EScctHudActivity::Kill || Kind == EScctHudActivity::Sleep || Kind == EScctHudActivity::Knockout)
			&& !Line.Actor.IsEmpty() && !Line.Victim.IsEmpty();
	}
}

void ScctHudElements::PaintFeed(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.Feed.Num() == 0)
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const float Right = Size.X - Margin;
	const FSlateFontInfo NameFont = Font(EWeight::Regular, 10.5f);
	const FSlateFontInfo TextFont = Font(EWeight::Regular, 10.0f);
	const FSlateFontInfo CauseFont = Font(EWeight::Regular, 8.5f, 20);
	constexpr float NameRowHeight = 26.0f, CauseRowHeight = 13.0f, RowGap = 4.0f, Pad = 12.0f, Mark = 40.0f, FeedIconSize = 15.0f, IconGap = 9.0f;

	float Y = Margin + TelemetryReservedTopRight(Model);
	for (const FScctHudLine& Line : Model.Feed)
	{
		const float Alpha = LineAlpha(Line);
		const float Slide = (1.0f - EaseOut(Line.Age / 0.3f)) * 36.0f;
		const bool bLocal = (Line.Flags & EScctHudMessageFlags::InvolvesLocal) != 0;
		const bool bKill = IsKill(Line);

		// How it happened, on a second line under the names (Godot's cause subtitle and trigger line).
		const FString Cause = ActivityCauseText(Line);
		const float CauseWidth = Cause.IsEmpty() ? 0.0f : Painter.Measure(Cause, CauseFont).X;
		const float RowHeight = NameRowHeight + (Cause.IsEmpty() ? 0.0f : CauseRowHeight);

		// A kill is drawn from its parts (who, to whom) in the teams' colours; anything else is the game's line. Each row
		// starts with the icon of its cause, or of what happened.
		const float ActorWidth = bKill ? Painter.Measure(Line.Actor, NameFont).X : 0.0f;
		const float VictimWidth = bKill ? Painter.Measure(Line.Victim, NameFont).X : 0.0f;
		const float MainWidth = bKill ? ActorWidth + Mark + VictimWidth : Painter.Measure(Line.Text, TextFont).X;
		const float Width = Pad * 2.0f + FeedIconSize + IconGap + FMath::Max(MainWidth, CauseWidth);
		const float X = Right - Width + Slide;
		const float TextX = X + Pad + FeedIconSize + IconGap;

		Painter.SetOpacity(Alpha);
		Painter.Blur(HudBox(X, Y, Width, RowHeight), 10.0f, 4.0f);
		Painter.Fill(HudBox(X, Y, Width, RowHeight), FLinearColor(0.004f, 0.007f, 0.011f, bLocal ? 0.52f : 0.36f), 4.0f,
			bLocal ? Fade(Accent(Model.LocalTeam), 0.55f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.07f), 1.0f);

		const bool bLethalLine = static_cast<EScctHudActivity>(Line.Kind) == EScctHudActivity::Kill
			|| static_cast<EScctHudActivity>(Line.Kind) == EScctHudActivity::Death || static_cast<EScctHudActivity>(Line.Kind) == EScctHudActivity::Suicide;
		const FLinearColor IconColour = bLocal ? Accent(Model.LocalTeam) : (bLethalLine ? Fade(Text(), 0.85f) : TextDim());
		PaintActivityIcon(Painter, FVector2f(X + Pad + FeedIconSize * 0.5f, Y + RowHeight * 0.5f), FeedIconSize, Line, IconColour);

		if (!Cause.IsEmpty())
		{
			Painter.Text(FVector2f(TextX, Y + NameRowHeight - 4.0f), Cause, CauseFont, TextDim());
		}

		if (bKill)
		{
			Painter.Text(FVector2f(TextX, Y + 5.0f), Line.Actor, NameFont, Accent(Line.ActorTeam));

			// Between the names: an arrow for a kill, with a ring on it for a headshot; a softer mark for a knock-out.
			const FVector2f MarkCentre(TextX + ActorWidth + Mark * 0.5f, Y + NameRowHeight * 0.5f);
			const bool bLethal = static_cast<EScctHudActivity>(Line.Kind) == EScctHudActivity::Kill;
			const FLinearColor MarkColour = bLethal ? Fade(Text(), 0.85f) : TextDim();
			const bool bHeadshot = (Line.Flags & EScctHudMessageFlags::Headshot) != 0;
			const float Tail = bHeadshot ? -1.0f : -8.0f;
			Painter.Line(MarkCentre + FVector2f(Tail, 0.0f), MarkCentre + FVector2f(8.0f, 0.0f), MarkColour, 1.5f);
			Painter.Polyline({ MarkCentre + FVector2f(4.0f, -4.0f), MarkCentre + FVector2f(8.5f, 0.0f), MarkCentre + FVector2f(4.0f, 4.0f) }, MarkColour, 1.5f);
			if (bHeadshot)
			{
				// A sight ring at the tail of the arrow.
				const FVector2f Ring = MarkCentre + FVector2f(-6.0f, 0.0f);
				Painter.Arc(Ring, 4.5f, 0.0f, 360.0f, Danger(), 1.5f);
				Painter.Fill(FBox2f(Ring - FVector2f(1.0f), Ring + FVector2f(1.0f)), Danger(), 1.0f);
			}

			Painter.Text(FVector2f(TextX + ActorWidth + Mark, Y + 5.0f), Line.Victim, NameFont, Accent(Line.VictimTeam));
		}
		else
		{
			Painter.Text(FVector2f(TextX, Y + 6.0f), Line.Text, TextFont, bLocal ? Text() : TextDim());
		}
		Painter.SetOpacity(1.0f);
		Y += (RowHeight + RowGap) * FMath::Clamp((Line.Life - Line.Age) / 0.3f, 0.0f, 1.0f);
	}
}

void ScctHudElements::PaintHeadlines(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.Headlines.Num() == 0)
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const float CentreX = Size.X * 0.5f;
	const FLinearColor Accent = ScctHudStyle::Accent(Model.LocalTeam);
	constexpr float MaxWidth = 640.0f, PadX = 22.0f, PadY = 11.0f;

	// Below the clock, its pips and a row of countdown chips.
	float Y = Margin + 112.0f;
	for (const FScctHudLine& Line : Model.Headlines)
	{
		// A briefing is a paragraph; an objective line is short and carries the side's colour. Briefings wait unseen while
		// a tutorial message is up (the game message queue's wait).
		const bool bBriefing = Line.Category == EScctHudMessageCategory::Game;
		if (bBriefing && Model.IsTutorialUp())
		{
			continue;
		}
		const FSlateFontInfo LineFont = bBriefing ? Font(EWeight::Light, 13.0f, 20) : Font(EWeight::Regular, 12.0f, 60);
		TArray<FString> Lines;
		Painter.Wrap(Line.Text, LineFont, MaxWidth, 4, Lines);
		float Widest = 0.0f;
		for (const FString& Wrapped : Lines)
		{
			Widest = FMath::Max(Widest, Painter.Measure(Wrapped, LineFont).X);
		}
		const float LineHeight = Painter.Measure(TEXT("Ag"), LineFont).Y + 3.0f;
		const float Width = Widest + PadX * 2.0f, Height = Lines.Num() * LineHeight - 3.0f + PadY * 2.0f;
		const float Alpha = LineAlpha(Line);
		const float Top = Y - (1.0f - EaseOut(Line.Age / 0.35f)) * 10.0f;

		Painter.SetOpacity(Alpha);
		Painter.Glass(HudBox(CentreX - Width * 0.5f, Top, Width, Height), Corner);
		if (!bBriefing)
		{
			Painter.GlowLine(FVector2f(CentreX - Width * 0.5f + 1.0f, Top + 9.0f), FVector2f(CentreX - Width * 0.5f + 1.0f, Top + Height - 9.0f), Accent, 2.0f);
		}
		float TextY = Top + PadY;
		for (const FString& Wrapped : Lines)
		{
			Painter.Text(FVector2f(CentreX, TextY), Wrapped, LineFont, Text(), EAlign::Centre);
			TextY += LineHeight;
		}
		Painter.SetOpacity(1.0f);
		Y += (Height + 8.0f) * FMath::Clamp((Line.Life - Line.Age) / 0.3f, 0.0f, 1.0f);
	}
}

void ScctHudElements::PaintLog(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.Log.Num() == 0)
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const FSlateFontInfo LineFont = Font(EWeight::Regular, 10.5f);
	constexpr float LineHeight = 20.0f;

	// Stacked upwards from above the status block (and above the vision chip while that shows), newest lowest.
	// The open chat line takes the place of the newest line.
	float Y = Size.Y - Margin - 72.0f - 16.0f - FMath::Max(Model.VisionShown * 32.0f, Model.bChatOpen ? 46.0f : 0.0f) - LineHeight;
	for (int32 Index = Model.Log.Num() - 1; Index >= 0; --Index)
	{
		const FScctHudLine& Line = Model.Log[Index];
		const bool bTeamChat = Line.Category == EScctHudMessageCategory::TeamChat;
		const bool bChat = bTeamChat || Line.Category == EScctHudMessageCategory::Chat;
		const FLinearColor Colour = bTeamChat ? Accent(Model.LocalTeam) : (bChat ? Text() : Fade(Text(), 0.82f));
		const float Slide = (1.0f - EaseOut(Line.Age / 0.3f)) * -14.0f;

		Painter.SetOpacity(LineAlpha(Line));
		Painter.Fill(HudBox(Margin + Slide, Y + 3.0f, 2.0f, 12.0f), Fade(Colour, 0.6f));
		float TextX = Margin + 10.0f + Slide;
		if (bChat && !Line.Actor.IsEmpty())
		{
			// Who said it, in his side's colour; a line to the team alone is marked as one.
			const FLinearColor NameColour = Line.ActorTeam == 0 || Line.ActorTeam == 1 ? Accent(Line.ActorTeam) : TextDim();
			if (bTeamChat)
			{
				TextX += Painter.Text(FVector2f(TextX, Y + 3.0f), TEXT("TEAM"), Label(7.0f), Fade(NameColour, 0.8f)).X + 8.0f;
			}
			TextX += Painter.Text(FVector2f(TextX, Y), Line.Actor, Font(EWeight::Bold, 10.5f), NameColour).X + 8.0f;
		}
		Painter.Text(FVector2f(TextX, Y), Line.Text, LineFont, bChat ? Text() : Colour);
		Painter.SetOpacity(1.0f);
		Y -= LineHeight;
	}
}
