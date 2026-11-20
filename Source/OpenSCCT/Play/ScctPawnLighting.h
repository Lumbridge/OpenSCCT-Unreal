#pragma once

#include "CoreMinimal.h"

class UMeshComponent;

/** The ordinary lamp term for one pawn. The body, its attachments and its view model share one visibility sample. */
struct FScctPawnLighting
{
	static FScctPawnLighting Sample(const UWorld& World, FVector Centre, float Radius, int32 SpyActorId = INDEX_NONE, bool bSampleViewBounce = false);
	void UseViewAmbient();
	void Apply(UMeshComponent& Mesh, bool bReceiveBodyAid = false, bool bBody = false) const;

private:
	// Material slots 0..2 belong to local detail, vertex light and near fade. pawn_light in scct_materials.py
	// reads slot 3 as the rig switch, followed by four pairs of float4 (light place, colour).
	// Slot 35, the last colour's spare alpha, is set separately on the body to keep equipment's own finish.
	float Data[32] = {};
	FVector3f ViewAmbient = FVector3f::ZeroVector;
	float SpySurfaceLight = 0.0f;
	bool bLamps = false;
};
