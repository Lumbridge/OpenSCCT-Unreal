#include "Hud/ScctHudElements.h"

#include "Hud/ScctCrosshairShape.h"
#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudTextures.h"
#include "OpenSCCT.h"
#include "Hud/ScctPoisonOverlay.h"
#include "Hud/ScctVisorEffects.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctViewPawn.h"

// The blocks about the player: status, equipment, prompt, crosshair, scope and the washes over the picture.

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	// Hud.mgm's A_Reticule. Positions are relative to its centre dot (61,65) on the 480-high canvas.
	void PaintSoundDirection(FScctHudPainter& Painter, FVector2f Centre, float Scale, int32 Direction, float Age)
	{
		static const FLinearColor Amber = FLinearColor::FromSRGBColor(FColor(255, 182, 68));
		// A one-pixel shadow separates the amber artwork from similarly coloured lights and walls.
		const FVector2f ShadowOffset(Scale, Scale);
		if (Direction == 2 || Direction == 3)
		{
			const bool bAbove = Direction == 2;
			const FVector2f TopLeft = Centre + FVector2f(-9, bAbove ? -58 : 41) * Scale;
			const FBox2f Box(TopLeft, TopLeft + FVector2f(17, 17) * Scale);
			if (const auto* Atlas = UScctHudTextures::Find(TEXT("Hud"), TEXT("Interface")))
			{
				const FBox2f Region = bAbove ? Atlas->Region(81, 22, 97, 38) : Atlas->Region(99, 18, 115, 34);
				Painter.Image(FBox2f(Box.Min + ShadowOffset, Box.Max + ShadowOffset), Atlas->Brush, Region,
					FLinearColor::Black.CopyWithNewOpacity(0.55f));
				Painter.Image(Box, Atlas->Brush, Region, Amber.CopyWithNewOpacity(150.0f / 255.0f));
			}
			else
			{
				const FVector2f Tip(Box.GetCenter().X, bAbove ? Box.Min.Y : Box.Max.Y);
				const float Base = bAbove ? Box.Max.Y : Box.Min.Y;
				Painter.Polyline({FVector2f(Box.Min.X, Base), Tip, FVector2f(Box.Max.X, Base)}, Amber, 2.0f * Scale);
			}
			return;
		}

		struct FWave
		{
			float Rotation;
			FVector2f TopLeft[4];
		};
		static const FWave Waves[] = {
			{-135, {{-27,-12}, {-34,-15}, {-43,-20}, {-52,-25}}}, // left
			{  45, {{  2,-13}, {  2,-16}, {  1,-21}, {  2,-26}}}, // right
			{ -45, {{-13,-28}, {-16,-34}, {-21,-43}, {-26,-52}}}, // ahead
			{ 135, {{-13,  3}, {-16,  4}, {-21,  2}, {-26,  1}}}  // behind
		};
		const FWave& Wave = Waves[Direction > 1 ? Direction - 2 : Direction];
		const auto* Ring = UScctHudTextures::Find(TEXT("Hud"), TEXT("ret_cercle"));
		constexpr float Sides[] = {25, 31, 41, 51};
		for (int32 Band = 0; Band < UE_ARRAY_COUNT(Sides); ++Band)
		{
			// Four waves peak three animation frames apart, repeating at frame 12 (10 fps).
			const float Phase = FMath::Fmod(Age * 10.0f + 12.0f - Band * 3.0f, 12.0f);
			const float Brightness = Phase < 9.0f ? 1.0f - Phase / 9.0f : (Phase - 9.0f) / 3.0f;
			const FLinearColor Tint = Amber.CopyWithNewOpacity(FMath::Lerp(50.0f, 255.0f, Brightness) * (128.0f / (255.0f * 255.0f)));
			if (Ring)
			{
				const FVector2f TopLeft = Centre + Wave.TopLeft[Band] * Scale;
				const FBox2f Box(TopLeft, TopLeft + FVector2f(Sides[Band], Sides[Band]) * Scale);
				const FBox2f Region(FVector2f(0.4f, 0), FVector2f(1, 0.6f));
				Painter.RotatedImage(FBox2f(Box.Min + ShadowOffset, Box.Max + ShadowOffset), Ring->Brush, Region,
					FLinearColor::Black.CopyWithNewOpacity(Tint.A), Wave.Rotation);
				Painter.RotatedImage(Box, Ring->Brush, Region, Tint, Wave.Rotation);
			}
			else
			{
				const float Bearing = Wave.Rotation + 45.0f;
				Painter.Arc(Centre, Sides[Band] * Scale, Bearing - 34.0f, Bearing + 34.0f, Tint, 2.0f * Scale);
			}
		}
	}

	constexpr float PanelWidth = 252.0f;
	constexpr float PanelHeight = 72.0f;

	void PaintHitMarker(FScctHudPainter& Painter, FVector2f Centre, float Strength, float Gap)
	{
		if (Strength <= 0.01f) return;
		const float Out = Gap + 4.0f + (1.0f - Strength) * 5.0f;
		const FVector2f Diagonals[] = { FVector2f(1.0f, 1.0f), FVector2f(-1.0f, 1.0f), FVector2f(1.0f, -1.0f), FVector2f(-1.0f, -1.0f) };
		for (const FVector2f& Diagonal : Diagonals)
		{
			const FVector2f Unit = Diagonal * UE_INV_SQRT_2;
			Painter.Line(Centre + Unit * Out, Centre + Unit * (Out + 7.0f), Fade(Danger(), Strength), 2.0f);
		}
	}

	/** A round gauge: a faint three-quarter track, the lit share of it and a number in the middle. */
	void Gauge(FScctHudPainter& Painter, const FVector2f& Centre, float Fraction, const FLinearColor& Colour, const FString& Value)
	{
		constexpr float Radius = 20.0f;
		constexpr float From = -135.0f, To = 135.0f;
		Painter.Blur(FBox2f(Centre - FVector2f(Radius + 8.0f), Centre + FVector2f(Radius + 8.0f)), 10.0f, Radius + 8.0f);
		Painter.Fill(FBox2f(Centre - FVector2f(Radius + 8.0f), Centre + FVector2f(Radius + 8.0f)), FLinearColor(0.004f, 0.007f, 0.011f, 0.36f), Radius + 8.0f);
		Painter.Arc(Centre, Radius, From, To, Fade(Text(), 0.16f), 2.0f);
		Painter.Arc(Centre, Radius, From, From + (To - From) * FMath::Clamp(Fraction, 0.0f, 1.0f), Colour, 2.0f, 1.0f);
		Painter.Text(Centre - FVector2f(0.0f, 8.0f), Value, Font(EWeight::Regular, 9.5f), Text(), EAlign::Centre);
	}

	/** Corner brackets just inside the edge of the picture: the frame of a vision mode or a gadget's camera. */
	void PlayerBrackets(FScctHudPainter& Painter, float Inset, float Length, const FLinearColor& Colour)
	{
		const FVector2f Size = Painter.Size();
		const float Left = Inset, Top = Inset, Right = Size.X - Inset, Bottom = Size.Y - Inset;
		Painter.Polyline({ FVector2f(Left, Top + Length), FVector2f(Left, Top), FVector2f(Left + Length, Top) }, Colour, 1.5f);
		Painter.Polyline({ FVector2f(Right - Length, Top), FVector2f(Right, Top), FVector2f(Right, Top + Length) }, Colour, 1.5f);
		Painter.Polyline({ FVector2f(Left, Bottom - Length), FVector2f(Left, Bottom), FVector2f(Left + Length, Bottom) }, Colour, 1.5f);
		Painter.Polyline({ FVector2f(Right - Length, Bottom), FVector2f(Right, Bottom), FVector2f(Right, Bottom - Length) }, Colour, 1.5f);
	}

	/** Says in the log what the merc's reticle is each time that changes (not each frame). */
	void LogPlayerCrosshair(const ScctCrosshair::FSettings& Crosshair, int32 Pieces, const FVector2f& At, const FVector2f& Size)
	{
		static FString LastPlayerCrosshairLine;
		const FString Line = FString::Printf(TEXT("%s; %d pieces on a %.0f x %.0f HUD%s"), *ScctCrosshair::Describe(Crosshair), Pieces, Size.X, Size.Y,
			Crosshair.bCustom && Crosshair.bFollowRecoil ? TEXT(" (on the aim point)") : *FString::Printf(TEXT(" at %.0f,%.0f"), At.X, At.Y));
		if (Line != LastPlayerCrosshairLine)
		{
			LastPlayerCrosshairLine = Line;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctCrosshair: %s"), *Line);
		}
	}

	/** A red glow in from the edges of the picture. */
	void EdgeWash(FScctHudPainter& Painter, const FLinearColor& Colour, float Alpha)
	{
		if (Alpha <= 0.005f)
		{
			return;
		}
		const FVector2f Size = Painter.Size();
		const FLinearColor Lit = Fade(Colour, Alpha);
		const FLinearColor Clear = Fade(Colour, 0.0f);
		const float SideWidth = Size.X * 0.16f, EdgeHeight = Size.Y * 0.20f;
		Painter.Gradient(HudBox(0.0f, 0.0f, SideWidth, Size.Y), Lit, Clear, false);
		Painter.Gradient(HudBox(Size.X - SideWidth, 0.0f, SideWidth, Size.Y), Clear, Lit, false);
		Painter.Gradient(HudBox(0.0f, 0.0f, Size.X, EdgeHeight), Lit, Clear, true);
		Painter.Gradient(HudBox(0.0f, Size.Y - EdgeHeight, Size.X, EdgeHeight), Clear, Lit, true);
	}
}

