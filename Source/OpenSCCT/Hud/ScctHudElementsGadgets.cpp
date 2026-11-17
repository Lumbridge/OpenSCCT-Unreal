#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"

// The gadget views: the sticky cam and mine menus, and the pages that replace the player's view while looking
// through a gadget's camera (the spy's sticky cam, the merc's camera network). As OpenSCCT's Godot client draws them
// (game/Scripts/Hud/Gadgets: GadgetAreaDrivers.cs, GadgetPages.cs) from the same rules (GadgetHudRules), in this HUD's
// own look.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	// The original's line colours (spec/equipment/gadget-hud.md rules 15 and 17): the selected line white, the others
	// (200,200,200), a line that cannot be used now (128,128,128) at half alpha.
	FLinearColor MenuLineColour(int32 Look)
	{
		switch (Look)
		{
		case 1: return Text();
		case 2: return Srgb(128, 128, 128, 0.5f);
		default: return Srgb(200, 200, 200);
		}
	}

	/** Corner brackets just inside the edge of the picture: the camera's frame. */
	void Brackets(FScctHudPainter& Painter, float Inset, float Length, const FLinearColor& Colour)
	{
		const FVector2f Size = Painter.Size();
		const float Left = Inset, Top = Inset, Right = Size.X - Inset, Bottom = Size.Y - Inset;
		Painter.Polyline({ FVector2f(Left, Top + Length), FVector2f(Left, Top), FVector2f(Left + Length, Top) }, Colour, 1.5f);
		Painter.Polyline({ FVector2f(Right - Length, Top), FVector2f(Right, Top), FVector2f(Right, Top + Length) }, Colour, 1.5f);
		Painter.Polyline({ FVector2f(Left, Bottom - Length), FVector2f(Left, Bottom), FVector2f(Left + Length, Bottom) }, Colour, 1.5f);
		Painter.Polyline({ FVector2f(Right - Length, Bottom), FVector2f(Right, Bottom), FVector2f(Right, Bottom - Length) }, Colour, 1.5f);
	}

	/**
	 * The zoom along the right edge: a scale from the camera's widest view (bottom) to its narrowest (top), the mark
	 * where it is now and the magnification beside it.
	 */
	void ZoomScale(FScctHudPainter& Painter, const FScctHudCameraView& Camera, const FLinearColor& Accent)
	{
		if (Camera.MaxFov <= Camera.MinFov || Camera.Fov <= 0.0f)
		{
			return;
		}
		const FVector2f Size = Painter.Size();
		const float X = Size.X - 72.0f, Top = Size.Y * 0.32f, Bottom = Size.Y * 0.68f;
		constexpr int32 Ticks = 10;
		for (int32 Index = 0; Index <= Ticks; ++Index)
		{
			const float Y = FMath::Lerp(Bottom, Top, static_cast<float>(Index) / Ticks);
			const float Length = Index % 5 == 0 ? 12.0f : 6.0f;
			Painter.Line(FVector2f(X - Length, Y), FVector2f(X, Y), Fade(Text(), 0.6f), 1.5f);
		}
		Painter.Line(FVector2f(X, Top), FVector2f(X, Bottom), Fade(Text(), 0.4f), 1.0f);

		const float Zoom = FMath::Clamp((Camera.MaxFov - Camera.Fov) / (Camera.MaxFov - Camera.MinFov), 0.0f, 1.0f);
		const float MarkY = FMath::Lerp(Bottom, Top, Zoom);
		Painter.Polyline({ FVector2f(X + 4.0f, MarkY), FVector2f(X + 12.0f, MarkY - 5.0f), FVector2f(X + 12.0f, MarkY + 5.0f), FVector2f(X + 4.0f, MarkY) }, Accent, 1.5f);
		// The magnification against the camera's widest view, as a lens reads.
		const float Magnification = FMath::Tan(FMath::DegreesToRadians(Camera.MaxFov * 0.5f)) / FMath::Max(FMath::Tan(FMath::DegreesToRadians(Camera.Fov * 0.5f)), 0.001f);
		Painter.Text(FVector2f(X + 18.0f, MarkY - 8.0f), FString::Printf(TEXT("x%.1f"), Magnification), Font(EWeight::Regular, 10.0f), Accent);
	}

	/** One entry of the key help: what it does and the key's name. */
	struct FKeyItem
	{
		FString Key;
		FString Value;
	};

	/** The key help along the bottom: label and key, in a row centred on the picture. */
	void KeyHelp(FScctHudPainter& Painter, const TArray<FKeyItem>& Items, const FLinearColor& Accent)
	{
		const FSlateFontInfo LabelFont = Label(8.0f);
		const FSlateFontInfo KeyFont = Font(EWeight::Regular, 9.5f);
		constexpr float Gap = 34.0f, KeyPad = 8.0f;

		float Width = 0.0f;
		TArray<FVector2f> LabelSizes, KeySizes;
		for (const FKeyItem& Item : Items)
		{
			LabelSizes.Add(Painter.Measure(Item.Key.ToUpper(), LabelFont));
			KeySizes.Add(Item.Value.IsEmpty() ? FVector2f::ZeroVector : Painter.Measure(Item.Value, KeyFont));
			Width += LabelSizes.Last().X + (Item.Value.IsEmpty() ? 0.0f : KeySizes.Last().X + KeyPad * 2.0f + 10.0f);
		}
		Width += Gap * FMath::Max(Items.Num() - 1, 0);

		const FVector2f Size = Painter.Size();
		const float Y = Size.Y - 92.0f;
		Painter.Glass(HudBox(Size.X * 0.5f - Width * 0.5f - 18.0f, Y - 9.0f, Width + 36.0f, 34.0f), 17.0f, 0.5f);
		float X = Size.X * 0.5f - Width * 0.5f;
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			Painter.Text(FVector2f(X, Y + 3.0f), Items[Index].Key.ToUpper(), LabelFont, TextDim());
			X += LabelSizes[Index].X + 10.0f;
			if (!Items[Index].Value.IsEmpty())
			{
				const FBox2f Pill = HudBox(X, Y - 2.0f, KeySizes[Index].X + KeyPad * 2.0f, 20.0f);
				Painter.Fill(Pill, FLinearColor(0.0f, 0.0f, 0.0f, 0.25f), 4.0f, Fade(Accent, 0.6f), 1.0f);
				Painter.Text(FVector2f(X + KeyPad, Y), Items[Index].Value, KeyFont, Text());
				X += Pill.GetSize().X;
			}
			X += Gap;
		}
	}

	/**
	 * The spy's sticky cam (P_StickyCam): the lens's reticle, the sweep and interference the original's page plays
	 * over the picture, the zoom, the gas left in the cam and the key help.
	 */
	void PaintStickyCam(FScctHudPainter& Painter, const FScctHudModel& Model, const FLinearColor& Accent)
	{
		const FVector2f Size = Painter.Size();
		const FVector2f Centre = Size * 0.5f;
		const FScctHudCameraView& Camera = Model.CameraView;

		// The sweep: a soft band that runs down the picture every four seconds.
		const float SweepY = FMath::Fmod(Model.Clock, 4.0f) / 4.0f * Size.Y;
		Painter.Gradient(HudBox(0.0f, SweepY - 60.0f, Size.X, 60.0f), Fade(Accent, 0.0f), Fade(Accent, 0.06f), true);
		Painter.Line(FVector2f(0.0f, SweepY), FVector2f(Size.X, SweepY), Fade(Accent, 0.12f), 1.0f);
		// Interference: thin bars that flicker in at times, as the cheap lens's noise.
		for (int32 Bar = 0; Bar < 3; ++Bar)
		{
			const float Phase = Model.Clock * (1.7f + Bar * 0.9f) + Bar * 2.1f;
			const float Strength = FMath::Max(FMath::Sin(Phase * 3.1f) * FMath::Sin(Phase * 1.3f), 0.0f);
			if (Strength > 0.4f)
			{
				const float Y = FMath::Frac(Phase * 0.37f) * Size.Y;
				Painter.Fill(HudBox(0.0f, Y, Size.X, 2.0f + Bar), Fade(Text(), 0.05f * Strength));
			}
		}

		Brackets(Painter, 36.0f, 44.0f, Fade(Accent, 0.8f));

		// The lens's reticle: a ring with four ticks, open in the middle.
		Painter.Arc(Centre, 26.0f, 0.0f, 360.0f, Fade(Accent, 0.5f), 1.0f);
		const FVector2f Directions[] = { FVector2f(1.0f, 0.0f), FVector2f(-1.0f, 0.0f), FVector2f(0.0f, 1.0f), FVector2f(0.0f, -1.0f) };
		for (const FVector2f& Direction : Directions)
		{
			Painter.Line(Centre + Direction * 12.0f, Centre + Direction * 40.0f, Fade(Accent, 0.7f), 1.0f);
		}

		ZoomScale(Painter, Camera, Accent);

		// The gas the cam carries: one pip a dose, dim while the last release still blocks the next.
		if (Camera.GasDoses >= 0)
		{
			const float X = 72.0f, Y = Size.Y * 0.5f - 20.0f;
			const bool bReady = Camera.GasDoses > 0 && Camera.GasReadySeconds <= 0.0f;
			Painter.Text(FVector2f(X, Y), Model.Label(EScctHudLabel::StickyCamGas).ToUpper(), Label(8.0f), bReady ? Accent : TextFaint());
			for (int32 Dose = 0; Dose < FMath::Max(Camera.GasDoses, 1); ++Dose)
			{
				const FBox2f Pip = HudBox(X + Dose * 16.0f, Y + 20.0f, 10.0f, 10.0f);
				Painter.Fill(Pip, Dose < Camera.GasDoses ? (bReady ? Accent : Fade(Accent, 0.35f)) : FLinearColor::Transparent, 5.0f, Fade(Accent, 0.6f), 1.0f);
			}
			if (Camera.GasDoses > 0 && Camera.GasReadySeconds > 0.0f)
			{
				Painter.Text(FVector2f(X + Camera.GasDoses * 16.0f + 6.0f, Y + 17.0f), FString::FromInt(FMath::CeilToInt32(Camera.GasReadySeconds)), Font(EWeight::Regular, 10.0f), TextDim());
			}
		}

		KeyHelp(Painter, {
			{ Model.Label(EScctHudLabel::StickyCamZoom), Model.GadgetKeys.Zoom },
			{ Model.Label(EScctHudLabel::StickyCamGas), Model.GadgetKeys.Use },
			{ Model.Label(EScctHudLabel::StickyCamExit), Model.GadgetKeys.Exit },
		}, Accent);
	}

	/**
	 * The merc's camera network (P_CamNetWork): the frame, the camera's name, the ALERT box while a spy hacks an
	 * objective, the zoom, the key help, and the blank feed of a camera that was shot until it reconnects.
	 */
	void PaintCamNetwork(FScctHudPainter& Painter, const FScctHudModel& Model, const FLinearColor& Accent)
	{
		const FScctHudState& State = Model.State;
		const FVector2f Size = Painter.Size();
		const FBox2f Whole(FVector2f::ZeroVector, Size);

		if (State.CamNetworkDisabledSeconds > 0)
		{
			Painter.Fill(Whole, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
			Painter.Text(FVector2f(Size.X * 0.5f, Size.Y * 0.5f - 24.0f), FString::FromInt(State.CamNetworkDisabledSeconds), Font(EWeight::Light, 36.0f), Danger(), EAlign::Centre);
		}

		Brackets(Painter, 36.0f, 44.0f, Fade(Accent, 0.8f));
		// Under the round's panel at the top centre, which stays up in the view.
		Painter.Text(FVector2f(Size.X * 0.5f, 122.0f), Model.Text(State.CamNetworkNameId).ToUpper(), Label(10.0f), Accent, EAlign::Centre);
		ZoomScale(Painter, Model.CameraView, Accent);

		if (Model.CamNetworkAlert())
		{
			// The original's Alert box blinks; here it pulses, in the danger colour, under the camera's name.
			FString Alert = Model.Label(EScctHudLabel::CamNetworkAlert).ToUpper();
			if (Alert.IsEmpty())
			{
				Alert = TEXT("ALERT");
			}
			const FSlateFontInfo AlertFont = Font(EWeight::Bold, 16.0f, 300);
			const float Pulse = 0.55f + 0.45f * FMath::Abs(FMath::Sin(Model.Clock * 4.0f));
			const float Width = Painter.Measure(Alert, AlertFont).X + 56.0f;
			const FBox2f Box = HudBox(Size.X * 0.5f - Width * 0.5f, 146.0f, Width, 40.0f);
			Painter.Fill(Box, Fade(Danger(), 0.18f * Pulse), 4.0f, Fade(Danger(), Pulse), 1.5f);
			Painter.Text(FVector2f(Size.X * 0.5f, 154.0f), Alert, AlertFont, Fade(Danger(), Pulse), EAlign::Centre);
		}

		KeyHelp(Painter, {
			{ Model.Label(EScctHudLabel::CamNetworkZoom), Model.GadgetKeys.Zoom },
			{ Model.Label(EScctHudLabel::CamNetworkNext), Model.GadgetKeys.Cameras },
			{ Model.Label(EScctHudLabel::CamNetworkExit), Model.GadgetKeys.Exit },
		}, Accent);
	}
}

