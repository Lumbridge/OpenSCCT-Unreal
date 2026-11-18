#include "Menu/SScctRankIcon.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Runtime/ScctFeatures.h"
#include "Styling/CoreStyle.h"

namespace
{
	FLinearColor RankIconHex(const TCHAR* Hex)
	{
		return FLinearColor(FColor::FromHex(Hex));
	}

	// RankIcons' colours: a tier of five levels each, and one per prestige (index 0 unused).
	const TCHAR* const GRankIconTierColours[] = {
		TEXT("7d8f81"), TEXT("93b38c"), TEXT("a6d38c"), TEXT("bfe07c"), TEXT("dbe46b"), TEXT("ecd25a"), TEXT("f0b24a"), TEXT("ef8f45"), TEXT("e96a4a"), TEXT("dc5072"), TEXT("c45ae0"),
	};
	const TCHAR* const GRankIconPrestigeColours[] = {
		TEXT("ffffff"), TEXT("c8463a"), TEXT("e6862b"), TEXT("f1c40f"), TEXT("7fd34a"), TEXT("1fc4a0"), TEXT("3a9be0"), TEXT("6c63f0"), TEXT("b356e6"), TEXT("eef3f4"), TEXT("ffcf3a"),
	};

	using FRankIconPoints = TArray<FVector2f, TInlineAllocator<48>>;

