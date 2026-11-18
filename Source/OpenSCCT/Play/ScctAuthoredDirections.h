#pragma once

#include "CoreMinimal.h"

class FJsonValue;
struct FMeshDescription;
namespace GLTF { struct FAccessor; class FMeshFactory; }

namespace ScctAuthored
{
	// Editor v1 exports use UV0 for the material and UV1 for the lightmap. The direction occupies UV2/3
	// only on marked, vertex-lit map components. These components do not enable the pawn light rig.
	constexpr int32 DirectionFlagSlot = 28;

	/** Uses GLTFCore's actual per-primitive source-index mapping, including for split device sections. */
	bool CopyDirections(const GLTF::FMeshFactory& Factory, TConstArrayView<GLTF::FAccessor> Accessors,
		TConstArrayView<TSharedPtr<FJsonValue>> Primitives, FMeshDescription& Description);
}