bool ScctHudElements::ShowsPlayerHud(const FScctHudModel& Model)
{
	return Model.bHasState
		&& (Model.State.Flags & EScctHudFlags::HasPawn) != 0
		&& (Model.State.Flags & (EScctHudFlags::HideHud | EScctHudFlags::Spectating | EScctHudFlags::GameEnded)) == 0
		&& !ScctVisorEffects::IsHudShutDown(Model) // an EMP keeps the merc's HUD down for 12 s
		&& Model.State.GadgetView == 0
		// A binocular page replaces the player's HUD, as P_BinocularsATT replaces P_HudATT and P_BinocularsDEF (the
		// merc's rifle scope) replaces P_HudDEF in the original.
		&& Model.State.BinocularMode == 0;
}

FString ScctHudElements::LivesText(const FScctHudModel& Model, int32 Lives, int32 InfiniteFrom)
{
	if (Lives >= InfiniteFrom)
	{
		const FString& Infinite = Model.Label(EScctHudLabel::Infinite);
		return Infinite.IsEmpty() ? FString(TEXT("∞")) : Infinite;
	}
	return FString::FromInt(FMath::Max(Lives, 0));
}

FString ScctHudElements::ClockText(int32 Seconds)
{
	Seconds = FMath::Max(Seconds, 0);
	return FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
}

