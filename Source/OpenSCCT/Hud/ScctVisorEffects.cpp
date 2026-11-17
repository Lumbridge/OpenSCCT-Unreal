#include "Hud/ScctVisorEffects.h"

#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudPainter.h"
#include "Hud/ScctHudStyle.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "OpenSCCT.h"

using ScctHudStyle::Fade;

namespace
{
	/** Seconds the faked foam splash lasts before it settles (the bridge's 10 HUD steps of a splash of 1). */
	constexpr float ForcedSplashSeconds = 0.2f;

	/** The faked foam's stay (SetHudProjection(1, 5, 10)). */
	constexpr int32 ForcedFoamStay = 5;

	/** The faked rain stops this long after it starts, so the droplets' run-out can be seen too. */
	constexpr float ForcedWetSeconds = 12.0f;

	bool HasFlag(const FScctHudState& State, int32 Flag)
	{
		return (State.EffectFlags & Flag) != 0;
	}

	/** The local merc with a live HUD: the only player whose visor these effects belong to. */
	bool IsMercView(const FScctHudModel& Model)
	{
		return Model.bHasState
			&& Model.State.Side == 1
			&& (Model.State.Flags & EScctHudFlags::HasPawn) != 0
			&& (Model.State.Flags & (EScctHudFlags::Spectating | EScctHudFlags::GameEnded)) == 0;
	}

	/** A filled circle. */
	void Disc(FScctHudPainter& Painter, const FVector2f& Centre, float Radius, const FLinearColor& Colour)
	{
		if (Radius > 0.5f && Colour.A > 0.003f)
		{
			Painter.Fill(FBox2f(Centre - FVector2f(Radius), Centre + FVector2f(Radius)), Colour, Radius);
		}
	}

	/**
	 * Interference over the visor: torn horizontal bands, fine scanlines and bright specks, new every 1/24 s.
	 * Strength scales all of it (1 is the perturbation of a hit).
	 */
	void PaintStatic(FScctHudPainter& Painter, float Now, float Strength)
	{
		if (Strength <= 0.01f)
		{
			return;
		}
		const FVector2f Size = Painter.Size();
		const float S = Size.Y / 1080.0f;
		FRandomStream Random(static_cast<int32>(FMath::FloorToInt(Now * 24.0f)) * 7919 + 17);

		// A grey veil, so the picture behind loses contrast as a jammed display does.
		Painter.Fill(FBox2f(FVector2f::ZeroVector, Size), FLinearColor(0.45f, 0.47f, 0.5f, 0.07f * Strength));

		// Scanlines: thin dark rows every few pixels, drifting down.
		const float Pitch = 4.0f * S;
		const float Offset = FMath::Fmod(Now * 40.0f * S, Pitch);
		for (float Y = Offset; Y < Size.Y; Y += Pitch)
		{
			Painter.Fill(FBox2f(FVector2f(0.0f, Y), FVector2f(Size.X, Y + 1.2f * S)), FLinearColor(0.0f, 0.0f, 0.0f, 0.10f * Strength));
		}

		// Torn bands: a few bright rows of noise the height of a line of text or more.
		const int32 Bands = Random.RandRange(3, 7);
		for (int32 Index = 0; Index < Bands; ++Index)
		{
			const float Y = Random.FRandRange(0.0f, Size.Y);
			const float Height = Random.FRandRange(3.0f, 34.0f) * S;
			const float Grey = Random.FRandRange(0.55f, 1.0f);
			Painter.Fill(FBox2f(FVector2f(0.0f, Y), FVector2f(Size.X, Y + Height)), FLinearColor(Grey, Grey, Grey, Random.FRandRange(0.06f, 0.22f) * Strength));
			// A bright edge where the band tears.
			Painter.Fill(FBox2f(FVector2f(Random.FRandRange(0.0f, Size.X * 0.5f), Y), FVector2f(Random.FRandRange(Size.X * 0.5f, Size.X), Y + 1.5f * S)),
				FLinearColor(1.0f, 1.0f, 1.0f, 0.35f * Strength));
		}

		// Specks of snow.
		const int32 Specks = FMath::RoundToInt32(260.0f * Strength);
		for (int32 Index = 0; Index < Specks; ++Index)
		{
			const FVector2f At(Random.FRandRange(0.0f, Size.X), Random.FRandRange(0.0f, Size.Y));
			const FVector2f Extent(Random.FRandRange(1.5f, 7.0f) * S, Random.FRandRange(1.0f, 2.5f) * S);
			const float Grey = Random.FRandRange(0.6f, 1.0f);
			Painter.Fill(FBox2f(At, At + Extent), FLinearColor(Grey, Grey, Grey, Random.FRandRange(0.25f, 0.7f) * Strength));
		}
	}

