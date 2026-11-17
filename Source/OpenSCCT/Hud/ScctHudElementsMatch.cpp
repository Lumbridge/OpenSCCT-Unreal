#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"

// The blocks about the match: clock and teams, objectives, world markers, the start and end banners, the scoreboard.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	/** Deathmatch and Merc Deathmatch: the score column counts kills for both sides and there are no objectives. */
	bool IsDeathmatch(const FScctHudRound& Round)
	{
		return Round.GameMode == 6 || Round.GameMode == 7;
	}

	/** Merc Deathmatch: everyone is on the mercs and plays for himself, to a kill limit, with unlimited lives. */
	bool IsFreeForAll(const FScctHudRound& Round)
	{
		return Round.GameMode == 7;
	}

	/** The scoreboard row of the player at this machine, or null before it has arrived. */
	const FScctHudScoreRow* LocalRow(const FScctHudModel& Model)
	{
		return Model.Scores.FindByPredicate([](const FScctHudScoreRow& Row) { return (Row.Flags & EScctHudScoreFlags::Local) != 0; });
	}

	/** The player with the most kills who is not the local one, or null when he plays alone. */
	const FScctHudScoreRow* LeadingRival(const FScctHudModel& Model)
	{
		const FScctHudScoreRow* Best = nullptr;
		for (const FScctHudScoreRow& Row : Model.Scores)
		{
			if ((Row.Flags & (EScctHudScoreFlags::Local | EScctHudScoreFlags::Spectator)) == 0 && (Best == nullptr || Row.Kills > Best->Kills))
			{
				Best = &Row;
			}
		}
		return Best;
	}

	const FString& TeamName(const FScctHudModel& Model, int32 Team)
	{
		return Model.Label(Team == 0 ? EScctHudLabel::Spies : EScctHudLabel::Mercenaries);
	}

	/** A small countdown chip: a ring that empties over a minute and the time. */
	void Countdown(FScctHudPainter& Painter, float CentreX, float Y, int32 Seconds, const FLinearColor& Colour)
	{
		constexpr float Width = 74.0f, Height = 24.0f;
		Painter.Glass(HudBox(CentreX - Width * 0.5f, Y, Width, Height), 12.0f);
		const FVector2f Ring(CentreX - Width * 0.5f + 15.0f, Y + 12.0f);
		Painter.Arc(Ring, 6.0f, 0.0f, 360.0f, Fade(Colour, 0.25f), 1.5f);
		Painter.Arc(Ring, 6.0f, 0.0f, 360.0f * FMath::Clamp(Seconds / 60.0f, 0.0f, 1.0f), Colour, 1.5f);
		Painter.Text(FVector2f(CentreX + 8.0f, Y + 4.0f), ScctHudElements::ClockText(Seconds), Font(EWeight::Regular, 11.0f), Colour, EAlign::Centre);
	}
}

