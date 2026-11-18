#pragma once

#include "CoreMinimal.h"

// The first-person arms in the map outfit: the bridge redraws the arms material of a view model from the dressed
// body (the outfit model, or the stock mesh with the outfit body texture) and hands the PNG it made. Mirrors
// Bridge/OpenScct.UnrealBridge/Pose/OutfitArms/OutfitArmsExports.cs; change both together.

constexpr int32 ScctOutfitArmsApiVersion = 1;

struct FScctOutfitArmsApi
{
	int32 Version;

	/**
	 * Writes the full path of the arms texture of a view model (its package folder and mesh, as "SWeaponFirst" and
	 * "p190") for a team (0 spies, 1 mercs) as the map dresses it. Returns its length, 0 when the arms keep their
	 * look. The first call for a pair of meshes makes the texture.
	 */
	int32 (__cdecl* ArmsTexture)(int32 Kind, const TCHAR* Package, int32 PackageLength, const TCHAR* Mesh, int32 MeshLength, TCHAR* OutPath, int32 Capacity);
};