	/** The EMP shutdown: the display collapses, the dead visor sits dimmed, then a reboot sweep brings it back. */
	void PaintShutDown(FScctHudPainter& Painter, const FScctVisorState& Visor, float Now)
	{
		const FVector2f Size = Painter.Size();
		const float S = Size.Y / 1080.0f;
		const FLinearColor Accent = ScctHudStyle::Accent(1);

		if (!Visor.bRebooting)
		{
			const float Age = Now - Visor.StartedAt;
			// The dead visor: darker, with the faint rows of a display that is off, and a slow dark band rolling down it.
			Painter.Fill(FBox2f(FVector2f::ZeroVector, Size), FLinearColor(0.0f, 0.0f, 0.0f, 0.24f));
			for (float Row = 0.0f; Row < Size.Y; Row += 3.0f * S)
			{
				Painter.Fill(FBox2f(FVector2f(0.0f, Row), FVector2f(Size.X, Row + 1.0f * S)), FLinearColor(0.0f, 0.0f, 0.0f, 0.12f));
			}
			const float BandY = FMath::Fmod(Age * 0.09f, 1.2f) * Size.Y - 0.1f * Size.Y;
			Painter.Gradient(FBox2f(FVector2f(0.0f, BandY), FVector2f(Size.X, BandY + 0.1f * Size.Y)), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.14f), true);

			// The first 0.6 s: interference, and the picture of the display squeezed into a line that goes out.
			constexpr float Collapse = 0.6f;
			if (Age < Collapse)
			{
				const float Share = Age / Collapse;
				PaintStatic(Painter, Now, 1.0f - Share);
				const float Half = FMath::Max(Size.Y * 0.5f * (1.0f - Share) * (1.0f - Share), 1.0f * S);
				const float Mid = Size.Y * 0.5f;
				Painter.Fill(FBox2f(FVector2f(0.0f, Mid - Half), FVector2f(Size.X, Mid + Half)), Fade(Accent, 0.10f * (1.0f - Share)));
				const float Width = Size.X * (1.0f - Share * Share);
				Painter.GlowLine(FVector2f((Size.X - Width) * 0.5f, Mid), FVector2f((Size.X + Width) * 0.5f, Mid), Fade(FLinearColor::White, 1.0f - Share), 2.0f * S);
			}
			return;
		}

