#include "Play/ScctExposure.h"

#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "OpenSCCT.h"
#include "Play/ScctLightPlan.h"
#include "Runtime/ScctFeatures.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

#include <atomic>

namespace
{
	// The exposure scale of a room lit by a few lamps (about EV100 1 with the levels' post-process volume).
	constexpr float ReferenceExposure = 0.42f;
	constexpr float LeastFlashScale = 0.04f;
	constexpr float MostFlashScale = 2.0f;

	// At 26 cd a shot lit nothing: in a lamp-lit room of the maps the exposure is at its dark end and FlashScale at
	// its least (0.04), which left one candela. Looked at with the flash held in such a room (AquaE's Pirates'
	// Room): 500 still shows nothing, 800 warms the floor, the posts and the ceiling beams around the gun, 1250 is
	// a strong glare and 3000 burns the floor white.
	TAutoConsoleVariable<float> CVarFlashCandelas(TEXT("scct.Flash.Candelas"), 800.0f,
		TEXT("Candelas of a muzzle flash's light in a moderately lit room, for every shot (it is scaled by the picture's exposure)."));

	TAutoConsoleVariable<float> CVarFlashWallLevel(TEXT("scct.Flash.WallLevel"), 30.0f,
		TEXT("How bright a flash light's spot on the nearest surface may be: candelas times exposure per square metre of ")
		TEXT("distance (0 for no limit). Calibrated by eye with the light held lit: 30 warms MissE's dark corridor walls ")
		TEXT("and MounE's pale tile floor and keeps their texture; 60 still turned that floor near white, 150 the walls."));

	// Closer than this the spot's size is about the light's own, and the distance stops meaning much.
	constexpr float LeastWallDistance = 15.0f;

	// The eye adapts to the flashes themselves: in a narrow corridor a burst lit the walls, the picture rose to 20
	// times its settled brightness, the exposure closed from 5 to 0.06 within a second, FlashScale followed it up to
	// its bound of 2 (1600 cd) and the walls burned white at any exposure. Flashes are therefore scaled by the
	// exposure the picture had settled on before them (SettledExposure), which follows the measured one only while
	// no flash has been lit for this long (the eye takes about that to come back from a burst).
	constexpr double FlashSettleSeconds = 3.0;
	float SettledExposure = 0.0f;
	double LastFlashSeconds = -1.0e9;

	std::atomic<float> LastExposure{ 0.0f };
	std::atomic<float> LastLuminance{ 0.0f };

	class FScctExposureProbe final : public FSceneViewExtensionBase
	{
	public:
		explicit FScctExposureProbe(const FAutoRegister& AutoRegister)
			: FSceneViewExtensionBase(AutoRegister)
		{
		}

		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
		virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

		virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override
		{
			// The player's own view only: a capture or a thumbnail has its own exposure.
			if (!InView.bIsGameView || InView.bIsSceneCapture || InView.State == nullptr)
			{
				return;
			}
			const float Exposure = InView.GetLastEyeAdaptationExposure();
			if (Exposure > 0.0f && FMath::IsFinite(Exposure))
			{
				LastExposure.store(Exposure, std::memory_order_relaxed);
			}
			const float Luminance = InView.GetLastAverageSceneLuminance();
			if (Luminance > 0.0f && FMath::IsFinite(Luminance))
			{
				LastLuminance.store(Luminance, std::memory_order_relaxed);
			}
		}
	};

	TSharedPtr<FScctExposureProbe, ESPMode::ThreadSafe> ScctExposureProbeInstance;