void ScctHudElements::PaintRound(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!Model.bHasRound || (Model.bHasState && (Model.State.Flags & (EScctHudFlags::HideHud | EScctHudFlags::TimeOff)) != 0))
	{
		return;
	}

	const FScctHudRound& Round = Model.Round;
	const FVector2f Size = Painter.Size();
	const float CentreX = Size.X * 0.5f;
	constexpr float Width = 372.0f, Height = 46.0f;
	const float X = CentreX - Width * 0.5f, Y = Margin;

	Painter.Glass(HudBox(X, Y, Width, Height), Corner);

	// The clock in the middle: time left when the round has a limit, else time played.
	const bool bLimited = Round.RemainingSeconds >= 0;
	const int32 Seconds = bLimited ? Round.RemainingSeconds : Round.ElapsedTime;
	const bool bLastMinute = bLimited && Seconds <= 60;
	const FLinearColor ClockColour = bLastMinute ? Fade(Danger(), 0.75f + 0.25f * FMath::Sin(Model.Clock * 5.0f)) : Text();
	Painter.Text(FVector2f(CentreX, Y + 8.0f), ClockText(Seconds), Font(EWeight::Light, 21.0f, 40), ClockColour, EAlign::Centre);

	// One block each side of the clock: a name in small capitals and a number; the player's own is underlined.
	const auto Side = [&](bool bLeft, const FString& Name, const FString& Number, const FLinearColor& Colour, bool bOwn)
	{
		const float Edge = bLeft ? X + 18.0f : X + Width - 18.0f;
		const EAlign Align = bLeft ? EAlign::Left : EAlign::Right;
		const FVector2f NameSize = Painter.Text(FVector2f(Edge, Y + 17.0f), Name.ToUpper(), Label(8.0f), Fade(Colour, 0.9f), Align);
		const float NumberX = bLeft ? Edge + NameSize.X + 10.0f : Edge - NameSize.X - 10.0f;
		Painter.Text(FVector2f(NumberX, Y + 12.0f), Number, Font(EWeight::Bold, 13.0f), Text(), Align);
		if (bOwn)
		{
			const float From = bLeft ? Edge : Edge - NameSize.X;
			Painter.GlowLine(FVector2f(From, Y + Height - 1.0f), FVector2f(From + NameSize.X, Y + Height - 1.0f), Colour, 1.5f);
		}
	};

	if (IsFreeForAll(Round))
	{
		// Everyone for himself: the player's kills on the left, the best of the others on the right.
		if (const FScctHudScoreRow* Local = LocalRow(Model))
		{
			Side(true, Model.Text(Local->NameId), FString::FromInt(Local->Kills), Accent(Model.LocalTeam), true);
		}
		if (const FScctHudScoreRow* Rival = LeadingRival(Model))
		{
			Side(false, Model.Text(Rival->NameId), FString::FromInt(Rival->Kills), Accent(-1), false);
		}
	}
	else
	{
		// A team each side with the lives it has left (a team deathmatch is lost when they run out, too).
		for (int32 Team = 0; Team < 2; ++Team)
		{
			Side(Team == 0, TeamName(Model, Team), LivesText(Model, Round.TeamLives[Team], 1000), Accent(Team), Team == Model.LocalTeam);
		}
	}

	// Under the clock: a pip per objective, lit once the spies have done it (disks taken in Disk Hunt).
	float BelowY = Y + Height + 9.0f;
	const int32 Pips = Round.CounterMode == 2 ? Round.NumDisksToWin : Round.NumObjectives;
	const int32 Lit = Round.CounterMode == 2 ? Round.NumDisks : Round.NumObjectives - Round.ObjectivesLeft;
	if (Pips > 0 && Pips <= 12 && !IsDeathmatch(Round))
	{
		constexpr float PipWidth = 18.0f, PipGap = 5.0f;
		float PipX = CentreX - (Pips * PipWidth + (Pips - 1) * PipGap) * 0.5f;
		for (int32 Index = 0; Index < Pips; ++Index, PipX += PipWidth + PipGap)
		{
			const bool bDone = Index < Lit;
			Painter.Fill(HudBox(PipX, BelowY - 0.5f, PipWidth, 4.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 2.0f);
			Painter.Fill(HudBox(PipX, BelowY, PipWidth, 3.0f), bDone ? Accent(0) : Fade(Text(), 0.30f), 1.5f);
		}
		BelowY += 14.0f;
	}

	// The countdown boxes of the original: the alarm, a hack in progress, timed objectives.
	if (Model.bHasState)
	{
		const FScctHudState& State = Model.State;
		const int32 Values[] = { State.AlarmSeconds, State.HackCountdown, State.ObjectiveCountdown0, State.ObjectiveCountdown1 };
		const FLinearColor Colours[] = { Danger(), Accent(0), Text(), Text() };
		int32 Shown = 0;
		for (const int32 Value : Values)
		{
			Shown += Value >= 0 ? 1 : 0;
		}
		float ChipX = CentreX - (Shown - 1) * 41.0f;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
		{
			if (Values[Index] >= 0)
			{
				Countdown(Painter, ChipX, BelowY, Values[Index], Colours[Index]);
				ChipX += 82.0f;
			}
		}
	}
}

