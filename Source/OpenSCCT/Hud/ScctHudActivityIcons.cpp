#include "Hud/ScctHudElements.h"

#include "Hud/ScctHudStyle.h"

// The kill feed's icons and cause line (Godot's HudActivityIcons and HudActivityPresentation). Godot draws licensed
// game-icons.net SVG silhouettes embedded in its own assembly; those are not in the converted content, so the icons
// here are vector equivalents drawn with the HUD painter, one per family of causes and per outcome, in the same
// mapping (a cause first, the outcome of the event when it has none). The cause text is Godot's English copy.

using namespace ScctHudStyle;

namespace
{
	/** The icon families: Godot's icon names grouped by the shape that tells them apart at 16 px. */
	enum class EScctFeedIcon : uint8
	{
		Death, Sleep, Knockout, Join, Leave, LastLife, Match,
		Rifle, Pistol, Shotgun, Sniper, Headshot,
		Explosion, Grenade, Barrel, Mine, LaserMine, PoisonMine,
		Fire, Molotov, Electric, Melee, Charge, Fall, Crush, Gas, Poison, Hazard,
	};

	bool IsCombatKind(int32 Kind)
	{
		const EScctHudActivity Activity = static_cast<EScctHudActivity>(Kind);
		return Activity == EScctHudActivity::Kill || Activity == EScctHudActivity::Death || Activity == EScctHudActivity::Suicide
			|| Activity == EScctHudActivity::Sleep || Activity == EScctHudActivity::Knockout;
	}

	EScctFeedIcon FeedOutcomeIcon(int32 Kind)
	{
		switch (static_cast<EScctHudActivity>(Kind))
		{
		case EScctHudActivity::Kill:
		case EScctHudActivity::Death:
		case EScctHudActivity::Suicide: return EScctFeedIcon::Death;
		case EScctHudActivity::Sleep: return EScctFeedIcon::Sleep;
		case EScctHudActivity::Knockout: return EScctFeedIcon::Knockout;
		case EScctHudActivity::Join: return EScctFeedIcon::Join;
		case EScctHudActivity::Leave:
		case EScctHudActivity::Resign: return EScctFeedIcon::Leave;
		case EScctHudActivity::LastLife: return EScctFeedIcon::LastLife;
		default: return EScctFeedIcon::Match;
		}
	}

	EScctFeedIcon FeedIconFor(const FScctHudLine& Line)
	{
		if (!IsCombatKind(Line.Kind))
		{
			return FeedOutcomeIcon(Line.Kind);
		}
		switch (static_cast<EScctHudCause>(Line.Cause))
		{
		case EScctHudCause::Firearm:
		case EScctHudCause::Rifle:
		case EScctHudCause::Smg: return EScctFeedIcon::Rifle;
		case EScctHudCause::Pistol:
		case EScctHudCause::Revolver: return EScctFeedIcon::Pistol;
		case EScctHudCause::Shotgun: return EScctFeedIcon::Shotgun;
		case EScctHudCause::Sniper: return EScctFeedIcon::Sniper;
		case EScctHudCause::Headshot: return EScctFeedIcon::Headshot;
		case EScctHudCause::Explosion:
		case EScctHudCause::ExplosiveProp:
		case EScctHudCause::GasExplosion:
		case EScctHudCause::ExplosiveExtinguisher:
		case EScctHudCause::SodaExplosion: return EScctFeedIcon::Explosion;
		case EScctHudCause::FragGrenade: return EScctFeedIcon::Grenade;
		case EScctHudCause::BarrelExplosion: return EScctFeedIcon::Barrel;
		case EScctHudCause::ProximityMine: return EScctFeedIcon::Mine;
		case EScctHudCause::LaserMine: return EScctFeedIcon::LaserMine;
		case EScctHudCause::PoisonMine: return EScctFeedIcon::PoisonMine;
		case EScctHudCause::Fire:
		case EScctHudCause::MolotovFire: return EScctFeedIcon::Fire;
		case EScctHudCause::MolotovExplosion: return EScctFeedIcon::Molotov;
		case EScctHudCause::StickyShock:
		case EScctHudCause::StickyBlast:
		case EScctHudCause::Taser:
		case EScctHudCause::ShockCollision: return EScctFeedIcon::Electric;
		case EScctHudCause::Elbow:
		case EScctHudCause::Grab:
		case EScctHudCause::Choke:
		case EScctHudCause::NeckBreak: return EScctFeedIcon::Melee;
		case EScctHudCause::Charge: return EScctFeedIcon::Charge;
		case EScctHudCause::DropAttack:
		case EScctHudCause::Fall:
		case EScctHudCause::LedgePull: return EScctFeedIcon::Fall;
		case EScctHudCause::Crush: return EScctFeedIcon::Crush;
		case EScctHudCause::Smoke:
		case EScctHudCause::StickyCamera: return EScctFeedIcon::Gas;
		case EScctHudCause::Poison: return EScctFeedIcon::Poison;
		case EScctHudCause::Hazard: return EScctFeedIcon::Hazard;
		default: return FeedOutcomeIcon(Line.Kind);
		}
	}

