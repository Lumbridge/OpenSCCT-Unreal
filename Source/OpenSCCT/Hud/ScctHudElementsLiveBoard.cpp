#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"
#include "Menu/ScctRankIcons.h"

// The scoreboard with the delayed combat columns (the Godot client's ScoreboardOverlay and RoundScoreboardView's live
// board): a card in the middle of the screen, two thirds of its width (960 to 1200 units), with the map and the
// round's clock, the line that says how old the combat columns are and when the next release comes, and one table per
// side, the local player's first. A table's columns are the rank, the name (three times a number column's width), the
// side's columns from the bridge and the ping in its own box at the right. On a low screen the board is drawn denser.
// The mission status page puts the mission's objectives under it, as the plain board does.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	/** The text cut to Width with an ellipsis. */
	FString LiveBoardFit(const FScctHudPainter& Painter, const FString& String, const FSlateFontInfo& Font, float Width)
	{
		if (String.IsEmpty() || Painter.Measure(String, Font).X <= Width)
		{
			return String;
		}
		int32 Low = 0, High = String.Len();
		while (Low < High)
		{
			const int32 Middle = (Low + High + 1) / 2;
			if (Painter.Measure(String.Left(Middle) + TEXT("…"), Font).X <= Width)
			{
				Low = Middle;
			}
			else
			{
				High = Middle - 1;
			}
		}
		return String.Left(Low).TrimEnd() + TEXT("…");
	}

	bool LiveBoardFreeForAll(const FScctHudModel& Model)
	{
		return Model.bHasRound && Model.Round.GameMode == 7;
	}
}