void ScctHudElements::PaintObjectives(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!Model.bHasRound || !Model.bHasState || (Model.State.Flags & EScctHudFlags::HideHud) != 0 || Model.ObjectiveLines.Num() == 0)
	{
		return;
	}

	const FScctHudState& State = Model.State;
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	constexpr float Width = 316.0f, Pad = 18.0f, TextLeft = 22.0f;
	const float X = Margin, Y = Margin + RadarReservedHeight(Model);
	const FSlateFontInfo LineFont = Font(EWeight::Regular, 10.0f);
	const float LineHeight = Painter.Measure(TEXT("Ag"), LineFont).Y + 2.0f;
	const float TextWidth = Width - Pad * 2.0f - TextLeft;

	// Hacks, thefts and bombs in progress, one row per objective name.
	struct FProgress
	{
		int32 NameId;
		int32 Percent;
	};
	TArray<FProgress, TInlineAllocator<4>> Active;
	for (const FScctHudObjective& Objective : Model.Objectives)
	{
		if ((Objective.Flags & EScctHudObjectiveFlags::Triggering) != 0 && Objective.Percent >= 0
			&& !Active.ContainsByPredicate([&Objective](const FProgress& Known) { return Known.NameId == Objective.NameId; }))
		{
			Active.Add({ Objective.NameId, Objective.Percent });
		}
	}

	// Measure first: the panel is as tall as its text.
	TArray<TArray<FString>> Wrapped;
	float Height = 38.0f;
	for (const FScctHudObjectiveLine& Line : Model.ObjectiveLines)
	{
		TArray<FString>& Lines = Wrapped.AddDefaulted_GetRef();
		Painter.Wrap(Model.Text(Line.TextId), LineFont, TextWidth, 2, Lines);
		Height += FMath::Max(Lines.Num(), 1) * LineHeight + 7.0f;
	}
	Height += Active.Num() * 24.0f + 6.0f;

	Painter.Glass(HudBox(X, Y, Width, Height), Corner);
	Painter.GlowLine(FVector2f(X + 1.0f, Y + 12.0f), FVector2f(X + 1.0f, Y + 26.0f), Accent, 2.0f);

	// Header: the map (where the player is in it is the place name above the radar, shown in every mode).
	Painter.Text(FVector2f(X + Pad, Y + 13.0f), Model.Text(Model.Round.MapTitleId).ToUpper(), Label(8.0f), TextDim());
	Painter.Line(FVector2f(X + Pad, Y + 32.0f), FVector2f(X + Width - Pad, Y + 32.0f), Fade(Text(), 0.10f), 1.0f);

	float RowY = Y + 41.0f;
	for (int32 Index = 0; Index < Model.ObjectiveLines.Num(); ++Index)
	{
		const bool bDone = Model.ObjectiveLines[Index].Done != 0;

		// A box per objective: empty to do, filled and ticked when done.
		const FBox2f Box = HudBox(X + Pad, RowY + 3.0f, 9.0f, 9.0f);
		if (bDone)
		{
			Painter.Fill(Box, Fade(Accent, 0.85f), 2.0f);
			Painter.Polyline({ Box.Min + FVector2f(2.0f, 4.5f), Box.Min + FVector2f(4.0f, 6.5f), Box.Min + FVector2f(7.0f, 2.5f) }, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), 1.5f);
		}
		else
		{
			Painter.Fill(Box, FLinearColor::Transparent, 2.0f, Fade(Text(), 0.55f), 1.0f);
		}

		for (const FString& Line : Wrapped[Index])
		{
			Painter.Text(FVector2f(X + Pad + TextLeft, RowY), Line, LineFont, bDone ? TextFaint() : Text());
			RowY += LineHeight;
		}
		RowY += 7.0f;
	}

	for (const FProgress& Progress : Active)
	{
		const float Pulse = 0.75f + 0.25f * FMath::Sin(Model.Clock * 6.0f);
		const FLinearColor Colour = Fade(State.Side == 0 ? Accent : Danger(), Pulse);
		Painter.Text(FVector2f(X + Pad, RowY + 2.0f), Model.Text(Progress.NameId).ToUpper(), Label(8.0f), Colour);
		Painter.Text(FVector2f(X + Width - Pad, RowY), FString::Printf(TEXT("%d%%"), Progress.Percent), Font(EWeight::Bold, 10.0f), Colour, EAlign::Right);
		Painter.Fill(HudBox(X + Pad, RowY + 17.0f, Width - Pad * 2.0f, 2.0f), Fade(Text(), 0.14f));
		Painter.Fill(HudBox(X + Pad, RowY + 17.0f, (Width - Pad * 2.0f) * FMath::Clamp(Progress.Percent / 100.0f, 0.0f, 1.0f), 2.0f), Colour);
		RowY += 24.0f;
	}
}

