#include "Play/ScctGlassFracture.h"

namespace ScctGlassFracture
{
	namespace
	{
		float GlassCross2(const FVector2f& A, const FVector2f& B)
		{
			return A.X * B.Y - A.Y * B.X;
		}

		FVector2f GlassClamp2(const FVector2f& P, const FVector2f& Min, const FVector2f& Max)
		{
			return FVector2f(FMath::Clamp(P.X, Min.X, Max.X), FMath::Clamp(P.Y, Min.Y, Max.Y));
		}

		/** Keeps the part of the convex polygon where dot(p, Normal) <= Limit (Sutherland-Hodgman against one half-plane). */
		TArray<FVertex> GlassClip(const TArray<FVertex>& Polygon, const FVector2f& Normal, float Limit)
		{
			TArray<FVertex> Clipped;
			Clipped.Reserve(Polygon.Num() + 1);
			FVertex A = Polygon.Last();
			float Da = FVector2f::DotProduct(A.Point, Normal) - Limit;
			for (const FVertex& B : Polygon)
			{
				const float Db = FVector2f::DotProduct(B.Point, Normal) - Limit;
				if ((Da <= 0.0f) != (Db <= 0.0f))
				{
					const float T = Da / (Da - Db);
					FVertex Cut;
					Cut.Point = A.Point + (B.Point - A.Point) * T;
					Cut.Uv = A.Uv + (B.Uv - A.Uv) * T;
					Clipped.Add(Cut);
				}
				if (Db <= 0.0f)
				{
					Clipped.Add(B);
				}
				A = B;
				Da = Db;
			}
			return Clipped;
		}
	}

	TArray<FFragment> Break(const TArray<FTriangle>& Triangles, FVector2f Impact, uint32 Seed, int32 Count)
	{
		TArray<FFragment> Result;
		if (Triangles.Num() == 0)
		{
			return Result;
		}
		FVector2f Min(TNumericLimits<float>::Max());
		FVector2f Max(-TNumericLimits<float>::Max());
		for (const FTriangle& T : Triangles)
		{
			for (const FVertex* V : { &T.A, &T.B, &T.C })
			{
				Min = FVector2f::Min(Min, V->Point);
				Max = FVector2f::Max(Max, V->Point);
			}
		}
		const FVector2f Size = Max - Min;
		if (Size.X < 1e-5f || Size.Y < 1e-5f)
		{
			return Result;
		}
		Impact = GlassClamp2(Impact, Min, Max);
		Seed = Seed == 0 ? 1u : Seed;
		auto Next = [&Seed]()
		{
			Seed ^= Seed << 13;
			Seed ^= Seed >> 17;
			Seed ^= Seed << 5;
			return static_cast<float>(Seed & 0xffffffu) / 16777216.0f;
		};
		Count = FMath::Clamp(Count, 12, 80);
		TArray<FVector2f> Sites;
		Sites.Reserve(Count);
		Sites.Add(Impact);
		for (int32 I = 1; I < Count; ++I)
		{
			// Small chips at the impact, larger irregular pieces across the rest of the pane.
			const float Angle = Next() * UE_TWO_PI;
			const float Radius = FMath::Min(Size.X, Size.Y) * (0.035f + Next() * 0.18f);
			if (I < Count / 3)
			{
				Sites.Add(GlassClamp2(Impact + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius, Min, Max));
			}
			else
			{
				const float X = Next();
				const float Y = Next();
				Sites.Add(Min + FVector2f(X * Size.X, Y * Size.Y));
			}
		}

		for (int32 I = 0; I < Sites.Num(); ++I)
		{
			FFragment Fragment;
			FVector2f Centroid = FVector2f::ZeroVector;
			float TotalArea = 0.0f;
			for (const FTriangle& T : Triangles)
			{
				TArray<FVertex> Polygon = { T.A, T.B, T.C };
				for (int32 J = 0; J < Sites.Num() && Polygon.Num() >= 3; ++J)
				{
					if (I == J)
					{
						continue;
					}
					const FVector2f Normal = Sites[J] - Sites[I];
					if (Normal.SizeSquared() < 1e-12f)
					{
						// Two sites in one place: the earlier one keeps the cell.
						if (J < I)
						{
							Polygon.Reset();
						}
						continue;
					}
					const float Limit = (Sites[J].SizeSquared() - Sites[I].SizeSquared()) * 0.5f;
					Polygon = GlassClip(Polygon, Normal, Limit);
				}
				if (Polygon.Num() < 3)
				{
					continue;
				}
				if (Area(Polygon) < 1e-10f)
				{
					continue;
				}
				// Area-weighted triangle centroids, valid for each convex clipped polygon.
				for (int32 K = 1; K + 1 < Polygon.Num(); ++K)
				{
					const float Ta = FMath::Abs(GlassCross2(Polygon[K].Point - Polygon[0].Point, Polygon[K + 1].Point - Polygon[0].Point)) * 0.5f;
					Centroid += (Polygon[0].Point + Polygon[K].Point + Polygon[K + 1].Point) * (Ta / 3.0f);
					TotalArea += Ta;
				}
				Fragment.Polygons.Add(MoveTemp(Polygon));
			}
			if (TotalArea > 1e-9f)
			{
				Fragment.Center = Centroid / TotalArea;
				Result.Add(MoveTemp(Fragment));
			}
		}
		return Result;
	}