		// The reboot: a bright line sweeps down the visor over the 2 s, lighting what it has passed for a moment.
		const float Share = FMath::Clamp((Now - Visor.RebootAt) / ScctVisorEffects::RebootSeconds, 0.0f, 1.0f);
		const float Y = Share * Size.Y;
		Painter.Gradient(FBox2f(FVector2f(0.0f, FMath::Max(Y - 0.25f * Size.Y, 0.0f)), FVector2f(Size.X, Y)), Fade(Accent, 0.0f), Fade(Accent, 0.12f), true);
		Painter.GlowLine(FVector2f(0.0f, Y), FVector2f(Size.X, Y), Fade(Accent, 0.9f), 2.0f * S);
		Painter.Fill(FBox2f(FVector2f(0.0f, Y), Size), FLinearColor(0.0f, 0.0f, 0.0f, 0.16f));
		if (Share < 0.25f)
		{
			PaintStatic(Painter, Now, 0.4f * (1.0f - Share / 0.25f));
		}
	}

	/**
	 * The extinguisher foam: clumps of foam that splat onto the visor as it is sprayed, stay while it lies there and,
	 * once its stay is over, slide down and thin out over the 10 s the original takes to clean it. Returns true while
	 * any foam is on the visor (the perturbation is not drawn then, as the merc page skips it).
	 */
	bool PaintFoam(FScctHudPainter& Painter, const FScctHudModel& Model, const FScctVisorState& Visor)
	{
		const FScctHudState& State = Model.State;
		const bool bSplash = HasFlag(State, EScctHudEffectFlags::FoamSplash);
		const bool bOn = HasFlag(State, EScctHudEffectFlags::FoamOn);
		if ((!bSplash && !bOn) || Visor.FoamSeed == 0)
		{
			return false;
		}

		const float Now = State.TimeSeconds;
		const FVector2f Size = Painter.Size();
		const float S = Size.Y / 1080.0f;
		const float SplashAge = Now - Visor.FoamSplashAt;
		// How far the cleaning is: 0 while the foam stays, to 1 when it is gone.
		const float CleanFrom = static_cast<float>(State.FoamStopTime + State.FoamStaySeconds);
		const float Clean = bOn && !bSplash ? FMath::Clamp((Now - CleanFrom) / ScctVisorEffects::FoamCleanSeconds, 0.0f, 1.0f) : 0.0f;

		FRandomStream Random(static_cast<int32>(Visor.FoamSeed));
		constexpr int32 Clumps = 18;
		for (int32 Clump = 0; Clump < Clumps; ++Clump)
		{
			// Most clumps near the edges and corners, some across the middle of the view.
			FVector2f Centre(Random.FRandRange(-0.05f, 1.05f) * Size.X, Random.FRandRange(-0.05f, 1.05f) * Size.Y);
			if (Clump % 3 != 0)
			{
				const FVector2f Pull = Centre - Size * 0.5f;
				Centre = Size * 0.5f + Pull * Random.FRandRange(1.0f, 1.35f);
			}
			const float ClumpSize = Random.FRandRange(70.0f, 190.0f) * S;
			const int32 Puffs = Random.RandRange(5, 9);
			const float Delay = Random.FRandRange(0.0f, 0.35f);
			const float Drift = Random.FRandRange(0.15f, 0.45f) * Size.Y;
			// The clumps go one after the other while cleaning.
			const float CleanStart = Random.FRandRange(0.0f, 0.5f);

			// Splatting on: each clump grows quickly to its size, a little later than the one before.
			const float Grow = FMath::Clamp((SplashAge - Delay) / 0.25f, 0.0f, 1.0f);
			const float Scale = ScctHudStyle::EaseOut(Grow);
			const float Gone = FMath::Clamp((Clean - CleanStart) / 0.5f, 0.0f, 1.0f);
			// A clump that is not seen still draws its random numbers (Disc skips it), so every clump keeps its look.
			const float Alpha = Scale * (1.0f - Gone);
			const FVector2f Slide(0.0f, Drift * Gone * Gone);
			const float Shrink = 1.0f - 0.45f * Gone;

			FVector3f Shape[9];
			for (int32 Puff = 0; Puff < Puffs; ++Puff)
			{
				const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
				const float Out = Random.FRandRange(0.0f, 0.75f) * ClumpSize;
				const float Radius = Random.FRandRange(0.35f, 0.75f) * ClumpSize * Scale * Shrink;
				const FVector2f At = Centre + Slide + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Out * Scale;
				Shape[Puff] = FVector3f(At.X, At.Y, Radius);
			}
			// The puffs of a clump are one mass: every grey rim first, then every white body over them, then the lit tops,
			// so no puff shows its own outline inside the clump.
			for (int32 Puff = 0; Puff < Puffs; ++Puff)
			{
				const FVector2f At(Shape[Puff].X, Shape[Puff].Y);
				Disc(Painter, At + FVector2f(0.0f, 0.08f * Shape[Puff].Z), Shape[Puff].Z * 1.07f, FLinearColor(0.40f, 0.44f, 0.48f, 0.5f * Alpha));
			}
			for (int32 Puff = 0; Puff < Puffs; ++Puff)
			{
				Disc(Painter, FVector2f(Shape[Puff].X, Shape[Puff].Y), Shape[Puff].Z, FLinearColor(0.80f, 0.83f, 0.86f, Alpha));
			}
			for (int32 Puff = 0; Puff < Puffs; ++Puff)
			{
				const FVector2f At(Shape[Puff].X, Shape[Puff].Y);
				Disc(Painter, At - FVector2f(0.2f, 0.24f) * Shape[Puff].Z, Shape[Puff].Z * 0.6f, FLinearColor(0.92f, 0.94f, 0.96f, 0.45f * Alpha));
			}
			// A few bubbles on the clump.
			for (int32 Bubble = 0; Bubble < 4; ++Bubble)
			{
				const FVector2f At = Centre + Slide + FVector2f(Random.FRandRange(-0.6f, 0.6f), Random.FRandRange(-0.6f, 0.6f)) * ClumpSize * Scale;
				const float Radius = Random.FRandRange(4.0f, 11.0f) * S * Scale;
				Disc(Painter, At, Radius, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f * Alpha));
				Disc(Painter, At + FVector2f(0.2f, 0.2f) * Radius, Radius * 0.55f, FLinearColor(0.70f, 0.75f, 0.80f, 0.6f * Alpha));
			}
		}
		return true;
	}

	/**
	 * Rain on the visor: beads that sit, then run down leaving a wet trail, each on its own cycle. While the pawn is in
	 * the rain new runs keep starting; once it leaves, the runs already started finish and no more begin (the merc page
	 * plays the droplets' outro instead of cutting them).
	 */
	void PaintDroplets(FScctHudPainter& Painter, const FScctVisorState& Visor, float Now)
	{
		if (Visor.WetSince < 0.0f)
		{
			return;
		}
		const FVector2f Size = Painter.Size();
		const float S = Size.Y / 1080.0f;
		FRandomStream Random(4271);
		constexpr int32 Drops = 34;
		for (int32 Drop = 0; Drop < Drops; ++Drop)
		{
			const float X0 = Random.FRandRange(0.02f, 0.98f) * Size.X;
			const float Period = Random.FRandRange(2.2f, 4.8f);
			const float Phase = Random.FRandRange(0.0f, Period);
			const float Radius = Random.FRandRange(7.0f, 18.0f) * S;
			const float Hold = Random.FRandRange(0.3f, 0.55f);
			const float Wobble = Random.FRandRange(-1.0f, 1.0f);

			// This drop's current cycle and when it began.
			const float Cycle = FMath::FloorToFloat((Now + Phase) / Period);
			const float CycleStart = Cycle * Period - Phase;
			if (CycleStart < Visor.WetSince - Period * 0.5f)
			{
				continue;
			}
			if (!Visor.bWet && (Visor.DryAt < 0.0f || CycleStart >= Visor.DryAt))
			{
				continue;
			}

			// Each cycle starts the bead somewhere else up the visor.
			FRandomStream CycleRandom(Drop * 977 + static_cast<int32>(Cycle) * 131);
			const float X = X0 + CycleRandom.FRandRange(-0.04f, 0.04f) * Size.X;
			const float Y0 = CycleRandom.FRandRange(-0.05f, 0.65f) * Size.Y;
			const float Share = (Now - CycleStart) / Period;
			float Y = Y0;
			float Bead = Radius * (0.6f + 0.4f * FMath::Min(Share / Hold, 1.0f));
			if (Share > Hold)
			{
				// Running: it speeds up as it goes and thins out, wandering a little sideways.
				const float Run = (Share - Hold) / (1.0f - Hold);
				Y = Y0 + Run * Run * (Size.Y * 1.1f - Y0);
				Bead = Radius * (1.0f - 0.35f * Run);
				const float Sway = FMath::Sin(Run * 9.0f + Wobble * 3.0f) * 3.0f * S;
				// The wet trail behind it: wide and faint, drying out further up (two lengths of it).
				const FVector2f Bottom(X + Sway, Y);
				const float Trail = FMath::Min(Y - Y0, 0.22f * Size.Y);
				Painter.Line(FVector2f(X, Y - Trail), Bottom, FLinearColor(0.75f, 0.82f, 0.88f, 0.07f), Bead * 1.0f);
				Painter.Line(FVector2f(X, Y - Trail * 0.5f), Bottom, FLinearColor(0.85f, 0.90f, 0.95f, 0.10f), Bead * 0.8f);
				Painter.Line(FVector2f(X, Y - Trail * 0.5f), Bottom, FLinearColor(1.0f, 1.0f, 1.0f, 0.14f), FMath::Max(Bead * 0.2f, 1.0f));
				Y = Bottom.Y;
			}
			const FVector2f At(Share > Hold ? X + FMath::Sin(((Share - Hold) / (1.0f - Hold)) * 9.0f + Wobble * 3.0f) * 3.0f * S : X, Y);
			// A dark rim, a clear body, a bright point where the light catches it.
			Disc(Painter, At, Bead * 1.15f, FLinearColor(0.03f, 0.05f, 0.07f, 0.40f));
			Disc(Painter, At, Bead, FLinearColor(0.70f, 0.78f, 0.85f, 0.30f));
			Disc(Painter, At + FVector2f(0.2f, 0.25f) * Bead, Bead * 0.6f, FLinearColor(0.88f, 0.92f, 0.96f, 0.25f));
			Disc(Painter, At - FVector2f(0.35f, 0.40f) * Bead, FMath::Max(Bead * 0.25f, 1.5f), FLinearColor(1.0f, 1.0f, 1.0f, 0.85f));
		}
	}

	/** One electric arc from A to B: a jagged line with a soft glow, and a fork or two. */
	void Bolt(FScctHudPainter& Painter, FRandomStream& Random, const FVector2f& From, const FVector2f& To, float S, float Alpha, int32 Depth = 0)
	{
		const FVector2f Along = To - From;
		const float Length = Along.Size();
		if (Length < 4.0f)
		{
			return;
		}
		const FVector2f Across = FVector2f(-Along.Y, Along.X) / Length;
		const int32 Segments = Depth == 0 ? 14 : 6;
		TArray<FVector2f> Points;
		Points.Reserve(Segments + 1);
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float Share = static_cast<float>(Index) / Segments;
			const float Kick = (Index == 0 || Index == Segments) ? 0.0f : Random.FRandRange(-1.0f, 1.0f) * Length * 0.07f;
			Points.Add(From + Along * Share + Across * Kick);
		}
		Painter.Polyline(Points, FLinearColor(0.35f, 0.55f, 1.0f, 0.22f * Alpha), (Depth == 0 ? 9.0f : 5.0f) * S);
		Painter.Polyline(Points, FLinearColor(0.88f, 0.94f, 1.0f, 0.9f * Alpha), (Depth == 0 ? 1.8f : 1.1f) * S);
		if (Depth == 0)
		{
			const int32 Forks = Random.RandRange(1, 3);
			for (int32 Fork = 0; Fork < Forks; ++Fork)
			{
				const FVector2f Start = Points[Random.RandRange(2, Segments - 2)];
				const FVector2f End = Start + (Along * Random.FRandRange(0.15f, 0.3f)) + Across * Random.FRandRange(-0.3f, 0.3f) * Length;
				Bolt(Painter, Random, Start, End, S, Alpha * 0.8f, 1);
			}
		}
	}

	/**
	 * The sticky shocker on the pawn: arcs crackling over the visor, from every edge when it hit the head, up from the
	 * bottom when it hit the body, redrawn twenty times a second with a cold flicker of light.
	 */
	void PaintStickyShock(FScctHudPainter& Painter, const FScctHudState& State, float Now)
	{
		const FVector2f Size = Painter.Size();
		const float S = Size.Y / 1080.0f;
		const bool bHead = HasFlag(State, EScctHudEffectFlags::StickyShockHead);
		FRandomStream Random(static_cast<int32>(FMath::FloorToInt(Now * 20.0f)) * 6151 + (bHead ? 3 : 5));
		const float Flicker = Random.FRandRange(0.55f, 1.0f);
		const FLinearColor Cold(0.45f, 0.65f, 1.0f, 1.0f);

		// The cold glow in from the edges the arcs come from.
		const FLinearColor Lit = Fade(Cold, 0.30f * Flicker), Clear = Fade(Cold, 0.0f);
		const float EdgeHeight = Size.Y * (bHead ? 0.18f : 0.35f);
		Painter.Gradient(FBox2f(FVector2f(0.0f, Size.Y - EdgeHeight), Size), Clear, Lit, true);
		if (bHead)
		{
			const float SideWidth = Size.X * 0.14f;
			Painter.Gradient(FBox2f(FVector2f::ZeroVector, FVector2f(Size.X, EdgeHeight)), Lit, Clear, true);
			Painter.Gradient(FBox2f(FVector2f::ZeroVector, FVector2f(SideWidth, Size.Y)), Lit, Clear, false);
			Painter.Gradient(FBox2f(FVector2f(Size.X - SideWidth, 0.0f), Size), Clear, Lit, false);
		}
		if (Random.FRand() < 0.2f)
		{
			Painter.Fill(FBox2f(FVector2f::ZeroVector, Size), Fade(Cold, 0.07f));
		}

		const int32 Bolts = bHead ? Random.RandRange(5, 8) : Random.RandRange(3, 5);
		for (int32 Index = 0; Index < Bolts; ++Index)
		{
			FVector2f From, To;
			if (bHead)
			{
				// From a point on any edge, a good way in towards the middle.
				const int32 Edge = Random.RandRange(0, 3);
				const float Along = Random.FRand();
				From = Edge == 0 ? FVector2f(Along * Size.X, 0.0f) : Edge == 1 ? FVector2f(Size.X, Along * Size.Y)
					: Edge == 2 ? FVector2f(Along * Size.X, Size.Y) : FVector2f(0.0f, Along * Size.Y);
				To = FMath::Lerp(From, Size * 0.5f, Random.FRandRange(0.35f, 0.75f)) + FVector2f(Random.FRandRange(-0.1f, 0.1f) * Size.X, Random.FRandRange(-0.1f, 0.1f) * Size.Y);
			}
			else
			{
				From = FVector2f(Random.FRandRange(0.05f, 0.95f) * Size.X, Size.Y);
				To = FVector2f(From.X + Random.FRandRange(-0.12f, 0.12f) * Size.X, Size.Y * Random.FRandRange(0.55f, 0.8f));
			}
			Bolt(Painter, Random, From, To, S, Flicker);
		}
	}
}