void ScctHudElements::PaintCameraPages(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!Model.bHasState || Model.State.GadgetView == 0 || (Model.State.Flags & EScctHudFlags::HideHud) != 0)
	{
		return;
	}
	const FLinearColor Accent = ScctHudStyle::Accent(Model.State.Side);
	if (Model.State.GadgetView == 1)
	{
		PaintStickyCam(Painter, Model, Accent);
	}
	else
	{
		PaintCamNetwork(Painter, Model, Accent);
	}
}

void ScctHudElements::PaintGadgetMenu(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!ShowsPlayerHud(Model) || Model.MenuLines.Num() == 0)
	{
		return;
	}

	const FScctHudState& State = Model.State;
	const FVector2f Size = Painter.Size();
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);

	// Where the original has the area on its 640 x 480 page: the sticky cam menu on the right a third of the way
	// down (StickyCamInteraction at 480,150), the mine menu on the right above the gadgets (MineInteraction at 477,366).
	constexpr float Width = 232.0f, RowHeight = 26.0f, Inset = 8.0f, TitleHeight = 24.0f;
	const float Height = TitleHeight + Model.MenuLines.Num() * RowHeight + Inset * 2.0f;
	const float X = Size.X - Margin - Width;
	const float Y = State.Side == 0 ? Size.Y * 0.31f : FMath::Min(Size.Y * 0.76f, Size.Y - Margin - 300.0f) - Height;
	Painter.Glass(HudBox(X, Y, Width, Height), Corner, 0.78f);
	Painter.GlowLine(FVector2f(X + Width - 1.0f, Y + 12.0f), FVector2f(X + Width - 1.0f, Y + Height - 12.0f), Accent, 2.0f);

	// The gadget whose menu it is, as the title.
	const FString Title = Model.Gadgets.IsValidIndex(State.SelectedGadget) ? Model.Text(Model.Gadgets[State.SelectedGadget].NameId).ToUpper() : FString();
	Painter.Text(FVector2f(X + 18.0f, Y + Inset + 4.0f), Title, Label(8.0f), Accent);

	float RowY = Y + Inset + TitleHeight;
	for (const FScctHudMenuLine& Line : Model.MenuLines)
	{
		const bool bSelected = Line.Look == 1;
		if (bSelected)
		{
			Painter.Fill(HudBox(X + 6.0f, RowY, Width - 12.0f, RowHeight), Fade(Accent, 0.14f), 4.0f);
			// The arrow beside the selected line.
			const FVector2f Tip(X + 24.0f, RowY + RowHeight * 0.5f);
			Painter.Polyline({ Tip + FVector2f(-8.0f, -6.0f), Tip, Tip + FVector2f(-8.0f, 6.0f), Tip + FVector2f(-8.0f, -6.0f) }, Accent, 2.0f);
		}
		Painter.Text(FVector2f(X + 34.0f, RowY + 6.0f), Model.Text(Line.TextId), Font(bSelected ? EWeight::Bold : EWeight::Regular, 11.0f), MenuLineColour(Line.Look));
		RowY += RowHeight;
	}
}
