#pragma once

#include "CoreMinimal.h"

// Mirrors Particles/MeshParticleExports.cs. IDs use the session's world name table. Mesh names are the
// original library node paths. A negative DrawStyle selects the mesh's own section materials.
struct FScctMeshParticleLayer
{
	int32 OwnerId, MeshId, Capacity, DrawStyle, TextureId, Unlit;
	float AlphaRef;
};
static_assert(sizeof(FScctMeshParticleLayer) == 28);

struct FScctMeshParticle
{
	int32 Layer;
	float X, Y, Z, Pitch, Yaw, Roll, ScaleX, ScaleY, ScaleZ;
	uint8 R, G, B, A;
};
static_assert(sizeof(FScctMeshParticle) == 44);

struct FScctMeshParticleApi
{
	int32 Version;
	int32 (__cdecl* Layers)(FScctMeshParticleLayer* Out, int32 Capacity);
	void (__cdecl* Trigger)(int32 OwnerId);
	void (__cdecl* Place)(int32 OwnerId, float X, float Y, float Z, float Pitch, float Yaw, float Roll);
	int32 (__cdecl* Frame)(FScctMeshParticle* Out, int32 Capacity);
};