void ScctHudElements::PaintMarkers(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!ShowsPlayerHud(Model) || Model.State.GadgetView != 0)
	{
		return;
	}

	// The spy's markers are his "enhanced reality" display and can be switched off; the merc always has his.
	const FScctHudState& State = Model.State;
	if (State.Side == 0 && (State.Flags & EScctHudFlags::EnhancedReality) == 0)
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const FVector2f Centre = Size * 0.5f;
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	for (const FScctHudMarker& Marker : Model.Markers)
	{
		const FVector2f At = Marker.Screen;
		if (!Marker.bInFront || At.X < 0.0f || At.Y < 0.0f || At.X > Size.X || At.Y > Size.Y)
		{
			continue;
		}

		const FScctHudObjective& Objective = Marker.Objective;
		const bool bTaken = (Objective.Flags & EScctHudObjectiveFlags::Taken) != 0;
		const bool bActive = (Objective.Flags & EScctHudObjectiveFlags::Triggering) != 0;
		const bool bAlert = (Objective.Flags & EScctHudObjectiveFlags::Alert) != 0;

		// Quiet by default, louder when something is happening there or the player looks at it.
		const float Focus = 1.0f - FMath::Clamp(FVector2f::Distance(At, Centre) / 220.0f, 0.0f, 1.0f);
		FLinearColor Colour = bActive || bAlert ? Danger() : Accent;
		float Alpha = bTaken ? 0.25f : (bActive ? 0.75f + 0.25f * FMath::Sin(Model.Clock * 7.0f) : 0.45f + 0.45f * Focus);
		Colour = Fade(Colour, Alpha);

		constexpr float Half = 6.0f;
		Painter.Polyline({ At + FVector2f(0.0f, -Half), At + FVector2f(Half, 0.0f), At + FVector2f(0.0f, Half), At + FVector2f(-Half, 0.0f), At + FVector2f(0.0f, -Half) }, Colour, 1.5f);
		if ((Objective.Flags & EScctHudObjectiveFlags::Locked) != 0)
		{
			Painter.Fill(FBox2f(At - FVector2f(1.5f), At + FVector2f(1.5f)), Colour, 1.5f);
		}
		if (Objective.Percent >= 0)
		{
			Painter.Arc(At, 12.0f, 0.0f, 360.0f * Objective.Percent / 100.0f, Colour, 2.0f, 1.0f);
		}

		// Name and distance only near where the player looks, so a room full of markers stays calm.
		if (Focus > 0.05f || bActive)
		{
			const float TextAlpha = bActive ? 1.0f : Focus;
			Painter.Text(At + FVector2f(0.0f, 14.0f), Model.Text(Objective.NameId).ToUpper(), Label(7.5f), Fade(Text(), TextAlpha), EAlign::Centre);
			const FString Distance = FString::Printf(TEXT("%.0f %s"), Marker.Distance * State.DistanceScale, *Model.Text(State.DistanceUnitId));
			Painter.Text(At + FVector2f(0.0f, 27.0f), Distance, Font(EWeight::Regular, 8.0f), Fade(TextDim(), TextAlpha), EAlign::Centre);
		}
	}
}