	// ---- the range the eye may move over ---------------------------------------------------------------------
	// Every map level (imported or built at run time) carries a post-process volume whose automatic exposure ran
	// from EV100 -4 to 4 around middle grey. Measured (scct.Exposure.Log), that made every place the same
	// brightness: StatE's night street (scene luminance 0.006) sat on the floor at -4.2 and read as an overcast
	// day, AquaE's unlit Pirates' Room (0.006) as an evenly lit hall, and any lamp-lit room (0.03 to 0.08, EV -1.5
	// to -2.9) was brought to middle grey. The original has no eye at all: a dark place is dark. So the range is
	// set here, over the levels' volumes (one place for every map, and no map needs importing again): the eye still
	// closes for a bright room, but opens no further than a dim lamp-lit room needs (luminance about 0.022), and
	// the whole picture sits three quarters of a stop under middle grey. Darker places than that stay darker in
	// step: the night street and the Pirates' Room come out at about a quarter of a lit room.
	TAutoConsoleVariable<float> CVarExposureMinEV(TEXT("scct.Exposure.MinEV"), -3.0f,
		TEXT("The darkest scene the automatic exposure of a map opens up for, in EV100 (the levels' own volumes say -4)."));
	TAutoConsoleVariable<float> CVarExposureMaxEV(TEXT("scct.Exposure.MaxEV"), 4.0f,
		TEXT("The brightest scene the automatic exposure of a map closes down for, in EV100."));
	TAutoConsoleVariable<float> CVarExposureBias(TEXT("scct.Exposure.Bias"), -0.75f,
		TEXT("Stops added to a map's exposure after the range (negative darkens the whole picture)."));
	TAutoConsoleVariable<int32> CVarExposureLog(TEXT("scct.Exposure.Log"), 0,
		TEXT("1 logs the exposure the player's picture has (its scale and EV100) and the scene's average luminance once a second."));

	// The sky dome's brightness is light in the scene, so the more the eye opened the paler the night sky got: a
	// washed blue over a dark roof. Above this exposure scale the dome is dimmed in step (down to a tenth of
	// itself), so the sky a lit yard shows is the sky a dark roof shows. The sky light it throws is left alone.
	TAutoConsoleVariable<float> CVarExposureSkyScale(TEXT("scct.Exposure.SkyScale"), 2.0f,
		TEXT("The exposure scale above which a map's sky dome is dimmed to stay as dark in the picture (0 leaves the dome alone)."));
	constexpr float ExposureSkyLeastShare = 0.1f;
	const FName ExposureSkyBrightness(TEXT("Brightness"));
	const FName ExposureSkyStars(TEXT("StarBrightness"));