	float Area(const TArray<FVertex>& Polygon)
	{
		float A = 0.0f;
		for (int32 I = 1; I + 1 < Polygon.Num(); ++I)
		{
			A += FMath::Abs(GlassCross2(Polygon[I].Point - Polygon[0].Point, Polygon[I + 1].Point - Polygon[0].Point)) * 0.5f;
		}
		return A;
	}

	TArray<TPair<FVertex, FVertex>> Boundary(const FFragment& Fragment)
	{
		struct FEdge
		{
			FVertex A, B;
			int32 Count = 0;
		};
		auto Key = [](const FVector2f& P)
		{
			return TPair<int64, int64>(FMath::RoundToInt64(static_cast<double>(P.X) * 100000.0), FMath::RoundToInt64(static_cast<double>(P.Y) * 100000.0));
		};
		auto Less = [](const TPair<int64, int64>& L, const TPair<int64, int64>& R)
		{
			return L.Key < R.Key || (L.Key == R.Key && L.Value < R.Value);
		};
		TMap<TPair<TPair<int64, int64>, TPair<int64, int64>>, FEdge> Edges;
		TArray<TPair<TPair<int64, int64>, TPair<int64, int64>>> Order;
		for (const TArray<FVertex>& Polygon : Fragment.Polygons)
		{
			for (int32 I = 0; I < Polygon.Num(); ++I)
			{
				const FVertex& A = Polygon[I];
				const FVertex& B = Polygon[(I + 1) % Polygon.Num()];
				const TPair<int64, int64> Ak = Key(A.Point);
				const TPair<int64, int64> Bk = Key(B.Point);
				if (Ak == Bk)
				{
					continue;
				}
				const TPair<TPair<int64, int64>, TPair<int64, int64>> EdgeKey = Less(Ak, Bk) ? MakeTuple(Ak, Bk) : MakeTuple(Bk, Ak);
				if (FEdge* Found = Edges.Find(EdgeKey))
				{
					++Found->Count;
				}
				else
				{
					Edges.Add(EdgeKey, FEdge{ A, B, 1 });
					Order.Add(EdgeKey);
				}
			}
		}
		TArray<TPair<FVertex, FVertex>> Result;
		for (const TPair<TPair<int64, int64>, TPair<int64, int64>>& EdgeKey : Order)
		{
			const FEdge& Edge = Edges[EdgeKey];
			if ((Edge.Count & 1) != 0)
			{
				Result.Add(TPair<FVertex, FVertex>(Edge.A, Edge.B));
			}
		}
		return Result;
	}
}
