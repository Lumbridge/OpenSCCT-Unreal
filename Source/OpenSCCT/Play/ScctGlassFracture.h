#pragma once

#include "CoreMinimal.h"

/**
 * Deterministic, impact-centred Voronoi cells clipped to a pane's actual triangles (holes included): the Godot
 * client's GlassFracture (game/Scripts/Render/GlassFracture.cs), ported line for line in single precision so the
 * same triangles, impact and seed give the same pieces. Points are in the pane's own 2D plane.
 */
namespace ScctGlassFracture
{
	struct FVertex
	{
		FVector2f Point = FVector2f::ZeroVector;
		FVector2f Uv = FVector2f::ZeroVector;
	};

	struct FTriangle
	{
		FVertex A, B, C;
	};

	struct FFragment
	{
		/** The area-weighted middle of the fragment. */
		FVector2f Center = FVector2f::ZeroVector;
		/** Convex pieces, one per pane triangle the cell covers. */
		TArray<TArray<FVertex>> Polygons;
	};

	/** Count cells (12 to 80), the first at the impact, a third of them chips around it, the rest across the pane. */
	OPENSCCT_API TArray<FFragment> Break(const TArray<FTriangle>& Triangles, FVector2f Impact, uint32 Seed, int32 Count = 56);

	OPENSCCT_API float Area(const TArray<FVertex>& Polygon);

	/** The fragment's outline: its edges that are not shared by two of its pieces, so a shard has no inner walls. */
	OPENSCCT_API TArray<TPair<FVertex, FVertex>> Boundary(const FFragment& Fragment);
}