	const TCHAR* FeedCauseName(int32 Cause)
	{
		switch (static_cast<EScctHudCause>(Cause))
		{
		case EScctHudCause::None: return TEXT("");
		case EScctHudCause::Firearm: return TEXT("Firearm");
		case EScctHudCause::Headshot: return TEXT("Headshot");
		case EScctHudCause::Explosion: return TEXT("Explosion");
		case EScctHudCause::Charge: return TEXT("Charge");
		case EScctHudCause::DropAttack: return TEXT("Drop attack");
		case EScctHudCause::Elbow: return TEXT("Elbow strike");
		case EScctHudCause::Grab: return TEXT("Grab");
		case EScctHudCause::NeckBreak: return TEXT("Neck break");
		case EScctHudCause::Choke: return TEXT("Choke");
		case EScctHudCause::Fall: return TEXT("Fall");
		case EScctHudCause::Crush: return TEXT("Crush");
		case EScctHudCause::Poison: return TEXT("Poison");
		case EScctHudCause::StickyShock: return TEXT("Electrical damage");
		case EScctHudCause::StickyBlast: return TEXT("Fatal electrical contact");
		case EScctHudCause::Taser: return TEXT("Taser");
		case EScctHudCause::ShockCollision: return TEXT("Fatal charge collision");
		case EScctHudCause::Smoke: return TEXT("Smoke");
		case EScctHudCause::StickyCamera: return TEXT("Camera gas");
		case EScctHudCause::ProximityMine: return TEXT("Proximity mine");
		case EScctHudCause::LaserMine: return TEXT("Laser mine");
		case EScctHudCause::PoisonMine: return TEXT("Poison mine");
		case EScctHudCause::FragGrenade: return TEXT("Frag grenade");
		case EScctHudCause::BarrelExplosion: return TEXT("Explosive barrel");
		case EScctHudCause::ExplosiveProp: return TEXT("Prop damage");
		case EScctHudCause::MolotovExplosion: return TEXT("Molotov blast");
		case EScctHudCause::MolotovFire: return TEXT("Molotov fire");
		case EScctHudCause::Fire: return TEXT("Fire");
		case EScctHudCause::Hazard: return TEXT("Environmental hazard");
		case EScctHudCause::LedgePull: return TEXT("Ledge pull");
		case EScctHudCause::Rifle: return TEXT("Rifle");
		case EScctHudCause::Shotgun: return TEXT("Shotgun");
		case EScctHudCause::Smg: return TEXT("SMG");
		case EScctHudCause::Sniper: return TEXT("Sniper");
		case EScctHudCause::Pistol: return TEXT("Pistol");
		case EScctHudCause::Revolver: return TEXT("Revolver");
		case EScctHudCause::GasExplosion: return TEXT("Gas explosion");
		case EScctHudCause::ExplosiveExtinguisher: return TEXT("Explosive canister");
		case EScctHudCause::SodaExplosion: return TEXT("Soda can blast");
		default: return TEXT("Other");
		}
	}