void ScctHudElements::PaintScreenEffects(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!Model.bHasState)
	{
		return;
	}

	const FScctHudState& State = Model.State;
	const FVector2f Size = Painter.Size();
	const FBox2f Whole(FVector2f::ZeroVector, Size);

	// The flash-bang after-image: the picture as it was at the flash, burnt in over the live one and fading
	// with RemanenceBlendFactor. It belongs to the picture, so it goes under every wash.
	if (Model.AfterImage != nullptr && Model.AfterImageAlpha > 0.0f)
	{
		Painter.Image(Whole, *Model.AfterImage, FLinearColor(1.0f, 1.0f, 1.0f, Model.AfterImageAlpha));
	}

	// The simulation's own washes: the value is an alpha of 0 to 255.
	for (int32 Slot = 0; Slot < ScctHudSfxSlots; ++Slot)
	{
		const float Alpha = FMath::Clamp(State.SfxValue[Slot] / 255.0f, 0.0f, 1.0f);
		switch (State.SfxType[Slot])
		{
		case EScctHudSfx::Black:
		case EScctHudSfx::Sleep:
			Painter.Fill(Whole, FLinearColor(0.0f, 0.0f, 0.0f, Alpha));
			break;
		case EScctHudSfx::White:
			Painter.Fill(Whole, FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
			break;
		case EScctHudSfx::Red:
			EdgeWash(Painter, Danger(), Alpha * 0.6f);
			break;
		case EScctHudSfx::Flash:
			// The slot drives the after-image above; its white-out is painted over the whole HUD
			// (PaintFlashWhiteOut), as the merc page's Flash covers the page.
			break;
		case EScctHudSfx::CrossEye:
		case EScctHudSfx::Smoke:
			// Gas and smoke paint nothing on the HUD: the original's ProcessSFX only sets CrossEyedFactor from them,
			// and the picture itself is doubled (UScctScreenEffectsSubsystem's double vision).
			break;
		default:
			break;
		}
	}

	// The poison's wash over the view, either side's (the brightening half of it is in the picture already).
	ScctPoisonOverlay::Paint(Painter, Model);

	// The merc's visor: EMP shutdown, perturbation, foam, droplets, the sticky shocker's arcs.
	ScctVisorEffects::Paint(Painter, Model);

	if ((State.EffectFlags & EScctHudEffectFlags::SleepScreen) != 0 && State.SleepingTextId != 0)
	{
		Painter.Text(FVector2f(Size.X * 0.5f, Size.Y * 0.44f), Model.Text(State.SleepingTextId), Font(EWeight::Light, 22.0f, 120), Text(), EAlign::Centre);
		Painter.Text(FVector2f(Size.X * 0.5f, Size.Y * 0.44f + 40.0f), FString::FromInt(FMath::Max(State.SleepSeconds, 0)), Font(EWeight::Light, 30.0f), TextDim(), EAlign::Centre);
	}

	// A hit, and the slow pulse of being nearly dead.
	const bool bAlive = (State.Flags & EScctHudFlags::HasPawn) != 0;
	float Wash = Model.DamagePulse * 0.34f;
	if (Model.HealthLoss > 0.0f)
	{
		Wash = FMath::Max(Wash, Model.HealthLoss * 0.25f);
	}
	if (bAlive && State.Health > 0 && State.Health <= 25)
	{
		Wash = FMath::Max(Wash, 0.10f + 0.06f * FMath::Sin(Model.Clock * 3.2f));
	}
	else if (bAlive && State.Health <= 0)
	{
		// Down: the pawn is still the player's until the simulation takes it away, and the red stays with it.
		Wash = FMath::Max(Wash, 0.30f);
	}
	EdgeWash(Painter, Danger(), Wash);

	// Looking through goggles: a thin frame in the side's colour. A gadget's camera has its own page
	// (PaintCameraPages).
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	if (Model.VisionShown > 0.01f)
	{
		PlayerBrackets(Painter, 16.0f, 22.0f, Fade(Accent, 0.55f * Model.VisionShown));
	}
}