	FRankIconPoints RankIconPolygon(int32 Sides, float Radius, float TurnDegrees)
	{
		FRankIconPoints Points;
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(TurnDegrees + 360.0f * Index / Sides - 90.0f);
			Points.Add(FVector2f(32.0f + Radius * FMath::Cos(Angle), 32.0f + Radius * FMath::Sin(Angle)));
		}
		return Points;
	}

	FRankIconPoints RankIconStar(int32 PointCount, float Outer, float Inner, FVector2f Centre = FVector2f(32.0f, 32.0f))
	{
		FRankIconPoints Points;
		for (int32 Index = 0; Index < PointCount * 2; ++Index)
		{
			const float Radius = Index % 2 == 0 ? Outer : Inner;
			const float Angle = FMath::DegreesToRadians(180.0f * Index / PointCount - 90.0f);
			Points.Add(Centre + FVector2f(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle)));
		}
		return Points;
	}

	FRankIconPoints RankIconCircle(FVector2f Centre, float Radius)
	{
		FRankIconPoints Points;
		const int32 Segments = Radius > 8.0f ? 40 : 14;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float Angle = 2.0f * PI * Index / Segments;
			Points.Add(Centre + FVector2f(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle)));
		}
		return Points;
	}

	void RankIconCubic(FRankIconPoints& Points, FVector2f A, FVector2f B, FVector2f C, FVector2f D)
	{
		for (int32 Step = 1; Step <= 8; ++Step)
		{
			const float T = Step / 8.0f;
			const float U = 1.0f - T;
			Points.Add(A * (U * U * U) + B * (3.0f * U * U * T) + C * (3.0f * U * T * T) + D * (T * T * T));
		}
	}

	/** Draws shapes given in RankIcons' 64 x 64 box into a square of the widget. */
	struct FRankIconPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Out;
		int32 Layer;
		FVector2f Origin;
		float Scale;
		float Alpha;
		FSlateResourceHandle White;

		FVector2f At(FVector2f Point) const { return Origin + Point * Scale; }

		/** A filled shape, as a fan of triangles from its middle (every shape here is seen whole from its middle). */
		void Fill(const FRankIconPoints& Points, FLinearColor Colour)
		{
			Colour.A *= Alpha;
			const FColor Packed = Colour.ToFColor(true); // sRGB, as Slate packs its own tints
			const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
			FVector2f Middle = FVector2f::ZeroVector;
			for (const FVector2f& Point : Points) { Middle += Point; }
			Middle /= static_cast<float>(FMath::Max(Points.Num(), 1));
			TArray<FSlateVertex> Vertices;
			TArray<SlateIndex> Indices;
			Vertices.Reserve(Points.Num() + 1);
			Vertices.Add(FSlateVertex::Make(Transform, At(Middle), FVector2f(0.5f, 0.5f), Packed));
			for (const FVector2f& Point : Points)
			{
				Vertices.Add(FSlateVertex::Make(Transform, At(Point), FVector2f(0.5f, 0.5f), Packed));
			}
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				Indices.Add(0);
				Indices.Add(static_cast<SlateIndex>(Index + 1));
				Indices.Add(static_cast<SlateIndex>((Index + 1) % Points.Num() + 1));
			}
			FSlateDrawElement::MakeCustomVerts(Out, ++Layer, White, Vertices, Indices, nullptr, 0, 0);
		}

		void Stroke(const FRankIconPoints& Points, bool bClosed, FLinearColor Colour, float Width, float Opacity = 1.0f)
		{
			Colour.A *= Alpha * Opacity;
			TArray<FVector2f> Line;
			Line.Reserve(Points.Num() + 2);
			for (const FVector2f& Point : Points) { Line.Add(At(Point)); }
			if (bClosed && Points.Num() > 2)
			{
				// Round the start again, so the first corner is joined like the others.
				Line.Add(At(Points[0]));
				Line.Add(At(Points[1]));
			}
			FSlateDrawElement::MakeLines(Out, ++Layer, Geometry.ToPaintGeometry(), MoveTemp(Line), ESlateDrawEffect::None, Colour, true,
				FMath::Max(Width * Scale * Geometry.Scale, 1.0f));
		}

		void Shape(const FRankIconPoints& Points, const FLinearColor& FillColour, const FLinearColor& Line, float Width)
		{
			Fill(Points, FillColour);
			Stroke(Points, true, Line, Width);
		}

		/** The trifocal goggles at the heart of every prestige emblem. */
		void Goggles(const FLinearColor& Ring)
		{
			FRankIconPoints Band;
			const FVector2f A(17.0f, 34.0f), B(32.0f, 22.0f), C(47.0f, 34.0f);
			for (int32 Step = 0; Step <= 10; ++Step)
			{
				const float T = Step / 10.0f;
				const float U = 1.0f - T;
				Band.Add(A * (U * U) + B * (2.0f * U * T) + C * (T * T));
			}
			Stroke(Band, false, Ring, 2.0f, 0.7f);
			for (const FVector2f& Lens : { FVector2f(24.0f, 33.0f), FVector2f(32.0f, 27.5f), FVector2f(40.0f, 33.0f) })
			{
				Shape(RankIconCircle(Lens, 4.6f), RankIconHex(TEXT("1d2a20")), Ring, 1.4f);
				Fill(RankIconCircle(Lens, 2.6f), RankIconHex(TEXT("7cff6b")));
			}
		}

		void Level(int32 InLevel)
		{
			const FLinearColor Ink = RankIconHex(TEXT("0a140e"));
			InLevel = FMath::Clamp(InLevel, 1, 55);
			const int32 Tier = FMath::Clamp((InLevel - 1) / 5, 0, static_cast<int32>(UE_ARRAY_COUNT(GRankIconTierColours)) - 1);
			const int32 Chevrons = (InLevel - 1) % 5 + 1;
			const FLinearColor Colour = RankIconHex(GRankIconTierColours[Tier]);
			switch (Tier)
			{
			case 0: Shape(RankIconCircle(FVector2f(32.0f, 32.0f), 27.0f), Ink, Colour, 3.0f); break;
			case 1: Shape(RankIconPolygon(4, 29.0f, 45.0f), Ink, Colour, 3.0f); break;
			case 2: Shape(RankIconPolygon(4, 30.0f, 0.0f), Ink, Colour, 3.0f); break;
			case 3:
			{
				// A shield: M32 4 L56 12 V30 C56 46 45 56 32 61 C19 56 8 46 8 30 V12 Z
				FRankIconPoints Shield;
				Shield.Add(FVector2f(32.0f, 4.0f));
				Shield.Add(FVector2f(56.0f, 12.0f));
				Shield.Add(FVector2f(56.0f, 30.0f));
				RankIconCubic(Shield, FVector2f(56.0f, 30.0f), FVector2f(56.0f, 46.0f), FVector2f(45.0f, 56.0f), FVector2f(32.0f, 61.0f));
				RankIconCubic(Shield, FVector2f(32.0f, 61.0f), FVector2f(19.0f, 56.0f), FVector2f(8.0f, 46.0f), FVector2f(8.0f, 30.0f));
				Shield.Add(FVector2f(8.0f, 12.0f));
				Shape(Shield, Ink, Colour, 3.0f);
				break;
			}
			case 4: Shape(RankIconPolygon(6, 29.0f, 30.0f), Ink, Colour, 3.0f); break;
			case 5: Shape(RankIconPolygon(8, 29.0f, 22.5f), Ink, Colour, 3.0f); break;
			case 6: Shape(RankIconStar(5, 31.0f, 21.0f), Ink, Colour, 2.5f); break;
			case 7: Shape(RankIconStar(6, 30.0f, 22.0f), Ink, Colour, 2.5f); break;
			case 8: Shape(RankIconStar(8, 30.0f, 24.0f), Ink, Colour, 2.5f); break;
			case 9: Shape(RankIconStar(10, 30.0f, 25.0f), Ink, Colour, 2.5f); break;
			default:
				Shape(RankIconStar(12, 31.0f, 26.0f), Ink, Colour, 2.5f);
				Stroke(RankIconCircle(FVector2f(32.0f, 32.0f), 22.0f), true, Colour, 1.2f, 0.6f);
				break;
			}
			const float Height = 7.0f + (Chevrons - 1) * 6.0f;
			const float Top = 32.0f - Height / 2.0f;
			for (int32 Index = 0; Index < Chevrons; ++Index)
			{
				const float Y = Top + Index * 6.0f;
				FRankIconPoints Chevron;
				Chevron.Add(FVector2f(21.0f, Y + 7.0f));
				Chevron.Add(FVector2f(32.0f, Y));
				Chevron.Add(FVector2f(43.0f, Y + 7.0f));
				Stroke(Chevron, false, Colour, 3.2f);
			}
		}

		void Prestige(int32 InPrestige)
		{
			InPrestige = FMath::Clamp(InPrestige, 1, 10);
			const FLinearColor Colour = RankIconHex(GRankIconPrestigeColours[InPrestige]);
			Shape(RankIconStar(4 + InPrestige, 31.0f, 23.0f), Colour, Colour, 1.5f);
			Shape(RankIconCircle(FVector2f(32.0f, 32.0f), 20.0f), RankIconHex(TEXT("0a140e")), Colour, 2.0f);
			if (InPrestige >= 5)
			{
				Stroke(RankIconCircle(FVector2f(32.0f, 32.0f), 16.5f), true, Colour, 1.0f, 0.55f);
			}
			Goggles(Colour);
			// Small pips under the goggles count the prestige within its five (1-5, 6-10).
			const int32 Pips = (InPrestige - 1) % 5 + 1;
			for (int32 Index = 0; Index < Pips; ++Index)
			{
				Fill(RankIconCircle(FVector2f(32.0f + (Index - (Pips - 1) / 2.0f) * 5.0f, 44.0f), 1.6f), Colour);
			}
		}

		void Master()
		{
			const FLinearColor Gold = RankIconHex(TEXT("ffcf3a"));
			const FLinearColor Light = RankIconHex(TEXT("fff3c4"));
			Shape(RankIconStar(16, 31.5f, 26.0f), Gold, Light, 1.2f);
			Shape(RankIconCircle(FVector2f(32.0f, 32.0f), 23.0f), RankIconHex(TEXT("0a140e")), Light, 2.0f);
			Stroke(RankIconCircle(FVector2f(32.0f, 32.0f), 19.5f), true, Gold, 1.4f);
			Goggles(Gold);
			Fill(RankIconStar(5, 4.5f, 2.0f, FVector2f(32.0f, 46.0f)), Light);
		}
	};
}

