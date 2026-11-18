#include "Play/ScctLightPlan.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"

namespace
{
	TAutoConsoleVariable<int32> CVarLightPlan(TEXT("scct.Light.Plan"), 0,
		TEXT("Which lighting a level is dressed with when it loads: 0 the released picture, exactly as before the lighting plan (the default); ")
		TEXT("1 the plan's lighting (light and dark as the original has them). Takes effect at the next map load at the latest."));

	int32 LightPlanLatched = 0;
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
