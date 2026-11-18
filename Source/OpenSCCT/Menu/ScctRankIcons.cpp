#include "Menu/ScctRankIcons.h"

#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "TextureResource.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

// The shapes are RankIcons' SVG, in its 64 x 64 box, filled by a small rasteriser of this file's own: a shape is a
// run of points (a polygon, a circle as a many-sided one, a flattened curve), filled, stroked or both, and the shapes
// are laid over each other in order as an SVG's are. Joins and ends of a stroke are round.
namespace
{
	struct FRankShape
	{
		TArray<FVector2f> Points;
		bool bClosed = true;
		bool bFill = false;
		FLinearColor Fill = FLinearColor::Black;
		float StrokeWidth = 0.0f;
		FLinearColor Stroke = FLinearColor::Black;
		float Opacity = 1.0f;
		FBox2f Bounds = FBox2f(ForceInit);
	};

	/** 0xRRGGBB as the colour's own (sRGB) numbers, 0 to 1: the shapes are mixed as an SVG mixes them. */
	FLinearColor RankHex(int32 Rgb)
	{
		return FLinearColor(((Rgb >> 16) & 255) / 255.0f, ((Rgb >> 8) & 255) / 255.0f, (Rgb & 255) / 255.0f, 1.0f);
	}

	const int32 RankTierColours[] = { 0x7d8f81, 0x93b38c, 0xa6d38c, 0xbfe07c, 0xdbe46b, 0xecd25a, 0xf0b24a, 0xef8f45, 0xe96a4a, 0xdc5072, 0xc45ae0 };
	const int32 RankPrestigeColours[] = { 0xffffff, 0xc8463a, 0xe6862b, 0xf1c40f, 0x7fd34a, 0x1fc4a0, 0x3a9be0, 0x6c63f0, 0xb356e6, 0xeef3f4, 0xffcf3a };
	constexpr int32 RankInk = 0x0a140e;

