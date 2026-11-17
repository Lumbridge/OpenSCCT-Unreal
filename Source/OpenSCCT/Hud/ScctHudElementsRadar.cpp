#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudTextures.h"

// The radar at the top left (the original's A_Radar on both pages, as the Godot client's HudRadar drives it) with the
// heart beat sensor's cone inside it, and the name of the place the viewed pawn is in above it (Reloaded's location
// readout, HudLocation). Drawn with the content's own pictures (Hud.fond_radar, the arrows of Hud.HUDConfig,
// Hud.ret_center, Hud.zoom_rond01); a picture the content lacks is drawn as plain shapes (the HUD subsystem logs which).

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	// The original's radar numbers (Bridge docs/overlays/radar.md 3.1, as game/Scripts/Hud/HudK.cs has them), in
	// units of the 640 x 480 Magma page.
	constexpr float WorldPerPage = 44.0f;   // world units per page unit
	constexpr float BlipRadius = 49.0f;     // a blip further than this from the centre is out of range
	constexpr float DishRadius = 50.0f;     // the dish picture (FondBoussole's fond, 100 x 100)
	constexpr float LowerBelow = -2.0f;     // a blip more than 2 page units below uses the Lower icon
	constexpr float HigherAbove = 7.0f;     // more than 7 above, the Higher icon
	constexpr int32 MaxTeammates = 2, MaxEnemies = 3, MaxAlarms = 5; // the instances the radar drivers use

	/** HUD units per page unit at Hud.RadarScale 1. */
	constexpr float PageScale = 1.6f;

	/** The place name's line above the dish, and the room kept under the dish before the objectives panel. */
	constexpr float LocationLine = 16.0f;
	constexpr float LocationGap = 4.0f;
	constexpr float BelowRadar = 14.0f;

	/** Blip colours: the instances' state colours on A_Radar (sRGB). */
	FLinearColor KindColour(int32 Kind)
	{
		switch (Kind)
		{
		case 0: return Srgb(200, 247, 184);
		case 1: return Srgb(255, 104, 32);
		default: return Srgb(255, 0, 0);
		}
	}

	/** Texel regions of Hud.HUDConfig's materials. */
	struct FTexels
	{
		float U0, V0, U1, V1;
	};
	constexpr FTexels HigherIcon = { 81.0f, 22.0f, 97.0f, 38.0f }; // flechehaut
	constexpr FTexels LowerIcon = { 99.0f, 18.0f, 115.0f, 34.0f }; // flechebas
	constexpr FTexels EdgeArrow = { 116.0f, 18.0f, 132.0f, 34.0f }; // fleche, pointing right

	struct FRadarFrame
	{
		FVector2f Centre;
		float Unit = PageScale; // HUD units per page unit
		float Cos = 1.0f, Sin = 0.0f;

		/** A world offset on the radar, in page units: forward is up when the radar turns with the view. */
		FVector2f Project(float Dx, float Dy) const
		{
			const float X = Dx / WorldPerPage, Y = Dy / WorldPerPage;
			return FVector2f(X * Cos - Y * Sin, X * Sin + Y * Cos);
		}

		FVector2f ToScreen(const FVector2f& Page) const { return Centre + Page * Unit; }
	};

	FBox2f Square(const FVector2f& Centre, float Half)
	{
		return FBox2f(Centre - FVector2f(Half), Centre + FVector2f(Half));
	}

	/** A blip's pulse: the dot (ret_center) with a ring (zoom_rond01) that grows from it and fades, once a loop. */
	void PaintPulse(FScctHudPainter& Painter, const FRadarFrame& Frame, const FVector2f& At, const FLinearColor& Colour, float Phase,
		const UScctHudTextures::FEntry* Dot, const UScctHudTextures::FEntry* Ring, bool bDot)
	{
		const float RingHalf = (2.5f + 8.0f * Phase) * Frame.Unit;
		const FLinearColor RingColour = Fade(Colour, 0.9f * (1.0f - Phase));
		if (Ring != nullptr)
		{
			Painter.Image(Square(At, RingHalf), Ring->Brush, Ring->Region(0, 0, Ring->Size.X, Ring->Size.Y), RingColour);
		}
		else
		{
			Painter.Arc(At, RingHalf * 0.8f, 0.0f, 360.0f, RingColour, 1.0f);
		}

		if (!bDot)
		{
			return;
		}
		// The dot dims to about 60% and back over the loop (rond's keys: 255, then 150).
		const FLinearColor DotColour = Fade(Colour, 0.8f + 0.2f * FMath::Cos(Phase * UE_TWO_PI));
		if (Dot != nullptr)
		{
			Painter.Image(Square(At, 2.5f * Frame.Unit), Dot->Brush, Dot->Region(0, 0, Dot->Size.X, Dot->Size.Y), DotColour);
		}
		else
		{
			Painter.Fill(Square(At, 1.6f * Frame.Unit), DotColour, 1.6f * Frame.Unit);
		}
	}

	/** A Higher or Lower icon (Hud.HUDConfig's arrow up or down), or a plain triangle without the picture. */
	void PaintHeightIcon(FScctHudPainter& Painter, const FRadarFrame& Frame, const FVector2f& At, const FLinearColor& Colour, bool bHigher,
		const UScctHudTextures::FEntry* Atlas)
	{
		const float Half = 5.0f * Frame.Unit;
		if (Atlas != nullptr)
		{
			const FTexels& T = bHigher ? HigherIcon : LowerIcon;
			Painter.Image(Square(At, Half), Atlas->Brush, Atlas->Region(T.U0, T.V0, T.U1, T.V1), Colour);
			return;
		}
		const float Tip = bHigher ? -0.6f : 0.6f;
		Painter.Polyline({ At + FVector2f(-0.7f, -Tip) * Half, At + FVector2f(0.0f, Tip) * Half, At + FVector2f(0.7f, -Tip) * Half, At + FVector2f(-0.7f, -Tip) * Half },
			Colour, 1.5f);
	}

	/** The heart sensor's cone: its two edges, the point at the centre and, while it scans, the waves running out. */
	void PaintHeartCone(FScctHudPainter& Painter, const FRadarFrame& Frame, const FScctHudHeartSensor& Sensor, float HeadingDegrees)
	{
		// The sensor finds pawns within 2200 uu in a 60 degree cone (SGadHeartBeatSensor), which on the radar reaches
		// its edge; the content's cone (A_Heart) has its two bars at 60 and 120 degrees, 50 page units long.
		const float Radius = BlipRadius * Frame.Unit;
		const float Left = HeadingDegrees - 30.0f, Right = HeadingDegrees + 30.0f;
		const bool bDetected = Sensor.Cone == 3;
		const FLinearColor Edge(1.0f, 1.0f, 1.0f, 0.75f);
		for (const float Side : { Left, Right })
		{
			const float Radians = FMath::DegreesToRadians(Side);
			const FVector2f Out(FMath::Sin(Radians), -FMath::Cos(Radians));
			// Blanc_opak bars that fade out at both ends.
			Painter.Line(Frame.Centre + Out * Radius * 0.08f, Frame.Centre + Out * Radius * 0.35f, Fade(Edge, 0.5f), 1.5f);
			Painter.Line(Frame.Centre + Out * Radius * 0.35f, Frame.Centre + Out * Radius * 0.8f, Edge, 1.5f);
			Painter.Line(Frame.Centre + Out * Radius * 0.8f, Frame.Centre + Out * Radius, Fade(Edge, 0.35f), 1.5f);
		}
		Painter.Fill(Square(Frame.Centre, 1.5f * Frame.Unit), FLinearColor::White, 1.5f * Frame.Unit);

		// Idle: the clip stands at frame 0 (nothing has left the centre yet).
		if (Sensor.Cone < 2 || Sensor.LoopFrames <= 0)
		{
			return;
		}

		// The waves inside the cone: a band (onde01, blanc02) and two rings (onde2/onde3, retifcule) running out from
		// the centre and fading over the loop; white while scanning, the band orange and the rings red on a detection.
		const FLinearColor Band = bDetected ? Srgb(255, 104, 32) : FLinearColor::White;
		const FLinearColor Ring = bDetected ? Srgb(255, 0, 0) : FLinearColor::White;
		const float Phase = FMath::Clamp(Sensor.Frame / Sensor.LoopFrames, 0.0f, 1.0f);
		Painter.Arc(Frame.Centre, FMath::Max(Radius * Phase, 1.0f), Left, Right, Fade(Band, 0.28f * (1.0f - Phase)), 5.0f * Frame.Unit);
		const float Second = Phase - 2.0f / Sensor.LoopFrames;
		for (const float P : { Phase, Second })
		{
			if (P > 0.0f)
			{
				Painter.Arc(Frame.Centre, Radius * P, Left, Right, Fade(Ring, 0.95f * (1.0f - P)), 1.5f, 0.6f);
			}
		}
	}
}

