#pragma once

#include "CoreMinimal.h"

struct FScctMenuMapInfo;

struct FScctMapVariant
{
	int32 MapIndex;
	FString Label;
};

struct FScctMapFamily
{
	FString Title;
	TArray<FScctMapVariant> Variants;
};

namespace ScctMapFamilies
{
	/** Families in title order. MapIndex always refers to the original playable map list. */
	TArray<FScctMapFamily> Build(const TArray<FScctMenuMapInfo>& Maps);
}
