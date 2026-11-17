#include "Play/ScctExposure.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
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

float FScctExposure::FlashScale()
{
	return PictureScale(LeastFlashScale, MostFlashScale);
}

float FScctExposure::MuzzleFlashCandelas()
{
	return FMath::Max(CVarFlashCandelas.GetValueOnGameThread(), 0.0f);
}

float FScctExposure::PictureScale(float Least, float Most)
{
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	return Exposure > 0.0f ? FMath::Clamp(ReferenceExposure / Exposure, Least, Most) : 1.0f;
}