void ScctVisorEffects::ParseCommandLine(FScctVisorState& Visor)
{
	FString Value;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ScctVisorFx="), Value))
	{
		return;
	}
	FString What = Value, Delay;
	if (Value.Split(TEXT("@"), &What, &Delay))
	{
		Visor.ForceDelay = FMath::Max(FCString::Atof(*Delay), 0.0f);
	}
	What = What.ToLower();
	if (What != TEXT("shutdown") && What != TEXT("perturb") && What != TEXT("foam") && What != TEXT("shock") && What != TEXT("wet"))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Visor: -ScctVisorFx=%s is not one of shutdown, perturb, foam, shock, wet"), *Value);
		return;
	}
	Visor.Forced = What;
	Visor.ForcedClock = 0.0f;
	UE_LOG(LogOpenSCCT, Log, TEXT("Visor: %s will be faked %.1f s after the HUD opens"), *What, Visor.ForceDelay);
}

void ScctVisorEffects::Reset(FScctVisorState& Visor)
{
	const FString Forced = Visor.Forced;
	const float ForceDelay = Visor.ForceDelay;
	Visor = FScctVisorState();
	Visor.Forced = Forced;
	Visor.ForceDelay = ForceDelay;
	Visor.ForcedClock = Forced.IsEmpty() ? -1.0f : 0.0f;
}