	TArray<FVector2f> RankPolygon(int32 Sides, float Radius, float TurnDegrees, const FVector2f& Centre = FVector2f(32.0f, 32.0f))
	{
		TArray<FVector2f> Points;
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(TurnDegrees + 360.0f * Index / Sides - 90.0f);
			Points.Add(Centre + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}

	TArray<FVector2f> RankStar(int32 Tips, float Outer, float Inner, const FVector2f& Centre = FVector2f(32.0f, 32.0f))
	{
		TArray<FVector2f> Points;
		for (int32 Index = 0; Index < Tips * 2; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(180.0f * Index / Tips - 90.0f);
			Points.Add(Centre + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * (Index % 2 == 0 ? Outer : Inner));
		}
		return Points;
	}

	void RankCubic(TArray<FVector2f>& Points, const FVector2f& A, const FVector2f& B, const FVector2f& C, const FVector2f& D)
	{
		for (int32 Step = 1; Step <= 12; ++Step)
		{
			const float T = Step / 12.0f, U = 1.0f - T;
			Points.Add(A * (U * U * U) + B * (3.0f * U * U * T) + C * (3.0f * U * T * T) + D * (T * T * T));
		}
	}

	struct FRankDrawing
	{
		TArray<FRankShape> Shapes;

		FRankShape& Add(TArray<FVector2f> Points, bool bClosed)
		{
			FRankShape& Shape = Shapes.AddDefaulted_GetRef();
			Shape.Points = MoveTemp(Points);
			Shape.bClosed = bClosed;
			return Shape;
		}

		void Filled(TArray<FVector2f> Points, int32 Fill, int32 Stroke, float Width, float Opacity = 1.0f)
		{
			FRankShape& Shape = Add(MoveTemp(Points), true);
			Shape.bFill = true;
			Shape.Fill = RankHex(Fill);
			Shape.Stroke = RankHex(Stroke);
			Shape.StrokeWidth = Width;
			Shape.Opacity = Opacity;
		}

		void Stroked(TArray<FVector2f> Points, bool bClosed, int32 Stroke, float Width, float Opacity = 1.0f)
		{
			FRankShape& Shape = Add(MoveTemp(Points), bClosed);
			Shape.Stroke = RankHex(Stroke);
			Shape.StrokeWidth = Width;
			Shape.Opacity = Opacity;
		}

		void Disc(const FVector2f& Centre, float Radius, int32 Fill, int32 Stroke, float Width)
		{
			Filled(RankPolygon(48, Radius, 0.0f, Centre), Fill, Stroke, Width);
		}

		void Ring(const FVector2f& Centre, float Radius, int32 Stroke, float Width, float Opacity)
		{
			Stroked(RankPolygon(48, Radius, 0.0f, Centre), true, Stroke, Width, Opacity);
		}

		/** The trifocal goggles at the heart of every prestige emblem. */
		void Goggles(int32 Ring)
		{
			// The strap: a quadratic curve from (17,34) by (32,22) to (47,34).
			TArray<FVector2f> Strap;
			for (int32 Step = 0; Step <= 12; ++Step)
			{
				const float T = Step / 12.0f, U = 1.0f - T;
				Strap.Add(FVector2f(17.0f, 34.0f) * (U * U) + FVector2f(32.0f, 22.0f) * (2.0f * U * T) + FVector2f(47.0f, 34.0f) * (T * T));
			}
			Stroked(MoveTemp(Strap), false, Ring, 2.0f, 0.7f);
			for (const FVector2f& Lens : { FVector2f(24.0f, 33.0f), FVector2f(32.0f, 27.5f), FVector2f(40.0f, 33.0f) })
			{
				Disc(Lens, 4.6f, 0x1d2a20, Ring, 1.4f);
				Disc(Lens, 2.6f, 0x7cff6b, 0x7cff6b, 0.0f);
			}
		}

		void TierShape(int32 Tier, int32 Colour)
		{
			switch (Tier)
			{
			case 0: Disc(FVector2f(32.0f, 32.0f), 27.0f, RankInk, Colour, 3.0f); break;
			case 1: Filled(RankPolygon(4, 29.0f, 45.0f), RankInk, Colour, 3.0f); break;
			case 2: Filled(RankPolygon(4, 30.0f, 0.0f), RankInk, Colour, 3.0f); break;
			case 3:
			{
				// The shield: M32 4 L56 12 V30 C56 46 45 56 32 61 C19 56 8 46 8 30 V12 Z
				TArray<FVector2f> Shield = { FVector2f(32.0f, 4.0f), FVector2f(56.0f, 12.0f), FVector2f(56.0f, 30.0f) };
				RankCubic(Shield, FVector2f(56.0f, 30.0f), FVector2f(56.0f, 46.0f), FVector2f(45.0f, 56.0f), FVector2f(32.0f, 61.0f));
				RankCubic(Shield, FVector2f(32.0f, 61.0f), FVector2f(19.0f, 56.0f), FVector2f(8.0f, 46.0f), FVector2f(8.0f, 30.0f));
				Shield.Add(FVector2f(8.0f, 12.0f));
				Filled(MoveTemp(Shield), RankInk, Colour, 3.0f);
				break;
			}
			case 4: Filled(RankPolygon(6, 29.0f, 30.0f), RankInk, Colour, 3.0f); break;
			case 5: Filled(RankPolygon(8, 29.0f, 22.5f), RankInk, Colour, 3.0f); break;
			case 6: Filled(RankStar(5, 31.0f, 21.0f), RankInk, Colour, 2.5f); break;
			case 7: Filled(RankStar(6, 30.0f, 22.0f), RankInk, Colour, 2.5f); break;
			case 8: Filled(RankStar(8, 30.0f, 24.0f), RankInk, Colour, 2.5f); break;
			case 9: Filled(RankStar(10, 30.0f, 25.0f), RankInk, Colour, 2.5f); break;
			default:
				Filled(RankStar(12, 31.0f, 26.0f), RankInk, Colour, 2.5f);
				Ring(FVector2f(32.0f, 32.0f), 22.0f, Colour, 1.2f, 0.6f);
				break;
			}
		}

		void Level(int32 InLevel)
		{
			const int32 Tier = FMath::Clamp((InLevel - 1) / 5, 0, static_cast<int32>(UE_ARRAY_COUNT(RankTierColours)) - 1);
			const int32 Chevrons = (InLevel - 1) % 5 + 1;
			const int32 Colour = RankTierColours[Tier];
			TierShape(Tier, Colour);
			const float Top = 32.0f - (7.0f + (Chevrons - 1) * 6.0f) * 0.5f;
			for (int32 Index = 0; Index < Chevrons; ++Index)
			{
				const float Y = Top + Index * 6.0f;
				Stroked({ FVector2f(21.0f, Y + 7.0f), FVector2f(32.0f, Y), FVector2f(43.0f, Y + 7.0f) }, false, Colour, 3.2f);
			}
		}

		void Prestige(int32 InPrestige)
		{
			const int32 Colour = RankPrestigeColours[InPrestige];
			Filled(RankStar(4 + InPrestige, 31.0f, 23.0f), Colour, Colour, 1.5f);
			Disc(FVector2f(32.0f, 32.0f), 20.0f, RankInk, Colour, 2.0f);
			if (InPrestige >= 5)
			{
				Ring(FVector2f(32.0f, 32.0f), 16.5f, Colour, 1.0f, 0.55f);
			}
			Goggles(Colour);
			// Small pips under the goggles count the prestige within its five (1-5, 6-10).
			const int32 Pips = (InPrestige - 1) % 5 + 1;
			for (int32 Index = 0; Index < Pips; ++Index)
			{
				Disc(FVector2f(32.0f + (Index - (Pips - 1) * 0.5f) * 5.0f, 44.0f), 1.6f, Colour, Colour, 0.0f);
			}
		}

		void Master()
		{
			constexpr int32 Gold = 0xffcf3a, Light = 0xfff3c4;
			Filled(RankStar(16, 31.5f, 26.0f), Gold, Light, 1.2f);
			Disc(FVector2f(32.0f, 32.0f), 23.0f, RankInk, Light, 2.0f);
			Ring(FVector2f(32.0f, 32.0f), 19.5f, Gold, 1.4f, 1.0f);
			Goggles(Gold);
			Filled(RankStar(5, 4.5f, 2.0f, FVector2f(32.0f, 46.0f)), Light, Light, 0.5f);
		}
	};

	bool RankInside(const TArray<FVector2f>& Points, const FVector2f& P)
	{
		bool bInside = false;
		for (int32 Index = 0, Last = Points.Num() - 1; Index < Points.Num(); Last = Index++)
		{
			const FVector2f& A = Points[Index];
			const FVector2f& B = Points[Last];
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	bool RankNearEdge(const FRankShape& Shape, const FVector2f& P, float Reach)
	{
		const float ReachSquared = Reach * Reach;
		const int32 Count = Shape.Points.Num();
		for (int32 Index = Shape.bClosed ? 0 : 1; Index < Count; ++Index)
		{
			const FVector2f& A = Shape.Points[(Index + Count - 1) % Count];
			const FVector2f& B = Shape.Points[Index];
			const FVector2f Along = B - A;
			const float Length = Along.SizeSquared();
			const float T = Length > 0.0f ? FMath::Clamp(FVector2f::DotProduct(P - A, Along) / Length, 0.0f, 1.0f) : 0.0f;
			if ((A + Along * T - P).SizeSquared() <= ReachSquared)
			{
				return true;
			}
		}
		return false;
	}

	/** The drawing as Pixels x Pixels BGRA texels, straight alpha, the colours as drawn (sRGB). */
	TArray<FColor> RankRaster(FRankDrawing& Drawing, int32 Pixels)
	{
		for (FRankShape& Shape : Drawing.Shapes)
		{
			Shape.Bounds = FBox2f(ForceInit);
			for (const FVector2f& Point : Shape.Points)
			{
				Shape.Bounds += Point;
			}
			Shape.Bounds = Shape.Bounds.ExpandBy(Shape.StrokeWidth * 0.5f + 0.01f);
		}

		constexpr int32 Samples = 4;
		TArray<FColor> Texels;
		Texels.SetNumZeroed(Pixels * Pixels);
		const float Unit = 64.0f / Pixels;
		for (int32 Y = 0; Y < Pixels; ++Y)
		{
			for (int32 X = 0; X < Pixels; ++X)
			{
				// Premultiplied, summed over the samples.
				float R = 0.0f, G = 0.0f, B = 0.0f, A = 0.0f;
				for (int32 Sample = 0; Sample < Samples * Samples; ++Sample)
				{
					const FVector2f P((X + (Sample % Samples + 0.5f) / Samples) * Unit, (Y + (Sample / Samples + 0.5f) / Samples) * Unit);
					float Sr = 0.0f, Sg = 0.0f, Sb = 0.0f, Sa = 0.0f;
					const auto Over = [&](const FLinearColor& Colour, float Opacity)
					{
						Sr = Colour.R * Opacity + Sr * (1.0f - Opacity);
						Sg = Colour.G * Opacity + Sg * (1.0f - Opacity);
						Sb = Colour.B * Opacity + Sb * (1.0f - Opacity);
						Sa = Opacity + Sa * (1.0f - Opacity);
					};
					for (const FRankShape& Shape : Drawing.Shapes)
					{
						if (!Shape.Bounds.IsInside(P))
						{
							continue;
						}
						if (Shape.bFill && RankInside(Shape.Points, P))
						{
							Over(Shape.Fill, Shape.Opacity);
						}
						if (Shape.StrokeWidth > 0.0f && RankNearEdge(Shape, P, Shape.StrokeWidth * 0.5f))
						{
							Over(Shape.Stroke, Shape.Opacity);
						}
					}
					R += Sr; G += Sg; B += Sb; A += Sa;
				}
				if (A > 0.0f)
				{
					const auto Byte = [](float Value) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255)); };
					Texels[Y * Pixels + X] = FColor(Byte(R / A), Byte(G / A), Byte(B / A), Byte(A / (Samples * Samples)));
				}
			}
		}
		return Texels;
	}

