#pragma once

#include "CoreMinimal.h"

namespace ScctMercLaserMount
{
	/** The model's visible barrel opening in centimetres relative to its FX bone in the bind pose. */
	FVector BoreOffset(const FString& ModelName);
}