void ScctHudElements::PaintSpectator(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!Model.bHasState || !Model.bHasRound || (Model.State.Flags & EScctHudFlags::Spectating) == 0
		|| (Model.State.Flags & (EScctHudFlags::HideHud | EScctHudFlags::GameEnded)) != 0)
	{
		return;
	}

	const FScctHudRound& Round = Model.Round;
	const FVector2f Size = Painter.Size();
	const float CentreX = Size.X * 0.5f;
	constexpr float Width = 420.0f, Pad = 20.0f;
	const FSlateFontInfo ValueFont = Font(EWeight::Regular, 12.0f);

	// What the block says, top to bottom: whom the player watches, how long until he plays again (or that he
	// will not), and the team's tip of the moment.
	const FString& Watched = Model.Text(Round.WatchedNameId);
	int32 WatchedTeam = -1;
	for (const FScctHudScoreRow& Row : Model.Scores)
	{
		if (Round.WatchedNameId != 0 && Row.PlayerId == Round.WatchedPlayerId)
		{
			WatchedTeam = Row.Team;
		}
	}
	const bool bCountdown = Round.RespawnIn > 0;
	const bool bOut = !bCountdown && Round.NoRespawn != 0;
	TArray<FString> TipLines;
	const FSlateFontInfo TipFont = Font(EWeight::Light, 11.0f);
	Painter.Wrap(Model.Text(Round.TipId), TipFont, Width - Pad * 2.0f, 5, TipLines);
	const float TipLineHeight = Painter.Measure(TEXT("Ag"), TipFont).Y + 3.0f;

	const int32 Rows = (Watched.IsEmpty() ? 0 : 1) + (bCountdown || bOut ? 1 : 0);
	const float Height = 40.0f + Rows * 24.0f + (TipLines.Num() > 0 ? TipLines.Num() * TipLineHeight + 9.0f : 0.0f) + 6.0f;
	const float X = CentreX - Width * 0.5f;
	const float Y = Size.Y - Margin - 96.0f - Height;

	Painter.Glass(HudBox(X, Y, Width, Height), Corner);
	Painter.Text(FVector2f(CentreX, Y + 13.0f), Model.Label(EScctHudLabel::SpectatorMode).ToUpper(), Label(8.0f), TextDim(), EAlign::Centre);
	Painter.Line(FVector2f(X + Pad, Y + 32.0f), FVector2f(X + Width - Pad, Y + 32.0f), Fade(Text(), 0.10f), 1.0f);

	float RowY = Y + 40.0f;
	if (!Watched.IsEmpty())
	{
		Painter.Text(FVector2f(X + Pad, RowY + 3.0f), Model.Label(EScctHudLabel::Watching).ToUpper(), Label(8.0f), TextDim());
		Painter.Text(FVector2f(X + Width - Pad, RowY), Watched, ValueFont, Accent(WatchedTeam), EAlign::Right);
		RowY += 24.0f;
	}
	if (bCountdown)
	{
		// The wait: the seconds, and a ring that closes as the last ten run out.
		Painter.Text(FVector2f(X + Pad, RowY + 3.0f), Model.Label(EScctHudLabel::RespawnIn).ToUpper(), Label(8.0f), TextDim());
		const FVector2f NumberSize = Painter.Text(FVector2f(X + Width - Pad, RowY), FString::FromInt(Round.RespawnIn), Font(EWeight::Bold, 12.0f), Text(), EAlign::Right);
		const FVector2f Ring(X + Width - Pad - NumberSize.X - 16.0f, RowY + 9.0f);
		Painter.Arc(Ring, 6.0f, 0.0f, 360.0f, Fade(Text(), 0.2f), 1.5f);
		Painter.Arc(Ring, 6.0f, 0.0f, 360.0f * FMath::Clamp(Round.RespawnIn / 10.0f, 0.0f, 1.0f), Accent(Model.LocalTeam), 1.5f);
		RowY += 24.0f;
	}
	else if (bOut)
	{
		Painter.Text(FVector2f(CentreX, RowY + 1.0f), Model.Label(EScctHudLabel::NoRespawn), ValueFont, Danger(), EAlign::Centre);
		RowY += 24.0f;
	}
	if (TipLines.Num() > 0)
	{
		RowY += 6.0f;
		for (const FString& Line : TipLines)
		{
			Painter.Text(FVector2f(CentreX, RowY), Line, TipFont, TextDim(), EAlign::Centre);
			RowY += TipLineHeight;
		}
	}
}

