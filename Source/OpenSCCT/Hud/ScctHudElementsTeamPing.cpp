#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudStyle.h"
#include "Runtime/ScctTeamPingApi.h"

// The team ping on the full-screen layer, as Godot's TeamPingOverlay draws it: each ping a dark disc with its kind's
// icon in the team's colour, lifted above the marked spot on a stem with the distance under the spot, or held at the
// screen's edge with an arrow pointing the way when the spot is out of view; and while the TeamPing key is held, the
// wheel at the centre with its kinds around a cancel cross. Sizes follow Godot's, which are for 720 lines, scaled with
// the screen's height. UScctTeamPingSubsystem places everything; this only draws.

#define LOCTEXT_NAMESPACE "ScctTeamPing"

namespace
{
	using namespace ScctHudStyle;

	// TeamPingOverlay's colours.
	FLinearColor PanelColour(float Alpha = 0.91f) { return Srgb(6, 12, 15, Alpha); }
	FLinearColor OutlineColour(float Alpha = 0.78f) { return Srgb(107, 143, 153, Alpha); }
	FLinearColor TextColour() { return Srgb(237, 245, 247); }

	/** The kind's colour: a mine is danger red, the rest the team's (spies teal, mercs amber). */
	FLinearColor KindColour(int32 Team, int32 Kind)
	{
		if (Kind == EScctTeamPingKind::Mine)
		{
			return Srgb(255, 125, 97);
		}
		return Team == 0 ? Srgb(112, 232, 214) : Srgb(255, 196, 110);
	}

	FText KindLabel(int32 Kind)
	{
		switch (Kind)
		{
		case EScctTeamPingKind::Watch: return LOCTEXT("Watch", "Watch here");
		case EScctTeamPingKind::Check: return LOCTEXT("Check", "Check here");
		case EScctTeamPingKind::CoopBoost: return LOCTEXT("Boost", "Co-op boost");
		case EScctTeamPingKind::Mine: return LOCTEXT("Mine", "Mine");
		default: return LOCTEXT("Ping", "Ping");
		}
	}

	void Disc(FScctHudPainter& Painter, const FVector2f& Centre, float Radius, const FLinearColor& Fill, const FLinearColor& Outline, float Stroke)
	{
		Painter.Fill(FBox2f(Centre - FVector2f(Radius), Centre + FVector2f(Radius)), Fill, Radius, Outline, Stroke);
	}

	/** Points of an icon drawn in a 22-unit box around its centre, at Scale. */
	TArray<FVector2f> Shape(std::initializer_list<FVector2f> Points, const FVector2f& Centre, float Scale)
	{
		TArray<FVector2f> Out;
		Out.Reserve(static_cast<int32>(Points.size()));
		for (const FVector2f& Point : Points)
		{
			Out.Add(Centre + Point * Scale);
		}
		return Out;
	}