	struct FExposureSky
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		float Brightness = 0.0f;
		float Stars = 0.0f;
		float Share = 1.0f;
	};
	FExposureSky ExposureSky;

	void ExposureFindSky(UWorld* World)
	{
		ExposureSky = FExposureSky();
		ExposureSky.World = World;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			UStaticMeshComponent* Component = It->GetStaticMeshComponent();
			UMaterialInterface* Material = Component != nullptr ? Component->GetMaterial(0) : nullptr;
			const UMaterial* Base = Material != nullptr ? Material->GetMaterial() : nullptr;
			if (Base == nullptr || Base->GetName() != TEXT("M_ScctSky"))
			{
				continue;
			}
			float Brightness = 0.0f;
			float Stars = 0.0f;
			Material->GetScalarParameterValue(ExposureSkyBrightness, Brightness);
			Material->GetScalarParameterValue(ExposureSkyStars, Stars);
			if (UMaterialInstanceDynamic* Instance = Component->CreateAndSetMaterialInstanceDynamic(0))
			{
				ExposureSky.Material = Instance;
				ExposureSky.Brightness = Brightness;
				ExposureSky.Stars = Stars;
			}
			return;
		}
	}

	// ---- the lighting plan's picture (scct.Light.Plan 1): no eye at all ---------------------------------------
	// The original has no eye: a surface of a given light is the same display value in a cellar and in a lit hall.
	// Measured with the automatic exposure (light parity, 64 spots a map): the scale ran from 0.48 to 4.76 and
	// brought lit over dark on AquaE down from 7.7 times in the scene to 2.7 times in the picture. Under the plan
	// the exposure is manual and one number for every map and every place, the local exposure (which lifts dark
	// parts of a picture beside bright ones) is off, and what followed the eye (the sky dome's dimming, the flash
	// lights' scale, the body lights' scale, the wall hold) goes by that one number instead of a reading.
	TAutoConsoleVariable<float> CVarLightExposureScale(TEXT("scct.Light.ExposureScale"), 1.0f,
		TEXT("With scct.Light.Plan 1: the fixed exposure scale of every map (scene light times this is the picture before the tone curve). ")
		TEXT("1 is as dark as the original; more brightens the whole picture alike."));
	TAutoConsoleVariable<float> CVarLightNightGain(TEXT("scct.Light.NightGain"), 4.0f,
		TEXT("With scct.Light.Plan 1: the night vision's gain, fixed (the released picture moves it with the exposure's reading)."));

	// A manual exposure without the camera's own settings comes out as 2^bias, measured: with a bias of log2(1.2)
	// (what the automatic exposure's rule, scale = 1 / (1.2 x 2^EV100), suggests for the project's extended luminance
	// range) the view's exposure read 1.2000 on every spot of AquaE and the tone ramp agreed. The manual mode leaves
	// that 1.2 out.
	constexpr float ExposureManualWhite = 1.0f;

	float ExposureFixedScale()
	{
		return FMath::Clamp(CVarLightExposureScale.GetValueOnAnyThread(), 0.05f, 16.0f);
	}

	// Above the level's own volume (0), below the goggles' (1000, which sets no exposure).
	// Measured (Test/ScctLightEngineChecks lists the view's volumes): the world places a volume by the priority it has
	// when it is added and never sorts again, and this one is added with 0 and given its 10 afterwards, so volumes
	// added later go in front of it whatever their priority: it is applied last and what it sets wins over every
	// other volume. Left as it is (the released picture depends on it, and the plan's fixed exposure is the safer for
	// it); a volume that must win over this one has to be added after its priority is set, with a priority above 10,
	// and still will not, until this one is taken out of the world's list and added again.
	constexpr float ExposureRangePriority = 10.0f;

	TWeakObjectPtr<APostProcessVolume> ExposureRangeVolume;
	FTSTicker::FDelegateHandle ExposureRangeTicker;
	double ExposureLastLogSeconds = 0.0;

	bool ExposureIsMapWorld(const UWorld* World)
	{
		if (World == nullptr || !(World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
		{
			return false;
		}
		const FString Path = World->GetPathName();
		return Path.Contains(TEXT("/Imported/Maps/")) || Path.Contains(TEXT("/RuntimeMap/"));
	}

	bool ExposureTickRange(float)
	{
		if (GEngine == nullptr)
		{
			return true;
		}
		UWorld* MapWorld = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (ExposureIsMapWorld(Context.World()) && Context.World()->HasBegunPlay())
			{
				MapWorld = Context.World();
				break;
			}
		}
		if (MapWorld == nullptr)
		{
			return true;
		}

		APostProcessVolume* Volume = ExposureRangeVolume.Get();
		if (!IsValid(Volume) || Volume->GetWorld() != MapWorld)
		{
			FActorSpawnParameters Parameters;
			Parameters.ObjectFlags |= RF_Transient;
			Volume = MapWorld->SpawnActor<APostProcessVolume>(Parameters);
			ExposureRangeVolume = Volume;
			if (!IsValid(Volume))
			{
				return true;
			}
			Volume->bUnbound = true;
			Volume->BlendWeight = 1.0f;
			Volume->Priority = ExposureRangePriority;
		}
		const bool bFixed = FScctLightPlan::IsOn();
		const float MinEV = CVarExposureMinEV.GetValueOnGameThread();
		const float MaxEV = FMath::Max(CVarExposureMaxEV.GetValueOnGameThread(), MinEV);
		FPostProcessSettings& Settings = Volume->Settings;
		if (bFixed)
		{
			// One exposure for every place: manual, by the bias alone.
			Settings.bOverride_AutoExposureMethod = true;
			Settings.AutoExposureMethod = AEM_Manual;
			Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			Settings.AutoExposureApplyPhysicalCameraExposure = false;
			Settings.bOverride_AutoExposureBias = true;
			Settings.AutoExposureBias = FMath::Log2(ExposureManualWhite * ExposureFixedScale());
			// No local exposure: contrast scales of 1 leave every part of the picture at the one exposure.
			Settings.bOverride_LocalExposureHighlightContrastScale = true;
			Settings.LocalExposureHighlightContrastScale = 1.0f;
			Settings.bOverride_LocalExposureShadowContrastScale = true;
			Settings.LocalExposureShadowContrastScale = 1.0f;
			Settings.bOverride_LocalExposureDetailStrength = true;
			Settings.LocalExposureDetailStrength = 1.0f;
		}
		else
		{
			Settings.bOverride_AutoExposureMinBrightness = true;
			Settings.AutoExposureMinBrightness = MinEV;
			Settings.bOverride_AutoExposureMaxBrightness = true;
			Settings.AutoExposureMaxBrightness = MaxEV;
			Settings.bOverride_AutoExposureBias = true;
			Settings.AutoExposureBias = CVarExposureBias.GetValueOnGameThread();
		}

		// The sky dome, held as dark in the picture as it is at the reference exposure.
		if (ExposureSky.World.Get() != MapWorld)
		{
			ExposureFindSky(MapWorld);
		}
		if (UMaterialInstanceDynamic* Sky = ExposureSky.Material.Get())
		{
			const float SkyScale = CVarExposureSkyScale.GetValueOnGameThread();
			const float Scale = LastExposure.load(std::memory_order_relaxed);
			// Under the plan the dome is never dimmed: the exposure does not move, so neither does the sky.
			const float Share = !bFixed && SkyScale > 0.0f && Scale > 0.0f ? FMath::Clamp(SkyScale / Scale, ExposureSkyLeastShare, 1.0f) : 1.0f;
			if (!FMath::IsNearlyEqual(Share, ExposureSky.Share, 0.002f))
			{
				ExposureSky.Share = Share;
				Sky->SetScalarParameterValue(ExposureSkyBrightness, ExposureSky.Brightness * Share);
				Sky->SetScalarParameterValue(ExposureSkyStars, ExposureSky.Stars * Share);
			}
		}

		if (CVarExposureLog.GetValueOnGameThread() != 0 && FPlatformTime::Seconds() - ExposureLastLogSeconds >= 1.0)
		{
			ExposureLastLogSeconds = FPlatformTime::Seconds();
			const float Exposure = LastExposure.load(std::memory_order_relaxed);
			const float Luminance = LastLuminance.load(std::memory_order_relaxed);
			// With the project's extended luminance range the scale is 1 / (1.2 * 2^EV100).
			if (bFixed)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Exposure: scale %.4f (EV100 %.2f), fixed at %.4f by scct.Light.Plan %d (manual, bias %.3f, no local exposure), scene luminance read %.5f, flash scale %.3f, sky share %.2f"),
					Exposure, Exposure > 0.0f ? FMath::Log2(1.0f / (1.2f * Exposure)) : 0.0f, ExposureFixedScale(), FScctLightPlan::Active(),
					Settings.AutoExposureBias, Luminance, FScctExposure::FlashScale(), ExposureSky.Share);
			}
			else
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Exposure: scale %.4f (EV100 %.2f), scene luminance %.5f, picture level %.3f, range %.1f..%.1f, bias %.2f, sky share %.2f"),
					Exposure, Exposure > 0.0f ? FMath::Log2(1.0f / (1.2f * Exposure)) : 0.0f, Luminance, Exposure * Luminance, MinEV, MaxEV,
					CVarExposureBias.GetValueOnGameThread(), ExposureSky.Share);
			}
		}
		return true;
	}
}