bool ScctHudElements::PaintLiveScoreboard(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FScctHudLiveBoard& Board = Model.LiveBoard;
	const float Shown = FMath::Max(Model.ScoreboardShown, Model.StatusShown);
	if (!Board.bValid || Shown <= 0.01f)
	{
		return false;
	}

	const FVector2f Size = Painter.Size();
	const float Width = FMath::Min(FMath::Clamp(Size.X * 0.67f, 960.0f, 1200.0f), FMath::Max(Size.X - 32.0f, 0.0f));
	const bool bCompact = Width < 1000.0f || Size.Y - 52.0f < 700.0f;
	const bool bFreeForAll = LiveBoardFreeForAll(Model);

	// Godot's 15 pixel type (12 when dense) in Slate's points.
	const float Points = bCompact ? 9.0f : 11.0f;
	const float RowHeight = bCompact ? 17.0f : 24.0f;
	const float HeadHeight = bCompact ? 16.0f : 20.0f;
	const float TeamHeight = bCompact ? 20.0f : 26.0f;
	const float TeamGap = bCompact ? 6.0f : 10.0f;
	const float LineHeight = bCompact ? 16.0f : 20.0f;
	constexpr float Pad = 16.0f, Gap = 3.0f, Inset = 3.0f;
	const float TopHeight = bCompact ? 46.0f : 58.0f;

	const FSlateFontInfo CellFont = Font(EWeight::Regular, Points);
	const FSlateFontInfo BoldFont = Font(EWeight::Bold, Points);
	const FSlateFontInfo HeadFont = Font(EWeight::Regular, Points - 1.0f);

	// The rows of each side, in the board's order.
	TArray<int32, TInlineAllocator<24>> Tables[2];
	for (int32 Row = 0; Row < Board.Rows.Num(); ++Row)
	{
		Tables[Board.Rows[Row].Team == 1 ? 1 : 0].Add(Row);
	}
	const int32 Order[2] = { Board.Info.FirstTeam == 1 ? 1 : 0, Board.Info.FirstTeam == 1 ? 0 : 1 };
	auto TableDrawn = [&](int32 Team) { return !(bFreeForAll && Tables[Team].Num() == 0); };

	// Who carries the disk, from the HUD's own player list (the board does not say).
	TArray<FString, TInlineAllocator<2>> Carriers;
	for (const FScctHudScoreRow& Row : Model.Scores)
	{
		if ((Row.Flags & EScctHudScoreFlags::HasFlag) != 0)
		{
			Carriers.Add(Model.Text(Row.NameId));
		}
	}

	float TablesHeight = TopHeight;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (TableDrawn(Team))
		{
			TablesHeight += TeamHeight + HeadHeight + Tables[Team].Num() * RowHeight + TeamGap;
		}
	}
	const bool bEmpty = Board.Rows.Num() == 0;
	const int32 Notes = (Board.Omitted.IsEmpty() ? 0 : 1) + (Board.Spectators.IsEmpty() ? 0 : 1) + (bEmpty ? 1 : 0);
	TablesHeight += Notes * LineHeight + Pad - TeamGap;

	// The status page's objective list is measured first: the page is as tall as its text.
	const bool bObjectiveList = Model.StatusShown > 0.01f && Model.ObjectiveLines.Num() > 0;
	const FSlateFontInfo ObjectiveFont = Font(EWeight::Regular, 11.0f);
	const float ObjectiveLineHeight = Painter.Measure(TEXT("Ag"), ObjectiveFont).Y + 2.0f;
	TArray<TArray<FString>> ObjectiveText;
	float ObjectivesHeight = 0.0f;
	if (bObjectiveList)
	{
		ObjectivesHeight = 44.0f;
		for (const FScctHudObjectiveLine& Line : Model.ObjectiveLines)
		{
			TArray<FString>& Lines = ObjectiveText.AddDefaulted_GetRef();
			Painter.Wrap(Model.Text(Line.TextId), ObjectiveFont, Width - Pad * 2.0f - 24.0f, 2, Lines);
			ObjectivesHeight += FMath::Max(Lines.Num(), 1) * ObjectiveLineHeight + 7.0f;
		}
	}

	const float Height = TablesHeight + ObjectivesHeight * Model.StatusShown;
	const float X = (Size.X - Width) * 0.5f;
	const float Y = FMath::Max((Size.Y - Height) * 0.5f, 8.0f) + (1.0f - EaseOut(Shown)) * 14.0f;
	const float Left = X + Pad, Right = X + Width - Pad;

	Painter.SetOpacity(Shown);
	Painter.Fill(FBox2f(FVector2f::ZeroVector, Size), FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
	Painter.Blur(HudBox(X, Y, Width, Height), 22.0f, 6.0f);
	Painter.Fill(HudBox(X, Y, Width, Height), Srgb(6, 9, 11, 0.90f), 6.0f, Srgb(64, 89, 102, 0.7f), 1.0f);

	// The map with the round's clock on the right, and the timing line under them.
	Painter.Text(FVector2f(Left, Y + (bCompact ? 8.0f : 10.0f)), LiveBoardFit(Painter, Board.Title, Font(EWeight::Regular, Points + 4.0f), Width - Pad * 2.0f - 90.0f),
		Font(EWeight::Regular, Points + 4.0f), Text());
	Painter.Text(FVector2f(Right, Y + (bCompact ? 11.0f : 14.0f)), Board.Clock, CellFont, TextDim(), EAlign::Right);
	Painter.Text(FVector2f(Left, Y + (bCompact ? 28.0f : 36.0f)), Board.Timing, CellFont, TextDim());

	// The columns: rank, name, the side's own, and the ping's box at the right.
	const float RankWidth = Painter.Measure(TEXT("Master · 55"), CellFont).X + Points * 1.4f + 12.0f;
	const float PingWidth = bCompact ? 46.0f : 60.0f;
	const float RowLeft = Left + Inset, RowRight = Right - Inset;
	const FLinearColor PingText = Srgb(135, 176, 199), PingHead = Srgb(166, 201, 224);

	float RowY = Y + TopHeight;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const int32 Team = Order[Slot];
		if (!TableDrawn(Team))
		{
			continue;
		}
		const TArray<FString>& Columns = Board.Columns[Team];
		const int32 StatCount = FMath::Max(Columns.Num() - 1, 0);
		const FLinearColor Colour = Accent(Team);

		// The side and how many play on it; the lives the side has left on the right.
		const FString& TeamName = Model.Label(Team == 0 ? EScctHudLabel::Spies : EScctHudLabel::Mercenaries);
		Painter.Text(FVector2f(Left, RowY + 2.0f), FString::Printf(TEXT("%s  ·  %d"), *(TeamName.IsEmpty() ? FString(Team == 0 ? TEXT("SPIES") : TEXT("MERCENARIES")) : TeamName.ToUpper()), Tables[Team].Num()),
			Font(EWeight::Bold, Points + 1.0f, 120), Colour);
		if (!bFreeForAll && Model.bHasRound)
		{
			const FVector2f NumberSize = Painter.Text(FVector2f(Right, RowY + 2.0f), LivesText(Model, Model.Round.TeamLives[Team], 1000), BoldFont, Text(), EAlign::Right);
			Painter.Text(FVector2f(Right - NumberSize.X - 8.0f, RowY + 5.0f), Model.Label(EScctHudLabel::Lives).ToUpper(), Label(7.0f), TextDim(), EAlign::Right);
		}
		RowY += TeamHeight;

		const float StatsRight = RowRight - PingWidth - Gap;
		const float NameLeft = RowLeft + RankWidth + Gap;
		const float Unit = FMath::Max((StatsRight - NameLeft - Gap * StatCount) / (2.8f + StatCount), 1.0f);
		const float NameWidth = Unit * 2.8f;
		auto StatCentre = [&](int32 Column) { return NameLeft + NameWidth + Gap + Column * (Unit + Gap) + Unit * 0.5f; };
		const float TextDrop = (RowHeight - Painter.Measure(TEXT("Ag"), CellFont).Y) * 0.5f;

		// The headings.
		const float HeadDrop = (HeadHeight - Painter.Measure(TEXT("Ag"), HeadFont).Y) * 0.5f;
		Painter.Text(FVector2f(RowLeft, RowY + HeadDrop), TEXT("Rank"), HeadFont, TextDim());
		Painter.Text(FVector2f(NameLeft, RowY + HeadDrop), TEXT("Name"), HeadFont, TextDim());
		for (int32 Column = 0; Column < StatCount; ++Column)
		{
			const FString& Heading = Columns[Column];
			Painter.Text(FVector2f(StatCentre(Column), RowY + HeadDrop), bCompact && Heading == TEXT("Cam sleep") ? FString(TEXT("Cam KO")) : Heading, HeadFont, TextDim(), EAlign::Centre);
		}
		if (Columns.Num() > 0)
		{
			Painter.Fill(HudBox(RowRight - PingWidth, RowY, PingWidth, HeadHeight - 1.0f), Srgb(26, 46, 61, 0.8f));
			Painter.Fill(HudBox(RowRight - PingWidth, RowY, 1.0f, HeadHeight - 1.0f), Srgb(82, 122, 148, 0.7f));
			Painter.Text(FVector2f(RowRight - 5.0f, RowY + HeadDrop), Columns.Last(), HeadFont, PingHead, EAlign::Right);
		}
		RowY += HeadHeight;

		for (const int32 Index : Tables[Team])
		{
			const FScctLobbyScoreRow& Row = Board.Rows[Index];
			const bool bLocal = (Row.Flags & EScctLobbyScoreRowFlags::Local) != 0;
			const bool bLeft = (Row.Flags & EScctLobbyScoreRowFlags::LeftEarly) != 0;
			const bool bBot = (Row.Flags & EScctLobbyScoreRowFlags::Bot) != 0;
			const float BarHeight = RowHeight - (bCompact ? 0.0f : 1.0f);
			Painter.Fill(HudBox(Left, RowY, Width - Pad * 2.0f, BarHeight), bLocal ? Fade(Colour, 0.16f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.025f));

			// The rank: its emblem and its tag; a dash for a guest or a bot.
			const float IconSize = FMath::Min(Points * 1.4f, RowHeight - 2.0f);
			if (const FSlateBrush* RankBrush = ScctRankIcons::ForPacked(Row.Rank, IconSize))
			{
				Painter.Image(HudBox(RowLeft, RowY + (RowHeight - IconSize) * 0.5f, IconSize, IconSize), *RankBrush, FBox2f(FVector2f::ZeroVector, FVector2f::UnitVector), bLeft ? Fade(FLinearColor::White, 0.45f) : FLinearColor::White);
				if (Board.RankTags.IsValidIndex(Index))
				{
					Painter.Text(FVector2f(RowLeft + IconSize + 5.0f, RowY + TextDrop), Board.RankTags[Index], CellFont, bLeft ? TextFaint() : Fade(Text(), 0.86f));
				}
			}
			else
			{
				Painter.Text(FVector2f(RowLeft, RowY + TextDrop), TEXT("—"), CellFont, TextDim());
			}

			const FString Name(Row.Name);
			const bool bCarrier = Carriers.Contains(Name);
			const FString NameText = LiveBoardFit(Painter, Name + (bLeft ? TEXT(" · left") : (bBot ? TEXT(" · bot") : TEXT(""))), bLocal ? BoldFont : CellFont, NameWidth - (bCarrier ? 22.0f : 4.0f));
			const FVector2f NameSize = Painter.Text(FVector2f(NameLeft, RowY + TextDrop), NameText, bLocal ? BoldFont : CellFont, bLeft ? TextFaint() : (bLocal ? Colour : Text()));
			if (bCarrier)
			{
				// Carrying the disk.
				const FVector2f Disk(NameLeft + NameSize.X + 11.0f, RowY + RowHeight * 0.5f);
				Painter.Polyline({ Disk + FVector2f(0.0f, -4.0f), Disk + FVector2f(4.0f, 0.0f), Disk + FVector2f(0.0f, 4.0f), Disk + FVector2f(-4.0f, 0.0f), Disk + FVector2f(0.0f, -4.0f) }, Colour, 1.5f);
			}

			const TArray<FString>* Cells = Board.Cells.IsValidIndex(Index) ? &Board.Cells[Index] : nullptr;
			for (int32 Column = 0; Cells != nullptr && Column < StatCount && Column < Cells->Num(); ++Column)
			{
				Painter.Text(FVector2f(StatCentre(Column), RowY + TextDrop), (*Cells)[Column], CellFont, bLeft ? TextFaint() : Text(), EAlign::Centre);
			}
			if (Cells != nullptr && Cells->Num() > 0)
			{
				Painter.Fill(HudBox(RowRight - PingWidth, RowY, PingWidth, BarHeight), Srgb(26, 46, 61, 0.55f));
				Painter.Fill(HudBox(RowRight - PingWidth, RowY, 1.0f, BarHeight), Srgb(82, 122, 148, 0.7f));
				Painter.Text(FVector2f(RowRight - 5.0f, RowY + TextDrop), Cells->Last(), CellFont, PingText, EAlign::Right);
			}
			RowY += RowHeight;
		}
		RowY += TeamGap;
	}

	// What the tables leave out, who watches, and an empty board's line.
	RowY += 2.0f - TeamGap * 0.5f;
	if (!Board.Omitted.IsEmpty())
	{
		Painter.Text(FVector2f(Left, RowY), Board.Omitted, CellFont, TextDim());
		RowY += LineHeight;
	}
	if (!Board.Spectators.IsEmpty())
	{
		Painter.Text(FVector2f(Left, RowY), LiveBoardFit(Painter, Board.Spectators, CellFont, Width - Pad * 2.0f), CellFont, TextDim());
		RowY += LineHeight;
	}
	if (bEmpty)
	{
		Painter.Text(FVector2f(Left, RowY), TEXT("No player scores were recorded."), CellFont, TextDim());
	}

	if (bObjectiveList)
	{
		// The mission's objectives for the player's side, ticked when done, as the original's page lists them.
		const FLinearColor Colour = Accent(Model.LocalTeam);
		Painter.SetOpacity(Shown * Model.StatusShown);
		float ListY = Y + TablesHeight - 4.0f;
		Painter.Line(FVector2f(Left, ListY), FVector2f(Right, ListY), Fade(Text(), 0.10f), 1.0f);
		ListY += 14.0f;
		Painter.Text(FVector2f(Left, ListY), Model.Label(EScctHudLabel::Objectives).ToUpper(), Label(8.0f), TextDim());
		ListY += 22.0f;
		for (int32 Index = 0; Index < Model.ObjectiveLines.Num() && Index < ObjectiveText.Num(); ++Index)
		{
			const bool bDone = Model.ObjectiveLines[Index].Done != 0;
			const FBox2f Box = HudBox(Left, ListY + 3.0f, 10.0f, 10.0f);
			if (bDone)
			{
				Painter.Fill(Box, Fade(Colour, 0.85f), 2.0f);
				Painter.Polyline({ Box.Min + FVector2f(2.0f, 5.0f), Box.Min + FVector2f(4.5f, 7.5f), Box.Min + FVector2f(8.0f, 2.5f) }, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), 1.5f);
			}
			else
			{
				Painter.Fill(Box, FLinearColor::Transparent, 2.0f, Fade(Text(), 0.55f), 1.0f);
			}
			for (const FString& Line : ObjectiveText[Index])
			{
				Painter.Text(FVector2f(Left + 24.0f, ListY), Line, ObjectiveFont, bDone ? TextFaint() : Text());
				ListY += ObjectiveLineHeight;
			}
			ListY += 7.0f;
		}
	}
	Painter.SetOpacity(1.0f);
	return true;
}
