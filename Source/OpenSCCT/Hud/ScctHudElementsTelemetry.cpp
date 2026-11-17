#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudStyle.h"
#include "Hud/ScctTelemetryView.h"

// The frame-rate and network readouts on the safe-area layer, as Godot's HudFps and HudNet draw them: a dark panel per
// readout, labels in the left column, values in the right, problems in the warning colour, and under a detailed readout
// its history graphs with their threshold line. The panels stack in the chosen corner, the frame rate nearest the corner.
// UScctTelemetrySubsystem prepares every line; this only lays them out.

namespace
{
	using namespace ScctHudStyle;

	// Godot's HudFps / HudNet colours.
	FLinearColor TelemetryPanelColour() { return FLinearColor(0.0019f, 0.0030f, 0.0045f, 0.8f); }
	FLinearColor TelemetryAccentColour() { return Srgb(128, 191, 217, 0.8f); }
	FLinearColor TelemetryLabelColour() { return Srgb(148, 184, 204); }
	FLinearColor TelemetryValueColour() { return Srgb(240, 247, 255); }
	FLinearColor TelemetryWarningColour() { return Srgb(255, 199, 102); }

	/** The readouts' measures at a size K (1 at 1080p and the default size). */
	struct FTelemetryMetrics
	{
		float K;
		float Row;
		float Pad;
		float Header;
		float GraphHeight;
		float Gap;

		explicit FTelemetryMetrics(float InK)
			: K(InK), Row(15.0f * InK), Pad(8.0f * InK), Header(14.0f * InK), GraphHeight(34.0f * InK), Gap(6.0f * InK)
		{
		}

		float PanelHeight(const FScctTelemetryPanel& Panel) const
		{
			if (!Panel.bShown)
			{
				return 0.0f;
			}
			float Height = Pad * 2.0f + Panel.Rows.Num() * Row;
			if (!Panel.Header.IsEmpty())
			{
				Height += Header + Pad * 0.5f;
			}
			Height += Panel.Graphs.Num() * (Pad * 0.5f + Row + GraphHeight);
			return Height;
		}
	};

	float TelemetryStackHeight(const FScctTelemetryView& View)
	{
		const FTelemetryMetrics M(View.Scale);
		const float Fps = M.PanelHeight(View.Fps);
		const float Net = M.PanelHeight(View.Net);
		return Fps + Net + (Fps > 0.0f && Net > 0.0f ? M.Gap : 0.0f);
	}

	float TelemetryPanelWidth(const FScctHudPainter& Painter, const FScctTelemetryPanel& Panel, const FTelemetryMetrics& M, float& OutLabelWidth)
	{
		const FSlateFontInfo LabelFont = Label(7.5f * M.K);
		const FSlateFontInfo ValueFont = Font(EWeight::Regular, 9.5f * M.K);
		OutLabelWidth = 0.0f;
		float Values = 0.0f;
		for (const FScctTelemetryRow& Row : Panel.Rows)
		{
			if (!Row.Label.IsEmpty())
			{
				OutLabelWidth = FMath::Max(OutLabelWidth, Painter.Measure(Row.Label, LabelFont).X + 10.0f * M.K);
			}
			Values = FMath::Max(Values, Painter.Measure(Row.Value, ValueFont).X);
		}
		float Width = M.Pad * 2.0f + OutLabelWidth + Values;
		if (!Panel.Header.IsEmpty())
		{
			Width = FMath::Max(Width, M.Pad * 3.0f + Painter.Measure(Panel.Header, LabelFont).X + Painter.Measure(Panel.Tag, LabelFont).X);
		}
		for (const FScctTelemetryGraph& Graph : Panel.Graphs)
		{
			Width = FMath::Max(Width, M.Pad * 2.0f + Painter.Measure(Graph.Caption, Label(7.0f * M.K)).X);
		}
		if (Panel.Graphs.Num() > 0)
		{
			Width = FMath::Max(Width, 250.0f * M.K);
		}
		return Width;
	}