	/** The kind's icon (TeamPingOverlay.DrawIcon): a pin, an eye, a question mark, two chevrons, a mine. */
	void Icon(FScctHudPainter& Painter, int32 Kind, const FVector2f& Centre, float Scale, const FLinearColor& Colour)
	{
		switch (Kind)
		{
		case EScctTeamPingKind::Watch:
			Painter.Polyline(Shape({ { -11.0f, 0.0f }, { -6.0f, -4.4f }, { 0.0f, -6.0f }, { 6.0f, -4.4f }, { 11.0f, 0.0f },
				{ 6.0f, 4.4f }, { 0.0f, 6.0f }, { -6.0f, 4.4f }, { -11.0f, 0.0f } }, Centre, Scale), Colour, 1.6f * Scale);
			Disc(Painter, Centre, 2.6f * Scale, Colour, FLinearColor::Transparent, 0.0f);
			break;
		case EScctTeamPingKind::Check:
			Painter.Polyline(Shape({ { -4.5f, -4.0f }, { -3.0f, -7.0f }, { 0.0f, -8.0f }, { 3.0f, -7.0f }, { 4.5f, -4.5f },
				{ 3.5f, -2.0f }, { 0.0f, 0.5f }, { 0.0f, 3.0f } }, Centre, Scale), Colour, 2.0f * Scale);
			Disc(Painter, Centre + FVector2f(0.0f, 7.5f) * Scale, 1.2f * Scale, Colour, FLinearColor::Transparent, 0.0f);
			break;
		case EScctTeamPingKind::CoopBoost:
			Painter.Polyline(Shape({ { -9.0f, 0.0f }, { 0.0f, -8.0f }, { 9.0f, 0.0f } }, Centre, Scale), Colour, 2.0f * Scale);
			Painter.Polyline(Shape({ { -9.0f, 6.0f }, { 0.0f, -2.0f }, { 9.0f, 6.0f } }, Centre, Scale), Colour, 2.0f * Scale);
			break;
		case EScctTeamPingKind::Mine:
			// The Godot client draws its proximity mine picture; this is the same idea in lines: a body with four prongs.
			Disc(Painter, Centre, 5.0f * Scale, FLinearColor::Transparent, Colour, 1.6f * Scale);
			Disc(Painter, Centre, 1.6f * Scale, Colour, FLinearColor::Transparent, 0.0f);
			for (const FVector2f& Way : { FVector2f(0.0f, -1.0f), FVector2f(1.0f, 0.0f), FVector2f(0.0f, 1.0f), FVector2f(-1.0f, 0.0f) })
			{
				Painter.Line(Centre + Way * (6.0f * Scale), Centre + Way * (10.0f * Scale), Colour, 1.8f * Scale);
			}
			break;
		default:
			Painter.Polyline(Shape({ { -8.0f, -1.0f }, { 0.0f, -9.0f }, { 8.0f, -1.0f }, { 0.0f, 9.0f }, { -8.0f, -1.0f } }, Centre, Scale), Colour, 1.6f * Scale);
			Disc(Painter, Centre, 1.9f * Scale, Colour, FLinearColor::Transparent, 0.0f);
			break;
		}
	}

	void PaintBadge(FScctHudPainter& Painter, const FScctHudModel& Model, const FScctTeamPingBadge& Badge, float K)
	{
		const float Alpha = FMath::Clamp(Badge.Opacity, 0.0f, 1.0f);
		const FLinearColor Accent = Fade(KindColour(Badge.Team, Badge.Kind), Alpha);
		const float Radius = 13.0f * K;
		FVector2f Point = Badge.Screen;
		const FScctHudState& State = Model.State;
		const FString Distance = FString::Printf(TEXT("%.0f %s"), Badge.Distance * (State.DistanceScale > 0.0f ? State.DistanceScale : 0.01f), *Model.Text(State.DistanceUnitId)).TrimEnd();
		const FSlateFontInfo SmallFont = Font(EWeight::Regular, 7.0f * K);
		const FSlateFontInfo NameFont = Label(6.5f * K);
		if (Badge.bAtEdge)
		{
			// An arrow beyond the disc, pointing the way to the spot.
			const FVector2f Way = Badge.Direction;
			const FVector2f Across(-Way.Y, Way.X);
			const FVector2f Base = Point + Way * (Radius + 2.0f * K);
			const FVector2f Tip = Base + Way * (8.0f * K);
			Painter.Polyline({ Tip, Base + Across * (4.5f * K), Base - Across * (4.5f * K), Tip }, Accent, 2.0f * K);
			Painter.Line(Tip, Base, Accent, 4.0f * K);
			Disc(Painter, Point, Radius, PanelColour(0.91f * Alpha), Accent, 1.5f * K);
			Icon(Painter, Badge.Kind, Point, 0.78f * K, Accent);
			// The distance on the inner side of the disc, away from the edge.
			const FVector2f TextAt = Point - Way * (Radius + 10.0f * K) - FVector2f(0.0f, 5.0f * K);
			Painter.Text(TextAt, Distance, SmallFont, Fade(TextColour(), Alpha), FScctHudPainter::EAlign::Centre);
			return;
		}

		// Above the spot on a stem, with a dot on the spot itself and the distance beside the disc (a ping at the aim
		// point would put text under it right in the crosshair).
		Point.Y -= Radius + 6.0f * K;
		Painter.Line(Point + FVector2f(0.0f, Radius), Badge.Screen, Accent, 1.5f * K);
		Disc(Painter, Badge.Screen, 2.3f * K, Accent, FLinearColor::Transparent, 0.0f);
		Disc(Painter, Point, Radius, PanelColour(0.91f * Alpha), Accent, 1.5f * K);
		Icon(Painter, Badge.Kind, Point, 0.78f * K, Accent);
		Painter.Text(Point + FVector2f(Radius + 4.0f * K, -6.0f * K), Distance, SmallFont, Fade(TextColour(), Alpha), FScctHudPainter::EAlign::Left);
		if (!Badge.bOwn && !Badge.Sender.IsEmpty())
		{
			Painter.Text(Point - FVector2f(0.0f, Radius + 11.0f * K), Badge.Sender.ToUpper(), NameFont, Fade(TextColour(), Alpha), FScctHudPainter::EAlign::Centre);
		}
	}

