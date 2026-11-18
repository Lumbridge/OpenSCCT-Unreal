#include "Play/ScctLightPlan.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"

namespace
{
	constexpr int32 DefaultLightPlan = 1;
	TAutoConsoleVariable<int32> CVarLightPlan(TEXT("scct.Light.Plan"), DefaultLightPlan,
		TEXT("1 uses fixed exposure and the map's baked lighting (default). 0 uses the legacy lighting. ")
		TEXT("Changing this setting takes full effect on the next map load."));

	int32 LightPlanLatched = DefaultLightPlan;
	bool bLightPlanStarted = false;

	void LatchLightPlan(UWorld* World, const UWorld::InitializationValues)
	{
		if (World == nullptr || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
		{
			return;
		}
		const int32 Plan = FMath::Max(CVarLightPlan.GetValueOnGameThread(), 0);
		if (Plan != LightPlanLatched || Plan != 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("LightPlan: %s loads with scct.Light.Plan %d (was %d)."), *World->GetName(), Plan, LightPlanLatched);
		}
		LightPlanLatched = Plan;
	}
}

void FScctLightPlan::Start()
{
	if (bLightPlanStarted)
	{
		return;
	}
	bLightPlanStarted = true;
	int32 Plan = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctLightPlan="), Plan))
	{
		CVarLightPlan->Set(FMath::Max(Plan, 0), ECVF_SetByCommandline);
	}
	LightPlanLatched = FMath::Max(CVarLightPlan.GetValueOnAnyThread(), 0);
	FWorldDelegates::OnPreWorldInitialization.AddStatic(&LatchLightPlan);
}

int32 FScctLightPlan::Requested()
{
	return FMath::Max(CVarLightPlan.GetValueOnAnyThread(), 0);
}

int32 FScctLightPlan::Active()
{
	return LightPlanLatched;
}