void ScctHudElements::PaintCrosshair(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!ShowsPlayerHud(Model) || Model.State.GadgetView != 0)
	{
		return;
	}

	const FScctHudState& State = Model.State;
	const FVector2f Size = Painter.Size();
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);

	// The merc's aim point is given on the original's 640 x 480 canvas, stretched over the picture.
	const FVector2f FixedCentre = Size * 0.5f;
	FVector2f Centre = FixedCentre;
	if ((State.Flags & EScctHudFlags::SpotValid) != 0)
	{
		Centre = FVector2f(State.SpotX / 640.0f * Size.X, State.SpotY / 480.0f * Size.Y);
	}
	// Throw charge and target messages still follow the weapon through the body camera lens.
	const AScctViewPawn* View = Model.MercView.Get();
	const bool bMercCamera = View && View->UsesMercCamera();
	if (bMercCamera)
	{
		Centre = View->ProjectMercAim(Centre, Size);
	}

	const bool bScoped = State.BinocularMode != 0;
	const AScctPlayerController* Player = View ? Cast<AScctPlayerController>(View->GetController()) : nullptr;
	const bool bAiming = Player && Player->GetLookView().RifleAimBlend > 0.0f;
	const bool bUsingScopeGlass = bAiming && Player->GetLookView().ScopeLensPower > 1.0f;
	const bool bMercReticle = (State.Flags & EScctHudFlags::ShowReticle) != 0 && !bScoped && State.Side == 1 && !bMercCamera && !bUsingScopeGlass;
	const ScctCrosshair::FSettings Crosshair = bMercReticle ? ScctCrosshair::Read() : ScctCrosshair::FSettings();
	// Reloaded's classic_crosshair: the merc reticle 3 units left and 3 down on the original's 480-high canvas.
	const FVector2f ReticleCentre = Centre + (bMercReticle && Crosshair.bClassicPosition ? FVector2f(-3.0f, 3.0f) * (Size.Y / 480.0f) : FVector2f::ZeroVector);
	if (bMercReticle && Crosshair.bCustom)
	{
		// The custom crosshair (Hud/ScctCrosshairShape.h): at the screen's centre, or on the aim point with follow
		// recoil; a dynamic style opens as far as the original ticks would, and it is red after a hit like them.
		const FVector2f FixedSpot = FixedCentre + (ReticleCentre - Centre);
		const FVector2f At = Crosshair.bFollowRecoil ? ReticleCentre : FixedSpot;
		const TArray<ScctCrosshair::FPiece> Pieces = ScctCrosshair::Build(Crosshair, At, Size.Y, Model.Spread * 24.0f, (State.Flags & EScctHudFlags::ReticleHit) != 0);
		ScctCrosshair::Paint(Painter, Pieces);
		LogPlayerCrosshair(Crosshair, Pieces.Num(), At, Size);
	}
	else if (bMercReticle)
	{
		// The merc's page only, as the arrows below: a spy never gets the ticks that open with a weapon's spread.
		LogPlayerCrosshair(Crosshair, 0, ReticleCentre, Size);
		const FVector2f TickCentre = ReticleCentre;
		// Four ticks that open with the weapon's spread; they turn red for a moment when a shot lands.
		const float Gap = 5.0f + Model.Spread * 24.0f;
		constexpr float Length = 8.0f;
		const FLinearColor Colour = FMath::Lerp(Text(), Danger(), Model.HitConfirm);
		const FVector2f Directions[] = { FVector2f(1.0f, 0.0f), FVector2f(-1.0f, 0.0f), FVector2f(0.0f, 1.0f), FVector2f(0.0f, -1.0f) };
		for (const FVector2f& Direction : Directions)
		{
			// A dark edge under each tick keeps it readable over a bright floor.
			Painter.Line(TickCentre + Direction * (Gap - 1.0f), TickCentre + Direction * (Gap + Length + 1.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), 4.0f);
			Painter.Line(TickCentre + Direction * Gap, TickCentre + Direction * (Gap + Length), Colour, 2.0f);
		}
		Painter.Fill(FBox2f(TickCentre - FVector2f(2.0f), TickCentre + FVector2f(2.0f)), FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), 2.0f);
		Painter.Fill(FBox2f(TickCentre - FVector2f(1.0f), TickCentre + FVector2f(1.0f)), Colour, 1.0f);

	}
	if (State.Side == 1 && Model.HitConfirm > 0.01f)
	{
		if (bAiming)
		{
			PaintHitMarker(Painter, Centre, Model.HitConfirm, 5.0f);
		}
		else if (bMercReticle && !Crosshair.bCustom)
		{
			PaintHitMarker(Painter, ReticleCentre, Model.HitConfirm, 5.0f + Model.Spread * 24.0f);
		}
	}

	// The throw charge: an arc under the centre that fills as the throw winds up.
	if (State.Ballistic > 0.001f)
	{
		Painter.Arc(Centre, 34.0f, 240.0f, 120.0f, Fade(Text(), 0.16f), 2.0f);
		Painter.Arc(Centre, 34.0f, 240.0f, 240.0f - 120.0f * FMath::Clamp(State.Ballistic, 0.0f, 1.0f), Accent, 2.0f, 1.0f);
	}

	// Who the laser sight holds, once the original would have typed the name; left out while the motion tracking writes
	// the name over the target's bracket itself (ScctHudElementsMotion.cpp).
	float TextY = Centre.Y + 40.0f;
	if (State.TargetActorId != 0 && State.TargetNameId != 0 && State.TimeSeconds >= State.TargetLockTime + State.TargetNameDelay
		&& Model.Motion.NameRow == 0)
	{
		const bool bFriend = (State.Flags & EScctHudFlags::TargetFriend) != 0;
		Painter.Text(FVector2f(Centre.X, TextY), Model.Text(State.TargetNameId), Font(EWeight::Regular, 11.0f), bFriend ? Accent : Danger(), EAlign::Centre);
		TextY += 20.0f;
	}
	if (State.ReticleMessageId != 0)
	{
		Painter.Text(FVector2f(Centre.X, TextY), Model.Text(State.ReticleMessageId), Font(EWeight::Regular, 11.0f), Text(), EAlign::Centre);
	}

	// Bearings are relative to the listener. Free aim may move the barrel away from his view centre.
	for (int32 Index = 0; State.Side == 1 && Index < ScctHudArrowCount; ++Index)
	{
		if (State.ArrowSeconds[Index] > 0.0f)
		{
			PaintSoundDirection(Painter, bMercCamera ? FixedCentre : ReticleCentre, Size.Y / 480.0f, Index, Model.SoundArrowAge[Index]);
		}
	}

	// A hit on the player. The simulation reports that a hit landed and its kind, not where it came from, so
	// the arcs close in from both sides instead of pointing.
	if (Model.DamagePulse > 0.01f)
	{
		const float Radius = 64.0f + (1.0f - Model.DamagePulse) * 10.0f;
		const FLinearColor Colour = Fade(Danger(), Model.DamagePulse);
		Painter.Arc(Centre, Radius, 55.0f, 125.0f, Colour, 3.0f, 1.0f);
		Painter.Arc(Centre, Radius, 235.0f, 305.0f, Colour, 3.0f, 1.0f);
	}
}