FScctRankEmblem FScctRankEmblem::OfPacked(int32 Packed)
{
	FScctRankEmblem Emblem;
	const FScctAccountApi* Api = Packed != 0 ? ScctFeature<FScctAccountApi>() : nullptr;
	if (Api != nullptr && Api->DescribeRank != nullptr)
	{
		FScctAccountRank Rank = {};
		TCHAR Text[128];
		Api->DescribeRank(Packed, &Rank, Text, UE_ARRAY_COUNT(Text));
		Emblem.Level = Rank.Level;
		Emblem.Prestige = Rank.Prestige;
		Emblem.bMaster = Rank.Master != 0;
	}
	return Emblem;
}

void SScctRankIcon::Construct(const FArguments& InArgs)
{
	Size = InArgs._Size;
	Fade = InArgs._Fade;
	Rank = InArgs._Rank;
	SetCanTick(false);
}

FVector2D SScctRankIcon::ComputeDesiredSize(float) const
{
	return Rank.Get().IsNone() ? FVector2D::ZeroVector : FVector2D(Size, Size);
}

int32 SScctRankIcon::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FScctRankEmblem Emblem = Rank.Get();
	const FVector2f Local = FVector2f(AllottedGeometry.GetLocalSize());
	const float Side = FMath::Min(Local.X, Local.Y);
	if (Emblem.IsNone() || Side < 2.0f || !FSlateApplication::IsInitialized())
	{
		return LayerId;
	}
	FRankIconPainter Painter{ AllottedGeometry, OutDrawElements, LayerId, (Local - FVector2f(Side, Side)) * 0.5f, Side / 64.0f,
		Fade * InWidgetStyle.GetColorAndOpacityTint().A,
		FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))) };
	if (Emblem.bMaster)
	{
		Painter.Master();
	}
	else if (Emblem.Prestige > 0)
	{
		Painter.Prestige(Emblem.Prestige);
	}
	else
	{
		Painter.Level(Emblem.Level);
	}
	return Painter.Layer;
}