void ScctHudElements::PaintBanner(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FVector2f Size = Painter.Size();
	const float CentreX = Size.X * 0.5f;

	// The round is over: who won, in the game's own words, until the next match. A tutorial has no winner and
	// ends on the original's plain "finished" line.
	const bool bTutorial = Model.bHasState && Model.State.TutorialId != 0;
	if (Model.EndAge >= 0.0f && Model.bHasRound && (bTutorial || (Model.Round.WinningTeam >= 0 && Model.Round.WinningTeam <= 1)))
	{
		const bool bWon = bTutorial || Model.Round.WinningTeam == Model.LocalTeam;
		const float In = EaseOut(Model.EndAge / 0.5f);
		const float Y = Size.Y * 0.34f;
		const FLinearColor Colour = bWon ? Accent(Model.LocalTeam) : Danger();
		Painter.SetOpacity(In);
		Painter.Blur(HudBox(0.0f, Y, Size.X, 120.0f), 10.0f);
		Painter.Gradient(HudBox(0.0f, Y, CentreX, 120.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), false);
		Painter.Gradient(HudBox(CentreX, Y, CentreX, 120.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), false);
		const FString Title = Model.Label(bTutorial ? EScctHudLabel::Finished : (bWon ? EScctHudLabel::YouWin : EScctHudLabel::YouLose)).ToUpper();
		const FVector2f TitleSize = Painter.Text(FVector2f(CentreX, Y + 26.0f), Title, Font(EWeight::Light, 34.0f, 320), Colour, EAlign::Centre);
		const float Reach = TitleSize.X * 0.5f + 28.0f;
		Painter.GlowLine(FVector2f(CentreX - Reach - 160.0f * In, Y + 60.0f), FVector2f(CentreX - Reach, Y + 60.0f), Colour, 1.5f);
		Painter.GlowLine(FVector2f(CentreX + Reach, Y + 60.0f), FVector2f(CentreX + Reach + 160.0f * In, Y + 60.0f), Colour, 1.5f);
		if (!bTutorial)
		{
			Painter.Text(FVector2f(CentreX, Y + 84.0f), TeamName(Model, Model.Round.WinningTeam).ToUpper(), Label(9.0f), Fade(Accent(Model.Round.WinningTeam), 0.9f), EAlign::Centre);
		}
		Painter.SetOpacity(1.0f);
		return;
	}

	// The round begins: the map's name for a few seconds, then out of the way.
	constexpr float Stay = 5.5f;
	if (!Model.bHasRound || Model.Clock >= Stay || Model.Round.MapTitleId == 0)
	{
		return;
	}
	const float Alpha = FMath::Min(Model.Clock / 0.5f, 1.0f) * FMath::Clamp((Stay - Model.Clock) / 0.8f, 0.0f, 1.0f);
	const float In = EaseOut(Model.Clock / 0.9f);
	const float Y = Size.Y * 0.30f;
	const FLinearColor Colour = Accent(Model.LocalTeam);
	Painter.SetOpacity(Alpha);
	const FVector2f TitleSize = Painter.Text(FVector2f(CentreX, Y), Model.Text(Model.Round.MapTitleId).ToUpper(), Font(EWeight::Light, 26.0f, 360), Text(), EAlign::Centre);
	const float Reach = TitleSize.X * 0.5f + 28.0f;
	Painter.GlowLine(FVector2f(CentreX - Reach - 140.0f * In, Y + 22.0f), FVector2f(CentreX - Reach, Y + 22.0f), Colour, 1.5f);
	Painter.GlowLine(FVector2f(CentreX + Reach, Y + 22.0f), FVector2f(CentreX + Reach + 140.0f * In, Y + 22.0f), Colour, 1.5f);
	Painter.Text(FVector2f(CentreX, Y + 50.0f), TeamName(Model, Model.LocalTeam).ToUpper(), Label(10.0f), Colour, EAlign::Centre);
	Painter.SetOpacity(1.0f);
}