void ScctHudElements::PaintScope(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	// The merc's scope only: the spy's binoculars are their own page (PaintSpyBinoculars). The scope replaces the
	// player's HUD, so its gates are the HUD's own without the binocular one.
	const FScctHudState& State = Model.State;
	if (!Model.bHasState || State.BinocularMode != 2 || State.GadgetView != 0 || (State.Flags & EScctHudFlags::HasPawn) == 0
		|| (State.Flags & (EScctHudFlags::HideHud | EScctHudFlags::Spectating | EScctHudFlags::GameEnded)) != 0
		|| ScctVisorEffects::IsHudShutDown(Model))
	{
		return;
	}

	const FVector2f Size = Painter.Size();
	const FVector2f Centre = Size * 0.5f;
	// The content's P_BinocularsDEF page (ScctHudElementsBinoculars.cpp); the drawing below stands in for it only
	// when the content or the bridge has no such page.
	if (Model.ScopeLayers.Num() > 0)
	{
		PaintScopePage(Painter, Model);
		PaintHitMarker(Painter, Centre + Model.ScopeSightOffset * (Size.Y / 480.0f), Model.HitConfirm, 5.0f);
		return;
	}

	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	const bool bHit = (State.Flags & EScctHudFlags::BinocularHit) != 0;
	const FLinearColor Sight = bHit ? Danger() : Fade(Text(), 0.8f);

	const float Radius = Size.Y * 0.30f;
	Painter.Arc(Centre, Radius, 0.0f, 360.0f, Fade(Text(), 0.28f), 1.0f);
	const FVector2f Directions[] = { FVector2f(1.0f, 0.0f), FVector2f(-1.0f, 0.0f), FVector2f(0.0f, 1.0f), FVector2f(0.0f, -1.0f) };
	for (const FVector2f& Direction : Directions)
	{
		Painter.Line(Centre + Direction * 14.0f, Centre + Direction * (Radius * 0.42f), Sight, 1.0f);
		Painter.Line(Centre + Direction * (Radius - 12.0f), Centre + Direction * (Radius + 12.0f), Fade(Text(), 0.5f), 1.5f);
	}
	Painter.Fill(FBox2f(Centre - FVector2f(1.0f), Centre + FVector2f(1.0f)), Sight, 1.0f);
	PaintHitMarker(Painter, Centre, Model.HitConfirm, 5.0f);

	// The zoom step: the field of view goes 30, 20, 10 degrees.
	const int32 Step = State.BinocularFov <= 12.0f ? 3 : (State.BinocularFov <= 22.0f ? 2 : 1);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVector2f Pip(Centre.X + Radius + 26.0f, Centre.Y + 12.0f - Index * 12.0f);
		Painter.Fill(FBox2f(Pip - FVector2f(5.0f, 2.0f), Pip + FVector2f(5.0f, 2.0f)), Index < Step ? Accent : Fade(Text(), 0.2f), 1.0f);
	}

	if ((State.Flags & EScctHudFlags::BinocularAim) != 0 && State.BinocularDistance > 0.0f)
	{
		const FString Range = FString::Printf(TEXT("%.0f %s"), State.BinocularDistance * State.DistanceScale, *Model.Text(State.DistanceUnitId));
		Painter.Text(FVector2f(Centre.X, Centre.Y + Radius + 20.0f), Range, Font(EWeight::Regular, 12.0f), Text(), EAlign::Centre);
	}
	if ((State.Flags & EScctHudFlags::BinocularHackPrompt) != 0)
	{
		Painter.Text(FVector2f(Centre.X, Centre.Y + Radius + 44.0f), Model.Text(State.HackPromptId), Font(EWeight::Regular, 12.0f), Accent, EAlign::Centre);
	}
}