	struct FRankPicture
	{
		FSlateBrush Brush;
	};

	/** Kind 0 a level, 1 a prestige, 2 the master emblem. */
	const FSlateBrush* RankCached(int32 Kind, int32 Index, float Size)
	{
		// Twice the slate size in texels, so the picture stays sharp on a scaled-up screen and is never shrunk by
		// more than the filter handles.
		const int32 Units = FMath::Clamp(FMath::RoundToInt(Size), 8, 128);
		const int32 Key = (Kind << 24) | (Index << 12) | Units;
		static TMap<int32, TUniquePtr<FRankPicture>> Pictures;
		if (const TUniquePtr<FRankPicture>* Found = Pictures.Find(Key))
		{
			return &(*Found)->Brush;
		}

		FRankDrawing Drawing;
		if (Kind == 0) { Drawing.Level(Index); }
		else if (Kind == 1) { Drawing.Prestige(Index); }
		else { Drawing.Master(); }
		const int32 Pixels = Units * 2;
		const TArray<FColor> Texels = RankRaster(Drawing, Pixels);

		UTexture2D* Texture = UTexture2D::CreateTransient(Pixels, Pixels, PF_B8G8R8A8, FName(*FString::Printf(TEXT("ScctRank_%d_%d_%d"), Kind, Index, Pixels)));
		if (Texture == nullptr)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Texels.GetData(), Texels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		Texture->AddToRoot(); // kept for the run: the brush below is handed out as a plain pointer

		TUniquePtr<FRankPicture> Picture = MakeUnique<FRankPicture>();
		Picture->Brush.SetResourceObject(Texture);
		Picture->Brush.ImageSize = FVector2D(Units, Units);
		Picture->Brush.DrawAs = ESlateBrushDrawType::Image;
		Picture->Brush.Tiling = ESlateBrushTileType::NoTile;
		return &Pictures.Add(Key, MoveTemp(Picture))->Brush;
	}
}