void FScctExposure::Start()
{
	if (!ScctExposureProbeInstance.IsValid() && GEngine != nullptr && GEngine->ViewExtensions.IsValid())
	{
		ScctExposureProbeInstance = FSceneViewExtensions::NewExtension<FScctExposureProbe>();
	}
	if (!ExposureRangeTicker.IsValid())
	{
		ExposureRangeTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ExposureTickRange));
	}
}

void FScctExposure::Stop()
{
	ScctExposureProbeInstance.Reset();
	if (ExposureRangeTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ExposureRangeTicker);
		ExposureRangeTicker.Reset();
	}
}

float FScctExposure::Get()
{
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	return Exposure > 0.0f ? Exposure : 1.0f;
}

float FScctExposure::PictureLevel()
{
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	const float Luminance = LastLuminance.load(std::memory_order_relaxed);
	return Exposure > 0.0f && Luminance > 0.0f ? Exposure * Luminance : 0.0f;
}

float FScctExposure::SettledPictureLevel()
{
	// Middle grey, moved by the bias the maps are played with.
	return 0.18f * FMath::Pow(2.0f, CVarExposureBias.GetValueOnGameThread());
}

bool FScctExposure::IsFixed()
{
	return FScctLightPlan::IsOn();
}

float FScctExposure::FixedScale()
{
	return ExposureFixedScale();
}

