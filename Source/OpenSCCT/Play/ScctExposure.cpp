#include "Play/ScctExposure.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
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
}

void FScctExposure::Start()
{
	if (!ScctExposureProbeInstance.IsValid() && GEngine != nullptr && GEngine->ViewExtensions.IsValid())
	{
		ScctExposureProbeInstance = FSceneViewExtensions::NewExtension<FScctExposureProbe>();
	}
}

void FScctExposure::Stop()
{
	ScctExposureProbeInstance.Reset();
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

float FScctExposure::FlashExposure()
{
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
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	return Exposure > 0.0f ? FMath::Clamp(ReferenceExposure / Exposure, Least, Most) : 1.0f;
}