	void PaintWheelOption(FScctHudPainter& Painter, int32 Kind, const FVector2f& Point, int32 Team, int32 Selected, float K)
	{
		const bool bSelected = Kind == Selected;
		const FLinearColor Accent = KindColour(Team, Kind);
		const float Radius = (bSelected ? 21.0f : 19.0f) * K;
		Disc(Painter, Point, Radius, bSelected ? Accent : PanelColour(), bSelected ? TextColour() : OutlineColour(), (bSelected ? 1.8f : 1.0f) * K);
		Icon(Painter, Kind, Point, K, bSelected ? PanelColour() : Accent);
		Painter.Text(Point + FVector2f(0.0f, Radius + 3.0f * K), KindLabel(Kind).ToString(), Font(EWeight::Regular, 9.0f * K), bSelected ? Accent : TextColour(), FScctHudPainter::EAlign::Centre);
	}

	void PaintWheel(FScctHudPainter& Painter, const FScctTeamPingView& View, float K)
	{
		const FVector2f Centre = Painter.Size() * 0.5f;
		const float Spread = 63.0f * K;
		Disc(Painter, Centre, Spread + 22.0f * K, PanelColour(0.57f), FLinearColor::Transparent, 0.0f);
		Painter.Arc(Centre, Spread, 0.0f, 360.0f, OutlineColour(0.35f), K);
		PaintWheelOption(Painter, EScctTeamPingKind::Watch, Centre + FVector2f(0.0f, -Spread), View.WheelTeam, View.Selection, K);
		PaintWheelOption(Painter, EScctTeamPingKind::Check, Centre + FVector2f(Spread, 0.0f), View.WheelTeam, View.Selection, K);
		if (View.WheelTeam == 0)
		{
			PaintWheelOption(Painter, EScctTeamPingKind::CoopBoost, Centre + FVector2f(0.0f, Spread), View.WheelTeam, View.Selection, K);
			PaintWheelOption(Painter, EScctTeamPingKind::Mine, Centre + FVector2f(-Spread, 0.0f), View.WheelTeam, View.Selection, K);
		}

		// The middle: letting go here sends nothing; its cross is lit while nothing is chosen.
		Disc(Painter, Centre, 14.0f * K, PanelColour(), OutlineColour(), K);
		const FLinearColor Cancel = View.Selection < 0 ? TextColour() : OutlineColour();
		const FVector2f Cross(4.5f * K, 4.5f * K);
		Painter.Line(Centre - Cross, Centre + Cross, Cancel, 1.6f * K);
		Painter.Line(Centre + FVector2f(-Cross.X, Cross.Y), Centre + FVector2f(Cross.X, -Cross.Y), Cancel, 1.6f * K);
	}
}

void ScctHudElements::PaintTeamPings(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FScctTeamPingView& View = Model.TeamPing;
	if (!View.bWheelOpen && View.Badges.Num() == 0)
	{
		return;
	}
	// Godot's scale: its sizes are for 720 lines; the layer's units are 1080p pixels.
	const float K = FMath::Clamp(Painter.Size().Y / 720.0f, 0.75f, 2.25f);
	if (View.bWheelOpen)
	{
		// The wheel covers the screen's centre; the badges wait for it to close, as in the Godot client.
		PaintWheel(Painter, View, K);
		return;
	}
	for (const FScctTeamPingBadge& Badge : View.Badges)
	{
		if (Badge.Opacity > 0.0f)
		{
			PaintBadge(Painter, Model, Badge, K);
		}
	}
}

#undef LOCTEXT_NAMESPACE