float FScctExposure::FixedNightGain()
{
	return FMath::Max(CVarLightNightGain.GetValueOnAnyThread(), 0.0f);
}

float FScctExposure::FlashExposure()
{
	if (FScctLightPlan::IsOn())
	{
		// The plan's exposure does not move, so there is nothing to settle on: the one number.
		return ExposureFixedScale();
	}
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	if (Exposure > 0.0f && (SettledExposure <= 0.0f || FPlatformTime::Seconds() - LastFlashSeconds > FlashSettleSeconds))
	{
		SettledExposure = Exposure;
	}
	return SettledExposure > 0.0f ? SettledExposure : 1.0f;
}

void FScctExposure::NoteFlash()
{
	FlashExposure();
	LastFlashSeconds = FPlatformTime::Seconds();
}

float FScctExposure::FlashScale()
{
	return FMath::Clamp(ReferenceExposure / FlashExposure(), LeastFlashScale, MostFlashScale);
}

float FScctExposure::MuzzleFlashCandelas()
{
	return FMath::Max(CVarFlashCandelas.GetValueOnGameThread(), 0.0f);
}

bool FScctExposure::TraceLevel(const FVector& Start, const FVector& End, float& OutDistance)
{
	OutDistance = FVector::Dist(Start, End);
	const FScctFxTraceApi* Api = ScctFeature<FScctFxTraceApi>();
	FScctFxHit Hit;
	if (Api == nullptr || Api->Trace == nullptr || Api->Trace(Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z, &Hit) == 0)
	{
		return false;
	}
	OutDistance *= FMath::Clamp(Hit.Time, 0.0f, 1.0f);
	return true;
}

float FScctExposure::WallHold(const FVector& At, float Candelas, float Radius, float* OutNearest)
{
	if (OutNearest != nullptr)
	{
		*OutNearest = Radius;
	}
	if (Candelas <= 0.0f || Radius <= 0.0f)
	{
		return 1.0f;
	}

	// The nearest surface in any of 26 directions (the faces, edges and corners of a cube): a wall met at a slant
	// is found within a factor of about 1.2 of its true distance.
	float Nearest = Radius;
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 Z = -1; Z <= 1; ++Z)
			{
				if (X == 0 && Y == 0 && Z == 0)
				{
					continue;
				}
				float Distance = 0.0f;
				if (TraceLevel(At, At + FVector(X, Y, Z).GetUnsafeNormal() * Nearest, Distance))
				{
					Nearest = FMath::Min(Nearest, Distance);
				}
			}
		}
	}
	if (OutNearest != nullptr)
	{
		*OutNearest = Nearest;
	}
	const float Level = CVarFlashWallLevel.GetValueOnGameThread();
	if (Level <= 0.0f)
	{
		return 1.0f;
	}

	// Picture brightness of the spot ~ Candelas / distance^2 * exposure: kept at or under Level.
	const float Metres = FMath::Max(Nearest, LeastWallDistance) / 100.0f;
	const float Most = Level * Metres * Metres / FlashExposure();
	return FMath::Clamp(Most / Candelas, 0.0f, 1.0f);
}

float FScctExposure::PictureScale(float Least, float Most)
{
	if (FScctLightPlan::IsOn())
	{
		return FMath::Clamp(ReferenceExposure / ExposureFixedScale(), Least, Most);
	}
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	return Exposure > 0.0f ? FMath::Clamp(ReferenceExposure / Exposure, Least, Most) : 1.0f;
}