	bool IsFireCause(int32 Cause)
	{
		return Cause == static_cast<int32>(EScctHudCause::Fire) || Cause == static_cast<int32>(EScctHudCause::MolotovFire);
	}
}

FString ScctHudElements::ActivityCauseText(const FScctHudLine& Line)
{
	if (!IsCombatKind(Line.Kind))
	{
		return FString();
	}
	FString Text = FeedCauseName(Line.Cause);
	const bool bHeadshot = (Line.Flags & EScctHudMessageFlags::Headshot) != 0;
	if (bHeadshot && Line.Cause != static_cast<int32>(EScctHudCause::Headshot))
	{
		Text = Text.IsEmpty() ? FString(TEXT("Headshot")) : Text + TEXT(" headshot");
	}

	// Who or what set it off (Godot's TriggerDetail), when the game says.
	FString Trigger;
	const bool bName = !Line.TriggeredBy.IsEmpty();
	if (Line.Trigger == 2)
	{
		Trigger = bName ? TEXT("Chain reaction · ") + Line.TriggeredBy : FString(TEXT("Chain reaction"));
	}
	else if (Line.Trigger == 1 && IsFireCause(Line.Cause))
	{
		Trigger = bName ? TEXT("Ignited by ") + Line.TriggeredBy : FString(TEXT("Ignited by a shot"));
	}
	else if (Line.Trigger == 1)
	{
		Trigger = bName ? TEXT("Shot by ") + Line.TriggeredBy : FString(TEXT("Detonated by a shot"));
	}
	else if (bName)
	{
		Trigger = TEXT("Triggered by ") + Line.TriggeredBy;
	}

	if (Trigger.IsEmpty())
	{
		return Text;
	}
	return Text.IsEmpty() ? Trigger : Text + TEXT("  ·  ") + Trigger;
}

