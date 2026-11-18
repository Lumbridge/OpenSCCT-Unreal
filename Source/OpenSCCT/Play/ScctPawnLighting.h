#pragma once

#include "CoreMinimal.h"

class UMeshComponent;

/** The ordinary lamp term for one pawn. The body, its attachments and its view model share one visibility sample. */
struct FScctPawnLighting
{
	static FScctPawnLighting Sample(FVector Centre, float Radius);
	void Apply(UMeshComponent& Mesh, bool bReceiveBodyAid = false) const;

private:
	// Material slots 0..2 belong to local detail, vertex light and near fade. pawn_light in scct_materials.py
	// reads slot 3 as the rig switch, followed by four pairs of float4 (light place, colour).
	float Data[33] = {};
	bool bLamps = false;
};