void ScctHudElements::PaintStatus(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!ShowsPlayerHud(Model))
	{
		return;
	}

	const FScctHudState& State = Model.State;
	const FVector2f Size = Painter.Size();
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	const float X = Margin, Y = Size.Y - Margin - PanelHeight;

	if ((State.Flags & EScctHudFlags::LifeOff) == 0)
	{
		Painter.Glass(HudBox(X, Y, PanelWidth, PanelHeight), Corner);
		Painter.GlowLine(FVector2f(X + 1.0f, Y + 14.0f), FVector2f(X + 1.0f, Y + PanelHeight - 14.0f), Accent, 2.0f);

		// The number is the loudest thing in the block; it reddens when low and flashes on a loss.
		const bool bLow = State.Health <= 30;
		const FLinearColor NumberColour = FMath::Lerp(bLow ? Danger() : Text(), Danger(), Model.HealthLoss);
		Painter.Text(FVector2f(X + 20.0f, Y + 12.0f), FString::FromInt(FMath::Max(State.Health, 0)), Font(EWeight::Light, 30.0f), NumberColour);

		// Lives: label and number on one line, right of the number.
		const float Column = X + 96.0f, ColumnRight = X + PanelWidth - 18.0f;
		Painter.Text(FVector2f(Column, Y + 17.0f), Model.Label(EScctHudLabel::Lives).ToUpper(), Label(), TextDim());
		Painter.Text(FVector2f(ColumnRight, Y + 13.0f), ScctHudElements::LivesText(Model, State.Lives, 100), Font(EWeight::Regular, 12.0f), Text(), EAlign::Right);

		// The bar: ten segments, the eased value lit.
		const float BarY = Y + 42.0f, BarWidth = ColumnRight - Column;
		constexpr int32 Segments = 10;
		const float SegmentWidth = (BarWidth - (Segments - 1) * 2.0f) / Segments;
		const float Filled = FMath::Clamp(Model.HealthShown / 100.0f, 0.0f, 1.0f) * Segments;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float SegmentX = Column + Index * (SegmentWidth + 2.0f);
			Painter.Fill(HudBox(SegmentX, BarY, SegmentWidth, 3.0f), Fade(Text(), 0.16f));
			const float Share = FMath::Clamp(Filled - Index, 0.0f, 1.0f);
			if (Share > 0.0f)
			{
				Painter.Fill(HudBox(SegmentX, BarY, SegmentWidth * Share, 3.0f), FMath::Lerp(bLow ? Danger() : Text(), Danger(), Model.HealthLoss));
			}
		}

		// Posture: a chevron while the merc crouches or the spy sneaks; two while poisoned in the danger colour.
		const bool bLowProfile = State.Side == 1 ? (State.Flags & EScctHudFlags::Crouched) != 0 : (State.Flags & EScctHudFlags::FastSpeed) == 0;
		if (bLowProfile)
		{
			const FVector2f Tip(Column + 5.0f, Y + 58.0f);
			Painter.Polyline({ Tip + FVector2f(-5.0f, -4.0f), Tip, Tip + FVector2f(5.0f, -4.0f) }, Accent, 1.5f);
		}
		if ((State.Flags & EScctHudFlags::Poisoned) != 0)
		{
			Painter.Fill(HudBox(ColumnRight - 6.0f, Y + 53.0f, 6.0f, 6.0f), Fade(Danger(), 0.6f + 0.4f * FMath::Sin(Model.Clock * 6.0f)), 3.0f);
		}
	}

	// The gauges the original has, right of the block: the spy's energy, the merc's shock protection and the air
	// left in the gas mask (the breath of a scoped merc is on the scope's own page).
	FVector2f GaugeCentre(X + PanelWidth + 40.0f, Y + PanelHeight * 0.5f);
	const auto NextGauge = [&GaugeCentre]() { GaugeCentre.X += 64.0f; };
	if (State.Side == 0 && State.EnergyLevelMax > 0)
	{
		const float Fraction = Model.EnergyShown / State.EnergyLevelMax;
		const bool bCloak = (State.Flags & EScctHudFlags::CloakEngaged) != 0;
		const FLinearColor Colour = Fraction < 0.2f ? Danger() : Fade(Accent, bCloak ? 0.7f + 0.3f * FMath::Sin(Model.Clock * 8.0f) : 1.0f);
		Gauge(Painter, GaugeCentre, Fraction, Colour, FString::FromInt(FMath::RoundToInt32(Fraction * 100.0f)));
		NextGauge();
	}
	if ((State.EffectFlags & EScctHudEffectFlags::ShockImmune) != 0)
	{
		const bool bByState = (State.EffectFlags & EScctHudEffectFlags::ShockImmuneByState) != 0;
		Gauge(Painter, GaugeCentre, bByState ? 1.0f : State.ShockImmunityFraction, Accent,
			bByState ? FString() : FString::FromInt(FMath::CeilToInt32(State.ShockImmunityRemaining)));
		NextGauge();
	}
	if (State.GasMaskAirSeconds >= 0)
	{
		const bool bLowAir = (State.EffectFlags & EScctHudEffectFlags::GasMaskLowAir) != 0;
		Gauge(Painter, GaugeCentre, static_cast<float>(State.GasMaskAirSeconds) / FMath::Max(Model.GasMaskCapacity, 1), bLowAir ? Danger() : Good(), FString::FromInt(State.GasMaskAirSeconds));
		NextGauge();
	}

	// The vision mode, named as the game names it, slides up above the block while the goggles are on.
	if (Model.VisionShown > 0.01f)
	{
		const FString Name = Model.VisionName(Model.VisionMode).ToUpper();
		const float Width = Painter.Measure(Name, Label(9.0f)).X + 46.0f;
		const float ChipY = Y - 34.0f + (1.0f - EaseOut(Model.VisionShown)) * 10.0f;
		Painter.SetOpacity(Model.VisionShown);
		Painter.Glass(HudBox(X, ChipY, Width, 24.0f), 12.0f);
		// An eye: two lids and a pupil.
		const FVector2f Eye(X + 18.0f, ChipY + 12.0f);
		Painter.Arc(Eye + FVector2f(0.0f, 7.0f), 10.0f, -42.0f, 42.0f, Accent, 1.5f);
		Painter.Arc(Eye - FVector2f(0.0f, 7.0f), 10.0f, 138.0f, 222.0f, Accent, 1.5f);
		Painter.Fill(FBox2f(Eye - FVector2f(2.0f), Eye + FVector2f(2.0f)), Accent, 2.0f);
		Painter.Text(FVector2f(X + 34.0f, ChipY + 6.0f), Name, Label(9.0f), Accent);
		Painter.SetOpacity(1.0f);
	}
}