	void PaintTelemetryGraph(FScctHudPainter& Painter, const FScctTelemetryGraph& Graph, const FBox2f& Box)
	{
		Painter.Fill(Box, FLinearColor(0.0f, 0.0f, 0.0f, 0.3f));
		const float Maximum = FMath::Max(Graph.Maximum, 0.001f);
		const float Height = Box.GetSize().Y;
		if (Graph.Threshold > 0.0f && Graph.Threshold <= Maximum)
		{
			const float Y = Box.Max.Y - Height * Graph.Threshold / Maximum;
			Painter.Line(FVector2f(Box.Min.X, Y), FVector2f(Box.Max.X, Y), Fade(TelemetryWarningColour(), 0.5f), 1.0f);
		}
		const int32 Count = Graph.Values.Num();
		if (Count == 0)
		{
			return;
		}
		// A short history covers only its share of the width, at the right (the newest end).
		const float Width = Box.GetSize().X * FMath::Clamp(Graph.Fill, 0.0f, 1.0f);
		const float Left = Box.Max.X - Width;
		TArray<FVector2f> Points;
		Points.Reserve(FMath::Max(Count, 2));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float X = Count > 1 ? Left + Width * Index / (Count - 1) : Box.Max.X - 2.0f;
			const float Y = Box.Max.Y - Height * FMath::Clamp(Graph.Values[Index] / Maximum, 0.0f, 1.0f);
			Points.Add(FVector2f(X, Y));
		}
		if (Count == 1)
		{
			Points.Add(FVector2f(Box.Max.X, Points[0].Y));
		}
		Painter.Polyline(MoveTemp(Points), Graph.Colour, 1.5f);
	}

	void PaintTelemetryPanel(FScctHudPainter& Painter, const FScctTelemetryPanel& Panel, const FTelemetryMetrics& M, const FVector2f& TopLeft, float Width, float LabelWidth)
	{
		const float Height = M.PanelHeight(Panel);
		const FBox2f Box(TopLeft, TopLeft + FVector2f(Width, Height));
		Painter.Fill(Box, TelemetryPanelColour(), 3.0f * M.K, Panel.bIssue ? Fade(TelemetryWarningColour(), 0.6f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), 1.0f);
		if (!Panel.Header.IsEmpty())
		{
			Painter.Fill(FBox2f(TopLeft, TopLeft + FVector2f(2.0f * M.K, Height)), TelemetryAccentColour());
		}

		const FSlateFontInfo LabelFont = Label(7.5f * M.K);
		const FSlateFontInfo ValueFont = Font(EWeight::Regular, 9.5f * M.K);
		const FSlateFontInfo CaptionFont = Label(7.0f * M.K);
		const float Left = TopLeft.X + M.Pad;
		const float Right = TopLeft.X + Width - M.Pad;
		float Y = TopLeft.Y + M.Pad;
		if (!Panel.Header.IsEmpty())
		{
			Painter.Text(FVector2f(Left, Y), Panel.Header, LabelFont, TelemetryLabelColour(), FScctHudPainter::EAlign::Left, false);
			Painter.Text(FVector2f(Right, Y), Panel.Tag.ToUpper(), LabelFont, TelemetryLabelColour(), FScctHudPainter::EAlign::Right, false);
			Y += M.Header + M.Pad * 0.5f;
		}
		for (const FScctTelemetryRow& Row : Panel.Rows)
		{
			if (!Row.Label.IsEmpty())
			{
				// The label's smaller capitals sit on the value's line.
				Painter.Text(FVector2f(Left, Y + 2.0f * M.K), Row.Label, LabelFont, Row.bAlert ? TelemetryWarningColour() : TelemetryLabelColour(), FScctHudPainter::EAlign::Left, false);
			}
			Painter.Text(FVector2f(Left + LabelWidth, Y), Row.Value, ValueFont, Row.bAlert ? TelemetryWarningColour() : TelemetryValueColour(), FScctHudPainter::EAlign::Left, false);
			Y += M.Row;
		}
		for (const FScctTelemetryGraph& Graph : Panel.Graphs)
		{
			Y += M.Pad * 0.5f;
			Painter.Text(FVector2f(Left, Y + 2.0f * M.K), Graph.Caption, CaptionFont, Graph.Colour, FScctHudPainter::EAlign::Left, false);
			Y += M.Row;
			PaintTelemetryGraph(Painter, Graph, FBox2f(FVector2f(Left, Y), FVector2f(Right, Y + M.GraphHeight)));
			Y += M.GraphHeight;
		}
	}
}

float ScctHudElements::TelemetryReservedTopRight(const FScctHudModel& Model)
{
	const FScctTelemetryView& View = Model.Telemetry;
	if (View.Corner != EScctTelemetryCorner::TopRight)
	{
		return 0.0f;
	}
	const float Height = TelemetryStackHeight(View);
	return Height > 0.0f ? Height + 10.0f * View.Scale : 0.0f;
}

void ScctHudElements::PaintTelemetry(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FScctTelemetryView& View = Model.Telemetry;
	if (!View.Fps.bShown && !View.Net.bShown)
	{
		return;
	}
	const FTelemetryMetrics M(View.Scale);
	const FVector2f Size = Painter.Size();
	const bool bRight = View.Corner == EScctTelemetryCorner::TopRight || View.Corner == EScctTelemetryCorner::BottomRight;
	const bool bTop = View.Corner == EScctTelemetryCorner::TopRight || View.Corner == EScctTelemetryCorner::TopLeft;
	// At the top left the radar has the corner: the readouts stand to its right (the radar is about as wide as it is tall).
	const float LeftEdge = Margin + (View.Corner == EScctTelemetryCorner::TopLeft ? RadarReservedHeight(Model) + 12.0f : 0.0f);

	// At the bottom the player's own blocks have the corners: the readouts stand above them. The status block (health,
	// lives) is about 70 units tall; the equipment is the weapon block (70) under the gadget list (24 a row and its padding).
	float BottomReserve = 0.0f;
	if (!bTop && ShowsPlayerHud(Model))
	{
		BottomReserve = bRight ? 70.0f + 15.0f + Model.Gadgets.Num() * 24.0f + 12.0f + 12.0f : 70.0f + 12.0f;
	}

	const float Before = Painter.GetOpacity();
	Painter.SetOpacity(Before * View.Opacity);
	float Y = bTop ? Margin : Size.Y - Margin - BottomReserve;
	for (const FScctTelemetryPanel* Panel : { &View.Fps, &View.Net })
	{
		if (!Panel->bShown)
		{
			continue;
		}
		float LabelWidth = 0.0f;
		const float Width = FMath::Min(TelemetryPanelWidth(Painter, *Panel, M, LabelWidth), FMath::Max(80.0f, Size.X - 2.0f * Margin));
		const float Height = M.PanelHeight(*Panel);
		const float X = bRight ? Size.X - Margin - Width : LeftEdge;
		if (!bTop)
		{
			Y -= Height;
		}
		PaintTelemetryPanel(Painter, *Panel, M, FVector2f(X, Y), Width, LabelWidth);
		Y = bTop ? Y + Height + M.Gap : Y - M.Gap;
	}
	Painter.SetOpacity(Before);
}