namespace
{
	/** -ScctVisorFx: writes the effect into the snapshot as the bridge would set it. Every value written is absolute. */
	void ApplyForced(FScctVisorState& Visor, FScctHudModel& Model, float DeltaSeconds)
	{
		if (Visor.Forced.IsEmpty() || !Model.bHasState)
		{
			return;
		}
		FScctHudState& State = Model.State;
		const float Now = State.TimeSeconds;
		if (Visor.ForcedAt < 0.0f)
		{
			Visor.ForcedClock += DeltaSeconds;
			if (Visor.ForcedClock < Visor.ForceDelay)
			{
				return;
			}
			Visor.ForcedAt = Now;
			Visor.ForcedShutDowns = State.ShutDowns + 1;
			UE_LOG(LogOpenSCCT, Log, TEXT("Visor: forced %s at t=%.2f (side %d)"), *Visor.Forced, Now, State.Side);
		}

		const float Age = Now - Visor.ForcedAt;
		if (Visor.Forced == TEXT("shutdown"))
		{
			// HudShutDown: ShutDowns moves on, the time is kept, bShutDown holds until 12 whole seconds later.
			State.ShutDowns = FMath::Max(State.ShutDowns, Visor.ForcedShutDowns);
			State.ShutDownTime = Visor.ForcedAt;
			if (Now <= FMath::TruncToFloat(Visor.ForcedAt) + ScctVisorEffects::ShutDownSeconds)
			{
				State.EffectFlags |= EScctHudEffectFlags::ShutDown;
			}
		}
		else if (Visor.Forced == TEXT("perturb"))
		{
			State.EffectFlags |= EScctHudEffectFlags::Perturbed;
		}
		else if (Visor.Forced == TEXT("foam"))
		{
			// SetHudProjection(1, 5, 10): the splash, then the foam settles at a whole second and stays 5 s, then
			// takes 10 s to clean.
			const int32 StopTime = static_cast<int32>(Visor.ForcedAt + ForcedSplashSeconds);
			if (Age < ForcedSplashSeconds)
			{
				State.EffectFlags |= EScctHudEffectFlags::FoamSplash;
			}
			else if (Now < StopTime + ForcedFoamStay + 1.0f + ScctVisorEffects::FoamCleanSeconds)
			{
				State.EffectFlags |= EScctHudEffectFlags::FoamOn;
				State.FoamStopTime = StopTime;
				State.FoamStaySeconds = ForcedFoamStay;
			}
		}
		else if (Visor.Forced == TEXT("shock"))
		{
			// The head for 5 s, the body for 5 s, and so on.
			State.EffectFlags |= EScctHudEffectFlags::StickyShocking;
			if (FMath::Fmod(Age, 10.0f) < 5.0f)
			{
				State.EffectFlags |= EScctHudEffectFlags::StickyShockHead;
			}
		}
		else if (Visor.Forced == TEXT("wet") && Age < ForcedWetSeconds)
		{
			State.EffectFlags |= EScctHudEffectFlags::Wet;
		}
	}
}