void ScctHudElements::PaintEquipment(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!ShowsPlayerHud(Model))
	{
		return;
	}

	const FScctHudState& State = Model.State;
	const FVector2f Size = Painter.Size();
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	const float Right = Size.X - Margin;
	float Bottom = Size.Y - Margin;

	const bool bWeapon = (State.Flags & EScctHudFlags::HasWeapon) != 0 && (State.Flags & EScctHudFlags::AmmoOff) == 0;
	if (bWeapon)
	{
		const float X = Right - PanelWidth, Y = Bottom - PanelHeight;
		Painter.Glass(HudBox(X, Y, PanelWidth, PanelHeight), Corner);
		Painter.GlowLine(FVector2f(Right - 1.0f, Y + 14.0f), FVector2f(Right - 1.0f, Y + PanelHeight - 14.0f), Accent, 2.0f);

		// The clip is the number to read at a glance; the rounds not in it sit small beside it.
		const int32 Reserve = FMath::Max(State.Ammo - State.ClipAmmo, 0);
		const bool bLow = Model.ClipCapacity > 0 && State.ClipAmmo * 4 <= Model.ClipCapacity;
		const FLinearColor ClipColour = FMath::Lerp(bLow ? Danger() : Text(), Accent, Model.AmmoPulse * 0.6f);
		const FVector2f ClipSize = Painter.Text(FVector2f(Right - 20.0f, Y + 12.0f), FString::FromInt(State.ClipAmmo), Font(EWeight::Light, 30.0f), ClipColour, EAlign::Right);
		Painter.Text(FVector2f(Right - 28.0f - ClipSize.X, Y + 28.0f), FString::FromInt(Reserve), Font(EWeight::Regular, 12.0f), TextDim(), EAlign::Right);

		Painter.Text(FVector2f(X + 18.0f, Y + 15.0f), Model.Text(State.WeaponNameId).ToUpper(), Label(), TextDim());
		Painter.Text(FVector2f(X + 18.0f, Y + 31.0f), Model.Text(State.FireModeNameId).ToUpper(), Label(), Accent);

		// One tick per round while that stays readable, else a plain bar.
		const float BarX = X + 18.0f, BarY = Y + 56.0f, BarWidth = PanelWidth - 38.0f;
		const int32 Capacity = FMath::Max(Model.ClipCapacity, 1);
		if (Capacity <= 40)
		{
			const float Tick = (BarWidth - (Capacity - 1)) / Capacity;
			for (int32 Index = 0; Index < Capacity; ++Index)
			{
				const bool bLoaded = Index < State.ClipAmmo;
				Painter.Fill(HudBox(BarX + Index * (Tick + 1.0f), BarY, Tick, 3.0f), bLoaded ? (bLow ? Danger() : Fade(Text(), 0.9f)) : Fade(Text(), 0.14f));
			}
		}
		else
		{
			Painter.Fill(HudBox(BarX, BarY, BarWidth, 3.0f), Fade(Text(), 0.14f));
			Painter.Fill(HudBox(BarX, BarY, BarWidth * State.ClipAmmo / Capacity, 3.0f), bLow ? Danger() : Fade(Text(), 0.9f));
		}
		Bottom = Y - 12.0f;
	}

	if ((State.Flags & EScctHudFlags::GadgetOff) != 0 || Model.Gadgets.Num() == 0)
	{
		return;
	}

	// The gadgets as a strip of rows in one panel, in the order the player cycles them, the selected one lit.
	constexpr float RowHeight = 24.0f, RowWidth = 208.0f, Inset = 6.0f;
	const float StripHeight = Model.Gadgets.Num() * RowHeight + Inset * 2.0f;
	Painter.Glass(HudBox(Right - RowWidth, Bottom - StripHeight, RowWidth, StripHeight), Corner, 0.68f);
	float RowY = Bottom - StripHeight + Inset;
	for (int32 Index = 0; Index < Model.Gadgets.Num(); ++Index, RowY += RowHeight)
	{
		const FScctHudGadget& Gadget = Model.Gadgets[Index];
		const bool bSelected = (Gadget.Flags & EScctHudGadgetFlags::Selected) != 0 || Index == State.SelectedGadget;
		const bool bUnavailable = (Gadget.Flags & EScctHudGadgetFlags::Unavailable) != 0;
		const bool bEmpty = Gadget.Charge == 0;

		if (bSelected)
		{
			// The highlight flashes a little brighter as the selection lands on the row.
			Painter.Fill(HudBox(Right - RowWidth + 4.0f, RowY, RowWidth - 8.0f, RowHeight), Fade(Accent, 0.13f + 0.22f * Model.GadgetPulse), 4.0f);
			Painter.GlowLine(FVector2f(Right - 1.0f, RowY + 5.0f), FVector2f(Right - 1.0f, RowY + RowHeight - 5.0f), Accent, 2.0f);
		}

		// What the row counts: a held mine hack's progress, the wait for a recharge, else the charges left.
		FString Count;
		if (Gadget.MineHackPercent > 0)
		{
			Count = FString::Printf(TEXT("%d%%"), Gadget.MineHackPercent);
		}
		else if (Gadget.RechargeRemaining > 0.0f)
		{
			Count = FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(Gadget.RechargeRemaining));
		}
		else if (Gadget.Charge >= 0)
		{
			Count = FString::FromInt(Gadget.Charge);
		}

		const FLinearColor NameColour = bUnavailable || bEmpty ? TextFaint() : (bSelected ? Text() : Fade(Text(), 0.74f));
		const FLinearColor CountColour = bUnavailable || bEmpty ? TextFaint() : (bSelected ? Accent : TextDim());
		Painter.Text(FVector2f(Right - 16.0f, RowY + 4.0f), Count, Font(EWeight::Bold, 10.5f), CountColour, EAlign::Right);
		Painter.Text(FVector2f(Right - RowWidth + 16.0f, RowY + 7.0f), Model.Text(Gadget.NameId).ToUpper(), Label(8.0f), NameColour);
	}
}

