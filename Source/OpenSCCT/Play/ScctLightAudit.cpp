#include "Play/ScctLightAudit.h"

#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "OpenSCCT.h"
#include "Play/ScctLightPlan.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctLightPlanApi.h"

namespace
{
	TAutoConsoleVariable<int32> CVarLightAudit(TEXT("scct.Light.Audit"), 0,
		TEXT("1: once a second the log says what the simulation's pawn light term gives every living pawn (SCCT_AUDIT lines), ")
		TEXT("with the merc's squint and the own spy's shadow darkness. 0 (the default): nothing."));

	TAutoConsoleVariable<float> CVarLightAuditSeconds(TEXT("scct.Light.AuditSeconds"), 1.0f,
		TEXT("Seconds between two blocks of scct.Light.Audit."));

	TAutoConsoleVariable<int32> CVarLightStickyCamShadow(TEXT("scct.Light.StickyCamShadow"), 0,
		TEXT("1: under scct.Light.Plan 1 the sticky camera's fly-in follows the original's shadow rule (solved twice a tick only while the own spy's shadow is drawn). ")
		TEXT("0 (the default): always twice, as released."));

	FTSTicker::FDelegateHandle LightAuditTicker;
	double LightAuditNext = 0.0;
	double LightAuditFeedNext = 0.0;
	bool bLightAuditFeeding = false;
	TWeakObjectPtr<UWorld> LightAuditDescribed;

	UWorld* LightAuditWorld()
	{
		if (GEngine == nullptr)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World != nullptr && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && World->HasBegunPlay())
			{
				return World;
			}
		}
		return nullptr;
	}

	bool LightAuditTick(float)
	{
		const bool bAudit = CVarLightAudit.GetValueOnGameThread() > 0;
		const bool bFeed = FScctLightPlan::IsOn() && CVarLightStickyCamShadow.GetValueOnGameThread() > 0;
		if (!bAudit && !bFeed && !bLightAuditFeeding)
		{
			// Nothing asked: the bridge's light plan state is not even made.
			return true;
		}
		UWorld* World = LightAuditWorld();
		const FScctLightPlanApi* Api = World != nullptr ? ScctFeature<FScctLightPlanApi>() : nullptr;
		if (Api == nullptr)
		{
			return true;
		}

		const double Now = FPlatformTime::Seconds();
		if (bFeed ? Now >= LightAuditFeedNext : bLightAuditFeeding)
		{
			// A new session starts with the rule off, so it is asked for again every second while wanted.
			LightAuditFeedNext = Now + 1.0;
			bLightAuditFeeding = Api->SetStickyCamShadow(bFeed ? 1 : 0) != 0;
		}
		if (!bAudit || Now < LightAuditNext)
		{
			return true;
		}
		LightAuditNext = Now + FMath::Max(CVarLightAuditSeconds.GetValueOnGameThread(), 0.1f);

		FScctLightLevel Level = {};
		if (Api->GetLevel(&Level) == 0)
		{
			return true;
		}
		const FString Map = World->GetMapName();
		if (LightAuditDescribed.Get() != World)
		{
			LightAuditDescribed = World;
			UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_AUDIT_MAP %s plan=%d %s"), *Map, FScctLightPlan::Active(), *FScctLightAudit::DescribeLevel(Level));
		}

		FVector Eye = FVector::ZeroVector;
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			if (Controller->PlayerCameraManager != nullptr)
			{
				Eye = Controller->PlayerCameraManager->GetCameraLocation();
			}
		}
		FScctLightSquint Squint = {};
		Api->GetSquint(&Squint);
		FScctShadowLighting Shadow = {};
		Api->GetShadowLighting(&Shadow);
		FScctLightAuditRow Rows[32] = {};
		const int32 Capacity = UE_ARRAY_COUNT(Rows);
		const int32 Pawns = FMath::Min(Api->GetAudit(Eye.X, Eye.Y, Eye.Z, Rows, Capacity), Capacity);
		UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_AUDIT %s t=%.1f plan=%d pawns=%d squint=%.3f/%.4f shadow=%s%d dir=%.2f,%.2f,%.2f stickycam=%d/%d"),
			*Map, World->GetTimeSeconds(), FScctLightPlan::Active(), Pawns, Squint.Factor, Squint.Term, Shadow.Valid != 0 ? TEXT("") : TEXT("none:"),
			Shadow.Darkness, Shadow.DirX, Shadow.DirY, Shadow.DirZ, Shadow.Feeds, Shadow.ViewUpdate);
		for (int32 Index = 0; Index < Pawns; ++Index)
		{
			const FScctLightAuditRow& Row = Rows[Index];
			UE_LOG(LogOpenSCCT, Log, TEXT("SCCT_AUDIT_PAWN id=%d kind=%s local=%d zone=%d at=%.0f,%.0f,%.0f level=%.3f facing=%.3f ambient=%.3f spot=%.3f lights=%d occluded=%d"),
				Row.ActorId, Row.Kind != 0 ? TEXT("merc") : TEXT("spy"), Row.Flags & 1, Row.Zone, Row.X, Row.Y, Row.Z, Row.Level, Row.LevelFacing,
				Row.Ambient, Row.Spot, Row.Lights, Row.Occluded);
		}
		return true;
	}
}

void FScctLightAudit::Start()
{
	if (!LightAuditTicker.IsValid())
	{
		LightAuditTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&LightAuditTick));
	}
}

void FScctLightAudit::Stop()
{
	if (LightAuditTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LightAuditTicker);
		LightAuditTicker.Reset();
	}
}

FString FScctLightAudit::DescribeLevel(const FScctLightLevel& Level)
{
	return FString::Printf(TEXT("lights=%d hidden=%d projectors=%d dynamic=%d baked=%d characters_only=%d special_lit=%d unused=%d zones=%d glow_threshold=%d glow=%d shadow_darkness_scale=%.2f"),
		Level.Lights, Level.Hidden, Level.Projectors, Level.Dynamic, Level.Baked, Level.CharactersOnly, Level.SpecialLit, Level.Unused, Level.Zones,
		Level.GlowThreshold, Level.GlowEnabled, Level.ShadowDarknessScale);
}