void ScctVisorEffects::Update(FScctVisorState& Visor, FScctHudModel& Model, float DeltaSeconds)
{
	ApplyForced(Visor, Model, DeltaSeconds);
	if (!Model.bHasState)
	{
		return;
	}
	const FScctHudState& State = Model.State;
	const float Now = State.TimeSeconds;

	// The shutdown (MercHudPage.DriveShutDown): a new count starts it at the whole second; 12 s later the reboot runs
	// for 2 s. The first count seen is only remembered, so opening the HUD does not shut it down.
	if (Visor.ShutDowns == INDEX_NONE)
	{
		Visor.ShutDowns = State.ShutDowns;
	}
	else if (State.ShutDowns != Visor.ShutDowns)
	{
		Visor.ShutDowns = State.ShutDowns;
		if (State.Side == 1)
		{
			Visor.bLatch = true;
			Visor.bRebooting = false;
			Visor.T0 = FMath::TruncToFloat(Now);
			Visor.StartedAt = Now;
			UE_LOG(LogOpenSCCT, Log, TEXT("Visor: shutdown start at t=%.2f (count %d, bShutDown %d)"), Now, State.ShutDowns,
				HasFlag(State, EScctHudEffectFlags::ShutDown) ? 1 : 0);
		}
	}
	if (Visor.bLatch)
	{
		if (!Visor.bRebooting)
		{
			if (Visor.T0 + ShutDownSeconds < Now)
			{
				Visor.bRebooting = true;
				Visor.T1 = FMath::TruncToFloat(Now);
				Visor.RebootAt = Now;
				UE_LOG(LogOpenSCCT, Log, TEXT("Visor: shutdown reboot at t=%.2f, whole seconds %.0f -> %.0f (%.0f s; %.2f s after the hit), bShutDown %d"), Now, Visor.T0, Visor.T1,
					Visor.T1 - Visor.T0, Now - Visor.StartedAt, HasFlag(State, EScctHudEffectFlags::ShutDown) ? 1 : 0);
			}
		}
		else if (Visor.T1 + RebootSeconds < Now)
		{
			Visor.bLatch = false;
			Visor.bRebooting = false;
			UE_LOG(LogOpenSCCT, Log, TEXT("Visor: shutdown end at t=%.2f, %.2f s after the reboot; the HUD is back"), Now, Now - Visor.RebootAt);
		}
	}

	// The foam: every splash gets new clumps. Foam that is found already lying (the HUD opened on it) gets some too.
	const bool bSplash = HasFlag(State, EScctHudEffectFlags::FoamSplash);
	const bool bFoamOn = HasFlag(State, EScctHudEffectFlags::FoamOn);
	if (bSplash && !Visor.bFoamSplash)
	{
		Visor.FoamSeed = GetTypeHash(Now) | 1u;
		Visor.FoamSplashAt = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: foam splash at t=%.2f"), Now);
	}
	else if (bFoamOn && Visor.FoamSeed == 0)
	{
		Visor.FoamSeed = GetTypeHash(Now) | 1u;
		Visor.FoamSplashAt = Now - 1.0f;
	}
	if (!bSplash && !bFoamOn && Visor.FoamSeed != 0)
	{
		Visor.FoamSeed = 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: foam cleared at t=%.2f, %.2f s after the splash"), Now, Now - Visor.FoamSplashAt);
	}
	else if (bFoamOn && !bSplash && Visor.bFoamSplash)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: foam settled at t=%.2f, stays until %d then cleans for %.0f s"), Now, State.FoamStopTime + State.FoamStaySeconds, FoamCleanSeconds);
	}
	Visor.bFoamSplash = bSplash;

	// The rain.
	const bool bWet = HasFlag(State, EScctHudEffectFlags::Wet);
	if (bWet && !Visor.bWet)
	{
		Visor.WetSince = Now;
		Visor.DryAt = -1.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: droplets from t=%.2f"), Now);
	}
	else if (!bWet && Visor.bWet)
	{
		Visor.DryAt = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: out of the rain at t=%.2f, the running droplets finish"), Now);
	}
	Visor.bWet = bWet;

	// The sticky shocker.
	const bool bShocked = HasFlag(State, EScctHudEffectFlags::StickyShocking);
	if (bShocked != Visor.bShocked)
	{
		Visor.ShockSince = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("Visor: sticky shock %s at t=%.2f%s"), bShocked ? TEXT("on") : TEXT("off"), Now,
			bShocked && HasFlag(State, EScctHudEffectFlags::StickyShockHead) ? TEXT(" (head)") : TEXT(""));
	}
	Visor.bShocked = bShocked;
}

bool ScctVisorEffects::IsHudShutDown(const FScctHudModel& Model)
{
	return Model.Visor != nullptr && Model.Visor->bLatch && !Model.Visor->bRebooting && IsMercView(Model);
}

void ScctVisorEffects::Paint(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (Model.Visor == nullptr || !IsMercView(Model))
	{
		return;
	}
	const FScctVisorState& Visor = *Model.Visor;
	const FScctHudState& State = Model.State;
	const float Now = State.TimeSeconds;

	// As the merc update runs its handlers: the shutdown first; while it runs neither foam nor perturbation is driven,
	// and foam on the visor hides the perturbation.
	if (Visor.bLatch)
	{
		PaintShutDown(Painter, Visor, Now);
	}
	else if (!PaintFoam(Painter, Model, Visor) && HasFlag(State, EScctHudEffectFlags::Perturbed))
	{
		PaintStatic(Painter, Now, 1.0f);
	}

	PaintDroplets(Painter, Visor, Now);
	if (HasFlag(State, EScctHudEffectFlags::StickyShocking))
	{
		PaintStickyShock(Painter, State, Now);
	}
}