void ScctHudElements::PaintPrompt(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!Model.bHasState || (Model.State.Flags & EScctHudFlags::HideHud) != 0)
	{
		return;
	}

	const FScctHudState& State = Model.State;
	const FVector2f Size = Painter.Size();
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	const float CentreX = Size.X * 0.5f;
	const float BaseY = Size.Y - Margin - 150.0f;

	// Two or more things to use: the list, with the selection lit once Use is held.
	if ((State.Flags & EScctHudFlags::ShowInteractions) != 0 && Model.Interactions.Num() > 0)
	{
		constexpr float RowHeight = 28.0f, Width = 300.0f;
		const float Height = Model.Interactions.Num() * RowHeight + 12.0f;
		const float Top = BaseY + 30.0f - Height;
		Painter.Glass(HudBox(CentreX - Width * 0.5f, Top, Width, Height), Corner);
		for (int32 Index = 0; Index < Model.Interactions.Num(); ++Index)
		{
			const FScctHudInteraction& Interaction = Model.Interactions[Index];
			const bool bSelected = Interaction.Selected != 0 && (State.Flags & EScctHudFlags::InteractionFocus) != 0;
			const float RowY = Top + 6.0f + Index * RowHeight;
			if (bSelected)
			{
				Painter.Fill(HudBox(CentreX - Width * 0.5f + 6.0f, RowY, Width - 12.0f, RowHeight), Fade(Accent, 0.14f), 4.0f);
				Painter.GlowLine(FVector2f(CentreX - Width * 0.5f + 7.0f, RowY + 6.0f), FVector2f(CentreX - Width * 0.5f + 7.0f, RowY + RowHeight - 6.0f), Accent, 2.0f);
			}
			Painter.Text(FVector2f(CentreX, RowY + 6.0f), Model.Text(Interaction.DescriptionId), Font(EWeight::Regular, 11.0f), bSelected ? Text() : TextDim(), EAlign::Centre);
		}
		return;
	}

	if (Model.PromptShown <= 0.01f || Model.PromptText.IsEmpty())
	{
		return;
	}

	// One thing to use: its own text in a pill, with a diamond in the side's colour.
	const FSlateFontInfo PromptFont = Font(EWeight::Regular, 12.0f, 40);
	const float TextWidth = Painter.Measure(Model.PromptText, PromptFont).X;
	const float Width = TextWidth + 58.0f;
	const float Y = BaseY + (1.0f - EaseOut(Model.PromptShown)) * 8.0f;
	Painter.SetOpacity(Model.PromptShown);
	Painter.Glass(HudBox(CentreX - Width * 0.5f, Y, Width, 32.0f), 16.0f);
	const FVector2f Diamond(CentreX - Width * 0.5f + 20.0f, Y + 16.0f);
	Painter.Polyline({ Diamond + FVector2f(0.0f, -5.0f), Diamond + FVector2f(5.0f, 0.0f), Diamond + FVector2f(0.0f, 5.0f), Diamond + FVector2f(-5.0f, 0.0f), Diamond + FVector2f(0.0f, -5.0f) }, Accent, 1.5f);
	Painter.Text(FVector2f(CentreX - Width * 0.5f + 36.0f, Y + 8.0f), Model.PromptText, PromptFont, Text());
	Painter.SetOpacity(1.0f);
}