void ScctHudElements::PaintActivityIcon(FScctHudPainter& Painter, const FVector2f& Centre, float Extent, const FScctHudLine& Line, const FLinearColor& Colour)
{
	// Shapes are written on a -1..1 square; U turns them into units.
	const float U = Extent * 0.5f;
	const auto P = [&Centre, U](float X, float Y) { return Centre + FVector2f(X, Y) * U; };
	const auto Poly = [&](std::initializer_list<FVector2f> Points, float Thickness = 1.5f) { Painter.Polyline(TArray<FVector2f>(Points), Colour, Thickness); };
	const auto Dot = [&](float X, float Y, float Radius) { const FVector2f At = P(X, Y); Painter.Fill(FBox2f(At - FVector2f(Radius), At + FVector2f(Radius)), Colour, Radius); };
	const auto Ring = [&](float X, float Y, float Radius, float Thickness = 1.5f) { Painter.Arc(P(X, Y), Radius * U, 0.0f, 360.0f, Colour, Thickness); };
	const auto Burst = [&](float X, float Y, float Outer, float Inner, int32 Points)
	{
		TArray<FVector2f> Star;
		for (int32 Index = 0; Index <= Points * 2; ++Index)
		{
			const float Angle = PI * Index / Points - HALF_PI;
			const float Radius = (Index % 2 == 0) ? Outer : Inner;
			Star.Add(P(X + FMath::Cos(Angle) * Radius, Y + FMath::Sin(Angle) * Radius));
		}
		Painter.Polyline(MoveTemp(Star), Colour, 1.5f);
	};
	const auto Flame = [&](float X, float Y, float Scale)
	{
		Poly({ P(X, Y - 1.0f * Scale), P(X + 0.55f * Scale, Y - 0.1f * Scale), P(X + 0.6f * Scale, Y + 0.45f * Scale), P(X + 0.25f * Scale, Y + 0.9f * Scale),
			P(X - 0.25f * Scale, Y + 0.9f * Scale), P(X - 0.6f * Scale, Y + 0.45f * Scale), P(X - 0.45f * Scale, Y - 0.05f * Scale), P(X - 0.15f * Scale, Y + 0.2f * Scale), P(X, Y - 1.0f * Scale) });
	};
	const auto Cloud = [&](float Y)
	{
		Painter.Arc(P(-0.45f, Y), 0.38f * U, 270.0f, 450.0f, Colour, 1.5f);
		Painter.Arc(P(0.05f, Y - 0.2f), 0.48f * U, 270.0f, 450.0f, Colour, 1.5f);
		Painter.Arc(P(0.5f, Y + 0.05f), 0.35f * U, 270.0f, 450.0f, Colour, 1.5f);
		Poly({ P(-0.83f, Y), P(-0.83f, Y + 0.38f), P(0.85f, Y + 0.38f), P(0.85f, Y + 0.05f) });
	};
	const auto Rifle = [&](float Length)
	{
		Poly({ P(-1.0f, -0.05f), P(-0.55f, -0.25f), P(Length, -0.25f), P(Length, -0.02f), P(-0.05f, -0.02f), P(-0.25f, 0.55f), P(-0.5f, 0.55f), P(-0.4f, 0.1f), P(-1.0f, 0.3f), P(-1.0f, -0.05f) });
	};

	switch (FeedIconFor(Line))
	{
	case EScctFeedIcon::Death:
		// A skull: the head, two eyes and the jaw.
		Painter.Arc(P(0.0f, -0.2f), 0.68f * U, 210.0f, 510.0f, Colour, 1.5f);
		Poly({ P(-0.35f, 0.4f), P(-0.35f, 0.85f), P(0.35f, 0.85f), P(0.35f, 0.4f) });
		Dot(-0.28f, -0.2f, 0.17f * U);
		Dot(0.28f, -0.2f, 0.17f * U);
		break;
	case EScctFeedIcon::Sleep:
	case EScctFeedIcon::Knockout:
	{
		// Two Zs, the big one lower left; a knock-out adds an impact star.
		Poly({ P(-0.9f, -0.1f), P(-0.1f, -0.1f), P(-0.9f, 0.8f), P(-0.1f, 0.8f) });
		Poly({ P(0.2f, -0.8f), P(0.8f, -0.8f), P(0.2f, -0.15f), P(0.8f, -0.15f) }, 1.25f);
		if (FeedIconFor(Line) == EScctFeedIcon::Knockout)
		{
			Burst(0.55f, 0.55f, 0.4f, 0.18f, 5);
		}
		break;
	}
	case EScctFeedIcon::Join:
		Ring(0.0f, 0.0f, 0.85f);
		Poly({ P(-0.45f, 0.0f), P(0.45f, 0.0f) });
		Poly({ P(0.0f, -0.45f), P(0.0f, 0.45f) });
		break;
	case EScctFeedIcon::Leave:
		// A door frame and an arrow out of it.
		Poly({ P(0.1f, -0.85f), P(-0.8f, -0.85f), P(-0.8f, 0.85f), P(0.1f, 0.85f) });
		Poly({ P(-0.3f, 0.0f), P(0.95f, 0.0f) });
		Poly({ P(0.6f, -0.35f), P(0.95f, 0.0f), P(0.6f, 0.35f) });
		break;
	case EScctFeedIcon::LastLife:
		// A heart.
		Painter.Arc(P(-0.4f, -0.3f), 0.42f * U, 225.0f, 450.0f, Colour, 1.5f);
		Painter.Arc(P(0.4f, -0.3f), 0.42f * U, 270.0f, 495.0f, Colour, 1.5f);
		Poly({ P(-0.7f, 0.0f), P(0.0f, 0.85f), P(0.7f, 0.0f) });
		break;
	case EScctFeedIcon::Match:
		Poly({ P(0.0f, -0.85f), P(0.85f, 0.0f), P(0.0f, 0.85f), P(-0.85f, 0.0f), P(0.0f, -0.85f) });
		Dot(0.0f, 0.0f, 0.15f * U);
		break;
	case EScctFeedIcon::Rifle:
		Rifle(1.0f);
		break;
	case EScctFeedIcon::Sniper:
		Rifle(1.0f);
		// The scope on top.
		Poly({ P(-0.4f, -0.25f), P(-0.4f, -0.55f), P(0.35f, -0.55f), P(0.35f, -0.25f) }, 1.25f);
		break;
	case EScctFeedIcon::Shotgun:
		Rifle(0.9f);
		Poly({ P(-0.05f, -0.45f), P(0.9f, -0.45f) }, 1.25f);
		break;
	case EScctFeedIcon::Pistol:
		Poly({ P(-0.75f, -0.45f), P(0.85f, -0.45f), P(0.85f, -0.1f), P(-0.05f, -0.1f), P(-0.2f, 0.75f), P(-0.7f, 0.75f), P(-0.5f, -0.1f), P(-0.75f, -0.1f), P(-0.75f, -0.45f) });
		break;
	case EScctFeedIcon::Headshot:
		Ring(0.0f, 0.0f, 0.62f);
		Poly({ P(0.0f, -1.0f), P(0.0f, -0.35f) });
		Poly({ P(0.0f, 0.35f), P(0.0f, 1.0f) });
		Poly({ P(-1.0f, 0.0f), P(-0.35f, 0.0f) });
		Poly({ P(0.35f, 0.0f), P(1.0f, 0.0f) });
		Dot(0.0f, 0.0f, 0.12f * U);
		break;
	case EScctFeedIcon::Explosion:
		Burst(0.0f, 0.0f, 0.98f, 0.45f, 8);
		break;
	case EScctFeedIcon::Grenade:
		Ring(-0.1f, 0.25f, 0.62f);
		Poly({ P(-0.25f, -0.35f), P(-0.25f, -0.6f), P(0.1f, -0.6f), P(0.1f, -0.35f) }, 1.25f);
		Poly({ P(0.1f, -0.55f), P(0.75f, -0.15f) }, 1.25f);
		break;
	case EScctFeedIcon::Barrel:
		Poly({ P(-0.6f, -0.9f), P(0.6f, -0.9f), P(0.6f, 0.9f), P(-0.6f, 0.9f), P(-0.6f, -0.9f) });
		Poly({ P(-0.6f, -0.3f), P(0.6f, -0.3f) }, 1.25f);
		Poly({ P(-0.6f, 0.3f), P(0.6f, 0.3f) }, 1.25f);
		break;
	case EScctFeedIcon::Mine:
	case EScctFeedIcon::LaserMine:
	case EScctFeedIcon::PoisonMine:
	{
		// A dome on its plate with its sensor; a laser mine sends its beam out, a poison mine has a drop over it.
		Painter.Arc(P(0.0f, 0.45f), 0.6f * U, 270.0f, 450.0f, Colour, 1.5f);
		Poly({ P(-0.95f, 0.45f), P(0.95f, 0.45f) });
		Poly({ P(0.0f, -0.15f), P(0.0f, -0.4f) }, 1.25f);
		const EScctFeedIcon Icon = FeedIconFor(Line);
		if (Icon == EScctFeedIcon::LaserMine)
		{
			Poly({ P(0.25f, -0.4f), P(1.0f, -0.8f) }, 1.0f);
		}
		else if (Icon == EScctFeedIcon::PoisonMine)
		{
			Poly({ P(0.65f, -0.95f), P(0.9f, -0.55f), P(0.65f, -0.35f), P(0.4f, -0.55f), P(0.65f, -0.95f) }, 1.25f);
		}
		break;
	}
	case EScctFeedIcon::Fire:
		Flame(0.0f, 0.0f, 0.95f);
		break;
	case EScctFeedIcon::Molotov:
		// A bottle with its burning rag.
		Poly({ P(-0.15f, -0.35f), P(-0.15f, -0.1f), P(-0.45f, 0.15f), P(-0.45f, 0.95f), P(0.45f, 0.95f), P(0.45f, 0.15f), P(0.15f, -0.1f), P(0.15f, -0.35f) });
		Flame(0.0f, -0.65f, 0.35f);
		break;
	case EScctFeedIcon::Electric:
		Poly({ P(0.25f, -1.0f), P(-0.5f, 0.1f), P(0.05f, 0.1f), P(-0.25f, 1.0f), P(0.55f, -0.2f), P(0.0f, -0.2f), P(0.25f, -1.0f) });
		break;
	case EScctFeedIcon::Melee:
		// An impact: a fist's knuckles against short strike lines.
		Poly({ P(-0.95f, -0.4f), P(-0.2f, -0.4f), P(-0.2f, 0.45f), P(-0.95f, 0.45f) });
		Poly({ P(-0.2f, -0.15f), P(-0.55f, -0.15f) }, 1.0f);
		Poly({ P(-0.2f, 0.15f), P(-0.55f, 0.15f) }, 1.0f);
		Poly({ P(0.15f, 0.0f), P(0.95f, 0.0f) });
		Poly({ P(0.1f, -0.45f), P(0.7f, -0.85f) });
		Poly({ P(0.1f, 0.45f), P(0.7f, 0.85f) });
		break;
	case EScctFeedIcon::Charge:
		Poly({ P(-0.2f, 0.0f), P(0.95f, 0.0f) });
		Poly({ P(0.5f, -0.45f), P(0.95f, 0.0f), P(0.5f, 0.45f) });
		Poly({ P(-0.95f, -0.45f), P(-0.35f, -0.45f) }, 1.0f);
		Poly({ P(-0.95f, 0.45f), P(-0.35f, 0.45f) }, 1.0f);
		break;
	case EScctFeedIcon::Fall:
		Poly({ P(0.0f, -0.95f), P(0.0f, 0.55f) });
		Poly({ P(-0.45f, 0.1f), P(0.0f, 0.55f), P(0.45f, 0.1f) });
		Poly({ P(-0.8f, 0.92f), P(0.8f, 0.92f) });
		break;
	case EScctFeedIcon::Crush:
		Poly({ P(-0.7f, -0.95f), P(0.7f, -0.95f), P(0.7f, -0.35f), P(-0.7f, -0.35f), P(-0.7f, -0.95f) });
		Poly({ P(-0.4f, -0.05f), P(0.0f, 0.35f), P(0.4f, -0.05f) }, 1.25f);
		Poly({ P(-0.9f, 0.9f), P(0.9f, 0.9f) });
		break;
	case EScctFeedIcon::Gas:
		Cloud(0.1f);
		break;
	case EScctFeedIcon::Poison:
		// A drop with a cross on it.
		Poly({ P(0.0f, -0.95f), P(0.6f, 0.15f), P(0.45f, 0.7f), P(0.0f, 0.92f), P(-0.45f, 0.7f), P(-0.6f, 0.15f), P(0.0f, -0.95f) });
		Poly({ P(-0.25f, 0.2f), P(0.25f, 0.6f) }, 1.25f);
		Poly({ P(0.25f, 0.2f), P(-0.25f, 0.6f) }, 1.25f);
		break;
	case EScctFeedIcon::Hazard:
		Poly({ P(0.0f, -0.95f), P(0.95f, 0.8f), P(-0.95f, 0.8f), P(0.0f, -0.95f) });
		Poly({ P(0.0f, -0.35f), P(0.0f, 0.25f) });
		Dot(0.0f, 0.52f, 0.1f * U);
		break;
	}

	// A firearm's headshot carries the sight ring at its corner, as Godot's "-headshot" icons do.
	const EScctFeedIcon Icon = FeedIconFor(Line);
	const bool bGun = Icon == EScctFeedIcon::Rifle || Icon == EScctFeedIcon::Pistol || Icon == EScctFeedIcon::Shotgun || Icon == EScctFeedIcon::Sniper;
	if (bGun && (Line.Flags & EScctHudMessageFlags::Headshot) != 0)
	{
		Painter.Arc(P(0.8f, 0.65f), 0.3f * U, 0.0f, 360.0f, Danger(), 1.25f);
	}
}
