#include "Play/ScctExposure.h"

#include "Engine/Engine.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

#include <atomic>

namespace
{
	// The exposure scale of a room lit by a few lamps (about EV100 1 with the levels' post-process volume).
	constexpr float ReferenceExposure = 0.42f;
	constexpr float LeastFlashScale = 0.04f;
	constexpr float MostFlashScale = 2.0f;

	std::atomic<float> LastExposure{ 0.0f };

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
		}
	};

	TSharedPtr<FScctExposureProbe, ESPMode::ThreadSafe> Probe;
}

void FScctExposure::Start()
{
	if (!Probe.IsValid() && GEngine != nullptr && GEngine->ViewExtensions.IsValid())
	{
		Probe = FSceneViewExtensions::NewExtension<FScctExposureProbe>();
	}
}

void FScctExposure::Stop()
{
	Probe.Reset();
}

float FScctExposure::Get()
{
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	return Exposure > 0.0f ? Exposure : 1.0f;
}

float FScctExposure::FlashScale()
{
	return PictureScale(LeastFlashScale, MostFlashScale);
}

float FScctExposure::PictureScale(float Least, float Most)
{
	const float Exposure = LastExposure.load(std::memory_order_relaxed);
	return Exposure > 0.0f ? FMath::Clamp(ReferenceExposure / Exposure, Least, Most) : 1.0f;
}
