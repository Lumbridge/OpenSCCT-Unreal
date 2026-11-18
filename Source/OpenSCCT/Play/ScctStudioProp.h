#pragma once

#include "CoreMinimal.h"

struct FScctWorldAnimated;
class USkeletalMeshComponent;
class UScctLevelStateSubsystem;

namespace ScctStudioProp
{
	bool Apply(USkeletalMeshComponent& Mesh, const FScctWorldAnimated& Record, float OriginalDuration, UScctLevelStateSubsystem& Level);
	void Restore(USkeletalMeshComponent& Mesh);
}