void ScctHudElements::PaintScoreboard(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const float Shown = Model.ScoreboardShown;
	if (Shown <= 0.01f || !Model.bHasRound)
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const FScctHudRound& Round = Model.Round;

	// One table per team; in a free-for-all a single table of everyone, the most kills first. Spectators are not
	// on the board.
	const bool bFreeForAll = IsFreeForAll(Round);
	const int32 TableCount = bFreeForAll ? 1 : 2;
	TArray<const FScctHudScoreRow*, TInlineAllocator<16>> Tables[2];
	for (const FScctHudScoreRow& Row : Model.Scores)
	{
		if ((Row.Flags & EScctHudScoreFlags::Spectator) == 0 && Row.Team >= 0 && Row.Team <= 1)
		{
			Tables[bFreeForAll ? 0 : Row.Team].Add(&Row);
		}
	}
	if (bFreeForAll)
	{
		Tables[0].StableSort([](const FScctHudScoreRow& A, const FScctHudScoreRow& B) { return A.Kills > B.Kills; });
	}
	const int32 MostRows = FMath::Max3(Tables[0].Num(), Tables[1].Num(), 1);

	constexpr float Pad = 28.0f, ColumnGap = 36.0f, RowHeight = 30.0f, ColumnWidth = 474.0f;
	const float Width = Pad * 2.0f + TableCount * ColumnWidth + (TableCount - 1) * ColumnGap;
	const float Height = 150.0f + MostRows * RowHeight;
	const float X = (Size.X - Width) * 0.5f;
	const float Y = (Size.Y - Height) * 0.5f - 20.0f + (1.0f - EaseOut(Shown)) * 14.0f;

	Painter.SetOpacity(Shown);
	Painter.Fill(FBox2f(FVector2f::ZeroVector, Size), FLinearColor(0.0f, 0.0f, 0.0f, 0.30f));
	Painter.Blur(HudBox(X, Y, Width, Height), 22.0f, 10.0f);
	Painter.Fill(HudBox(X, Y, Width, Height), FLinearColor(0.004f, 0.007f, 0.011f, 0.66f), 10.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f);

	// Header: the page's title, the map and the clock.
	Painter.Text(FVector2f(X + Pad, Y + 24.0f), Model.Label(EScctHudLabel::MissionStatus).ToUpper(), Label(9.0f), TextDim());
	Painter.Text(FVector2f(X + Width * 0.5f, Y + 18.0f), Model.Text(Round.MapTitleId).ToUpper(), Font(EWeight::Light, 15.0f, 300), Text(), EAlign::Centre);
	const int32 Seconds = Round.RemainingSeconds >= 0 ? Round.RemainingSeconds : Round.ElapsedTime;
	Painter.Text(FVector2f(X + Width - Pad, Y + 19.0f), ClockText(Seconds), Font(EWeight::Light, 15.0f, 40), Text(), EAlign::Right);
	Painter.Line(FVector2f(X + Pad, Y + 56.0f), FVector2f(X + Width - Pad, Y + 56.0f), Fade(Text(), 0.10f), 1.0f);

	for (int32 Table = 0; Table < TableCount; ++Table)
	{
		const int32 Team = bFreeForAll ? Model.LocalTeam : Table;
		const float Left = X + Pad + Table * (ColumnWidth + ColumnGap);
		const float Right = Left + ColumnWidth;
		const FLinearColor Colour = Accent(Team);
		float RowY = Y + 74.0f;

		// The team's name, and the lives it has left on the right (a free-for-all has none to count).
		Painter.Text(FVector2f(Left, RowY), TeamName(Model, Team).ToUpper(), Font(EWeight::Bold, 12.0f, 220), Colour);
		if (!bFreeForAll)
		{
			const FVector2f NumberSize = Painter.Text(FVector2f(Right, RowY - 1.0f), LivesText(Model, Round.TeamLives[Team], 1000), Font(EWeight::Bold, 13.0f), Text(), EAlign::Right);
			Painter.Text(FVector2f(Right - NumberSize.X - 10.0f, RowY + 3.0f), Model.Label(EScctHudLabel::Lives).ToUpper(), Label(7.5f), TextDim(), EAlign::Right);
		}
		RowY += 26.0f;
		Painter.GlowLine(FVector2f(Left, RowY), FVector2f(Right, RowY), Fade(Colour, 0.9f), 1.5f);
		RowY += 10.0f;

		// Column headings, right-aligned over their numbers. The first is what the original's score column
		// counts for that side: objectives for spies (kills in a deathmatch), kills for mercs. A free-for-all has
		// unlimited lives, so that column is left out.
		const bool bObjectives = Team == 0 && !IsDeathmatch(Round);
		const FString Headings[] = {
			Model.Label(bObjectives ? EScctHudLabel::Objectives : EScctHudLabel::Kills),
			Model.Label(EScctHudLabel::Deaths),
			bFreeForAll ? FString() : Model.Label(EScctHudLabel::Lives),
			Model.Label(EScctHudLabel::Ping),
		};
		const float Columns[] = { Right - 246.0f, Right - 158.0f, Right - 82.0f, Right - 12.0f };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Columns); ++Index)
		{
			Painter.Text(FVector2f(Columns[Index], RowY), Headings[Index].ToUpper(), Label(7.0f), TextFaint(), EAlign::Right);
		}
		RowY += 20.0f;

		for (const FScctHudScoreRow* Row : Tables[Table])
		{
			const bool bLocal = (Row->Flags & EScctHudScoreFlags::Local) != 0;
			const bool bOut = (Row->Flags & EScctHudScoreFlags::OutOfLives) != 0;
			if (bLocal)
			{
				Painter.Fill(HudBox(Left - 8.0f, RowY, ColumnWidth + 16.0f, RowHeight - 2.0f), Fade(Colour, 0.12f), 4.0f);
				Painter.GlowLine(FVector2f(Left - 7.0f, RowY + 7.0f), FVector2f(Left - 7.0f, RowY + RowHeight - 9.0f), Colour, 2.0f);
			}

			const FLinearColor NameColour = bOut ? TextFaint() : (bLocal ? Text() : Fade(Text(), 0.86f));
			const FVector2f NameSize = Painter.Text(FVector2f(Left + 6.0f, RowY + 6.0f), Model.Text(Row->NameId), Font(bLocal ? EWeight::Bold : EWeight::Regular, 11.5f), NameColour);
			if ((Row->Flags & EScctHudScoreFlags::HasFlag) != 0)
			{
				// Carrying the disk.
				const FVector2f Disk(Left + 6.0f + NameSize.X + 14.0f, RowY + 14.0f);
				Painter.Polyline({ Disk + FVector2f(0.0f, -4.0f), Disk + FVector2f(4.0f, 0.0f), Disk + FVector2f(0.0f, 4.0f), Disk + FVector2f(-4.0f, 0.0f), Disk + FVector2f(0.0f, -4.0f) }, Colour, 1.5f);
			}

			const FSlateFontInfo NumberFont = Font(EWeight::Regular, 11.5f);
			const FLinearColor NumberColour = bOut ? TextFaint() : Fade(Text(), 0.86f);
			Painter.Text(FVector2f(Columns[0], RowY + 6.0f), FString::FromInt(Row->ObjOrKills), Font(EWeight::Bold, 11.5f), bOut ? TextFaint() : Text(), EAlign::Right);
			Painter.Text(FVector2f(Columns[1], RowY + 6.0f), FString::FromInt(Row->Deaths), NumberFont, NumberColour, EAlign::Right);
			if (!bFreeForAll)
			{
				Painter.Text(FVector2f(Columns[2], RowY + 6.0f), LivesText(Model, Row->Lives, 100), NumberFont, NumberColour, EAlign::Right);
			}
			const FLinearColor PingColour = Row->Ping > 150 ? Danger() : (Row->Ping > 80 ? Text() : Fade(Good(), 0.9f));
			Painter.Text(FVector2f(Columns[3], RowY + 6.0f), FString::FromInt(Row->Ping), NumberFont, bOut ? TextFaint() : PingColour, EAlign::Right);
			RowY += RowHeight;
		}
	}
	Painter.SetOpacity(1.0f);
}
