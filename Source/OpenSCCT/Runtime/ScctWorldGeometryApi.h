#pragma once

#include "CoreMinimal.h"

// WorldGeometryExports.cs. Rotations use the simulation's integer units (65536 per turn).
struct FScctGeometryRow
{
	int32 Kind, Owner, Asset, Material, Picture, Part, First, Count;
	float X, Y, Z, ScaleX, ScaleY, ScaleZ;
	int32 Pitch, Yaw, Roll;
	float Phase;
	int32 Spin;
};

struct FScctGeometryVertex
{
	float X, Y, Z, Nx, Ny, Nz, U, V;
};

static_assert(sizeof(FScctGeometryRow) == 76 && sizeof(FScctGeometryVertex) == 32);

struct FScctWorldGeometryApi
{
	int32 Version;
	int32 (__cdecl* Prepare)();
	int32 (__cdecl* Rows)(FScctGeometryRow*, int32);
	int32 (__cdecl* Vertices)(int32, FScctGeometryVertex*, int32);
};