float ScctHudElements::RadarReservedHeight(const FScctHudModel& Model)
{
	const float Scale = FMath::Clamp(Model.RadarScale, 0.5f, 2.0f);
	return (LocationLine + LocationGap) * Scale + DishRadius * 2.0f * PageScale * Scale + BelowRadar;
}

void ScctHudElements::PaintRadar(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	const FScctHudState& State = Model.State;
	// The spy binocular page replaces the whole player page, the place name with it.
	if (!Model.bHasState || (State.Flags & EScctHudFlags::HideHud) != 0 || State.GadgetView != 0 || State.BinocularMode == 1)
	{
		return;
	}

	const float Scale = FMath::Clamp(Model.RadarScale, 0.5f, 2.0f);
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);

	// The place name, in every mode (deathmatch modes have no objectives panel to carry it), left-aligned over the
	// radar as Reloaded puts it, in the side's colour.
	const FString& Location = Model.Text(State.LocationNameId);
	if (!Location.IsEmpty())
	{
		Painter.Text(FVector2f(Margin, Margin), Location.ToUpper(), Label(9.0f * Scale), Fade(Accent, 0.92f));
	}

	if (!ShowsPlayerHud(Model))
	{
		return;
	}

	FRadarFrame Frame;
	Frame.Unit = PageScale * Scale;
	const float DishHalf = DishRadius * Frame.Unit;
	Frame.Centre = FVector2f(Margin + DishHalf, Margin + (LocationLine + LocationGap) * Scale + DishHalf);
	// th = (-yaw - 16384) * pi / 32768 in the original's rotator units; turned off, the map's +X is to the right.
	const float Theta = Model.bRadarRotate ? -State.Yaw - 90.0f : 0.0f;
	Frame.Cos = FMath::Cos(FMath::DegreesToRadians(Theta));
	Frame.Sin = FMath::Sin(FMath::DegreesToRadians(Theta));
	const float YawRadians = FMath::DegreesToRadians(State.Yaw);
	const FVector2f Forward = Frame.Project(FMath::Cos(YawRadians) * WorldPerPage, FMath::Sin(YawRadians) * WorldPerPage);
	const float Heading = FMath::RadiansToDegrees(FMath::Atan2(Forward.X, -Forward.Y));

	const UScctHudTextures::FEntry* Dish = UScctHudTextures::Find(TEXT("Hud"), TEXT("fond_radar"));
	const UScctHudTextures::FEntry* Atlas = UScctHudTextures::Find(TEXT("Hud"), TEXT("HUDConfig"));
	const UScctHudTextures::FEntry* Dot = UScctHudTextures::Find(TEXT("Hud"), TEXT("ret_center"));
	const UScctHudTextures::FEntry* Ring = UScctHudTextures::Find(TEXT("Hud"), TEXT("zoom_rond01"));

	// Which blips the radar places, as HudRadar.Update picks them: the first two teammates, three enemies and (on the
	// merc's radar, where they include the terminals being hacked) five alarms; the spy's malfunctioning objects are
	// not radar blips.
	struct FPlaced
	{
		int32 Kind;
		FVector2f Page;
		float Dz;
		bool bInRange;
	};
	TArray<FPlaced, TInlineAllocator<10>> Placed;
	int32 Counts[3] = {};
	const int32 Caps[3] = { MaxTeammates, MaxEnemies, State.Side == 1 ? MaxAlarms : 0 };
	for (const FScctHudBlip& Blip : Model.Radar)
	{
		if (Blip.Kind < 0 || Blip.Kind > 2 || Counts[Blip.Kind] >= Caps[Blip.Kind])
		{
			continue;
		}
		++Counts[Blip.Kind];
		const FVector2f Page = Frame.Project(Blip.X - State.X, Blip.Y - State.Y);
		const bool bInRange = Page.Size() <= BlipRadius;
		// Teammates out of range have no arrow and do not show.
		if (bInRange || Blip.Kind != 0)
		{
			Placed.Add({ Blip.Kind, Page, (Blip.Z - State.Z) / WorldPerPage, bInRange });
		}
	}

	// The dish shows only while something is on it (a blip or an arrow) or the heart cone is up.
	const bool bCone = State.Side == 0 && Model.HeartSensor.Cone > 0;
	if (Placed.Num() == 0 && !bCone)
	{
		return;
	}

	const FBox2f DishBox = Square(Frame.Centre, DishHalf);
	Painter.Glass(DishBox, DishHalf, 0.38f);
	if (Dish != nullptr)
	{
		// FondBoussole: the grid turns with the view (its fond widget takes the radar's angle), at 150/255.
		Painter.RotatedImage(DishBox, Dish->Brush, Dish->Region(0, 0, Dish->Size.X, Dish->Size.Y), FLinearColor(1.0f, 1.0f, 1.0f, 0.59f), Theta);
	}
	else
	{
		Painter.Arc(Frame.Centre, DishHalf - 1.0f, 0.0f, 360.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), 1.5f);
		for (int32 Quarter = 0; Quarter < 2; ++Quarter)
		{
			const float Radians = FMath::DegreesToRadians(Theta + Quarter * 90.0f);
			const FVector2f Axis = FVector2f(FMath::Cos(Radians), FMath::Sin(Radians)) * (DishHalf - 4.0f);
			Painter.Line(Frame.Centre - Axis, Frame.Centre + Axis, FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), 1.0f);
		}
	}
	if (Ring != nullptr)
	{
		// The faint outer ring (Element1, 119 units across at 101/255).
		Painter.Image(Square(Frame.Centre, DishHalf * 1.19f), Ring->Brush, Ring->Region(0, 0, Ring->Size.X, Ring->Size.Y), FLinearColor(1.0f, 1.0f, 1.0f, 0.3f));
	}

	if (!Model.bRadarRotate)
	{
		// North up: a mark at the centre shows the way the player faces.
		const float Radians = FMath::DegreesToRadians(Heading);
		const FVector2f Out(FMath::Sin(Radians), -FMath::Cos(Radians));
		const FVector2f Across(-Out.Y, Out.X);
		const float Size = 6.0f * Frame.Unit;
		Painter.Polyline({ Frame.Centre + Out * Size, Frame.Centre - Out * Size * 0.4f + Across * Size * 0.5f,
			Frame.Centre - Out * Size * 0.4f - Across * Size * 0.5f, Frame.Centre + Out * Size }, Fade(Accent, 0.9f), 1.5f);
	}

	if (bCone)
	{
		PaintHeartCone(Painter, Frame, Model.HeartSensor, Heading);
	}

	for (const FPlaced& Blip : Placed)
	{
		const FLinearColor Colour = KindColour(Blip.Kind);
		if (Blip.bInRange)
		{
			// The blip clips loop every 10 frames (14 for a teammate) at 10 a second.
			const float Loop = Blip.Kind == 0 ? 1.4f : 1.0f;
			const float Phase = FMath::Fmod(Model.Clock, Loop) / Loop;
			const FVector2f At = Frame.ToScreen(Blip.Page);
			const bool bLower = Blip.Dz < LowerBelow, bHigher = Blip.Dz > HigherAbove;
			PaintPulse(Painter, Frame, At, Colour, Phase, Dot, Ring, !bLower && !bHigher);
			if (bLower || bHigher)
			{
				PaintHeightIcon(Painter, Frame, At, Colour, bHigher, Atlas);
			}
			continue;
		}

		// An enemy or alarm out of range: an arrow at the edge pointing at it (radar_ArrowRight, white).
		const FVector2f Direction = Blip.Page.GetSafeNormal();
		const FVector2f At = Frame.ToScreen(Direction * BlipRadius);
		const float Degrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		if (Atlas != nullptr)
		{
			Painter.RotatedImage(Square(At, 5.0f * Frame.Unit), Atlas->Brush, Atlas->Region(EdgeArrow.U0, EdgeArrow.V0, EdgeArrow.U1, EdgeArrow.V1),
				FLinearColor(1.0f, 1.0f, 1.0f, 0.95f), Degrees);
		}
		else
		{
			const FVector2f Across(-Direction.Y, Direction.X);
			const float Size = 4.0f * Frame.Unit;
			Painter.Polyline({ At + Direction * Size, At - Direction * Size + Across * Size, At - Direction * Size - Across * Size, At + Direction * Size },
				FLinearColor::White, 1.5f);
		}
	}
}