const FSlateBrush* ScctRankIcons::For(int32 InPrestige, int32 Level, float Size)
{
	if (Level <= 0)
	{
		return nullptr;
	}
	if (InPrestige >= MaxPrestige && Level >= MaxLevel)
	{
		return Master(Size);
	}
	return InPrestige > 0 ? Prestige(InPrestige, Size) : RankCached(0, FMath::Clamp(Level, 1, MaxLevel), Size);
}

const FSlateBrush* ScctRankIcons::ForPacked(int32 PackedRank, float Size)
{
	// PlayerRank.Unpack: the level in the low byte (none outside 1 to MaxLevel), the prestige in the next.
	const int32 Level = PackedRank & 0xFF;
	return Level <= 0 || Level > MaxLevel ? nullptr : For(FMath::Min((PackedRank >> 8) & 0xFF, MaxPrestige), Level, Size);
}

const FSlateBrush* ScctRankIcons::Prestige(int32 InPrestige, float Size)
{
	return RankCached(1, FMath::Clamp(InPrestige, 1, MaxPrestige), Size);
}

const FSlateBrush* ScctRankIcons::Master(float Size)
{
	return RankCached(2, 0, Size);
}

TSharedRef<SWidget> ScctRankIcons::Icon(const FSlateBrush* Brush, float Size)
{
	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		.Visibility(Brush != nullptr ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
		[
			SNew(SImage).Image(Brush)
		];
}
