#include "Settings/ScctNvidiaDlss.h"

#if SCCT_WITH_NVIDIA_DLSS
#include "DLSSLibrary.h"
#include "DynamicRHI.h"
#include "Features/IModularFeatures.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "StreamlineLibraryDLSSG.h"
#include "Windows/IDXGISwapchainProvider.h"
#endif

namespace ScctNvidiaDlss
{
#if SCCT_WITH_NVIDIA_DLSS
	namespace
	{
		UDLSSMode VendorPreset(ESuperResolutionPreset Preset)
		{
			switch (Preset)
			{
			case ESuperResolutionPreset::Quality: return UDLSSMode::Quality;
			case ESuperResolutionPreset::Balanced: return UDLSSMode::Balanced;
			case ESuperResolutionPreset::Performance: return UDLSSMode::Performance;
			case ESuperResolutionPreset::UltraPerformance: return UDLSSMode::UltraPerformance;
			case ESuperResolutionPreset::DLAA: return UDLSSMode::DLAA;
			default: return UDLSSMode::Off;
			}
		}

		EStreamlineDLSSGMode VendorGeneration(int32 Multiplier)
		{
			switch (Multiplier)
			{
			case 2: return EStreamlineDLSSGMode::On2X;
			case 3: return EStreamlineDLSSGMode::On3X;
			case 4: return EStreamlineDLSSGMode::On4X;
			case 5: return EStreamlineDLSSGMode::On5X;
			case 6: return EStreamlineDLSSGMode::On6X;
			default: return EStreamlineDLSSGMode::Off;
			}
		}

		bool LibraryReady(FName Module)
		{
			return GDynamicRHI && FApp::CanEverRender() && !IsRunningCommandlet() &&
				FModuleManager::Get().IsModuleLoaded(Module);
		}

		bool UsesStreamlineSwapchain()
		{
			// D3D chooses the first registered provider supporting its RHI when creating the viewport.
			// A hardware support query alone can succeed even when another provider owns presentation.
			const ERHIInterfaceType RHI = RHIGetInterfaceType();
			if (RHI != ERHIInterfaceType::D3D11 && RHI != ERHIInterfaceType::D3D12) return false;
			for (IDXGISwapchainProvider* Provider : IModularFeatures::Get().GetModularFeatureImplementations<IDXGISwapchainProvider>(
				IDXGISwapchainProvider::GetModularFeatureName()))
			{
				if (Provider->SupportsRHI(RHI))
					return FCString::Stristr(Provider->GetProviderName(), TEXT("Streamline")) != nullptr;
			}
			return false;
		}
	}
#endif

	bool IsSuperResolutionSupported()
	{
#if SCCT_WITH_NVIDIA_DLSS
		return LibraryReady(TEXT("DLSS")) && LibraryReady(TEXT("DLSSBlueprint")) && UDLSSLibrary::IsDLSSSupported();
#else
		return false;
#endif
	}

	bool IsFrameGenerationSupported()
	{
#if SCCT_WITH_NVIDIA_DLSS
		return SupportsFrameGenerationHardwareMode(2) && UsesStreamlineSwapchain();
#else
		return false;
#endif
	}

	bool SupportsSuperResolutionPreset(ESuperResolutionPreset Preset)
	{
#if SCCT_WITH_NVIDIA_DLSS
		const UDLSSMode Mode = VendorPreset(Preset);
		return Mode != UDLSSMode::Off && IsSuperResolutionSupported() && UDLSSLibrary::IsDLSSModeSupported(Mode);
#else
		return false;
#endif
	}

	bool SupportsFrameGenerationHardwareMode(int32 Multiplier)
	{
		if (Multiplier == 0) return true;
#if SCCT_WITH_NVIDIA_DLSS
		const EStreamlineDLSSGMode Mode = VendorGeneration(Multiplier);
		return Mode != EStreamlineDLSSGMode::Off && !FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen")) &&
			LibraryReady(TEXT("StreamlineCore")) &&
			LibraryReady(TEXT("StreamlineDLSSGBlueprint")) && UStreamlineLibraryDLSSG::IsDLSSGModeSupported(Mode);
#else
		return false;
#endif
	}

	bool SupportsFrameGenerationMode(int32 Multiplier)
	{
		if (Multiplier == 0) return true;
#if SCCT_WITH_NVIDIA_DLSS
		return SupportsFrameGenerationHardwareMode(Multiplier) && UsesStreamlineSwapchain();
#else
		return false;
#endif
	}

	bool SetSuperResolution(bool bEnabled, ESuperResolutionPreset Preset, FIntPoint OutputResolution, float& OutScreenPercentage)
	{
		OutScreenPercentage = 0.f;
#if SCCT_WITH_NVIDIA_DLSS
		if (!IsSuperResolutionSupported()) return !bEnabled;
		if (!bEnabled)
		{
			if (UDLSSLibrary::IsDLSSEnabled()) UDLSSLibrary::EnableDLSS(false);
			return !UDLSSLibrary::IsDLSSEnabled();
		}
		if (!SupportsSuperResolutionPreset(Preset) || OutputResolution.X <= 0 || OutputResolution.Y <= 0) return false;
		bool bSupported = false, bFixed = false;
		float Optimal = 0.f, Min = 0.f, Max = 0.f, Sharpness = 0.f;
		UDLSSLibrary::GetDLSSModeInformation(VendorPreset(Preset), FVector2D(OutputResolution), bSupported, Optimal, bFixed, Min, Max, Sharpness);
		if (!bSupported || !FMath::IsFinite(Optimal) || Optimal <= 0.f || Optimal > 100.f) return false;
		UDLSSLibrary::EnableDLSS(true);
		if (!UDLSSLibrary::IsDLSSEnabled()) return false;
		OutScreenPercentage = Optimal;
		return true;
#else
		return !bEnabled;
#endif
	}

	bool SetFrameGeneration(int32 Multiplier)
	{
#if SCCT_WITH_NVIDIA_DLSS
		if (!LibraryReady(TEXT("StreamlineDLSSGBlueprint"))) return Multiplier == 0;
		if (!SupportsFrameGenerationMode(Multiplier)) return false;
		const EStreamlineDLSSGMode Mode = VendorGeneration(Multiplier);
		if (UStreamlineLibraryDLSSG::GetDLSSGMode() != Mode) UStreamlineLibraryDLSSG::SetDLSSGMode(Mode);
		return UStreamlineLibraryDLSSG::GetDLSSGMode() == Mode;
#else
		return Multiplier == 0;
#endif
	}

	bool GetFrameTiming(float& OutPresentedHz, int32& OutPresentedFrames)
	{
		OutPresentedHz = 0.f;
		OutPresentedFrames = 0;
#if SCCT_WITH_NVIDIA_DLSS
		if (LibraryReady(TEXT("StreamlineDLSSGBlueprint")) && LibraryReady(TEXT("StreamlineCore")))
			UStreamlineLibraryDLSSG::GetDLSSGFrameTiming(OutPresentedHz, OutPresentedFrames);
#endif
		return FMath::IsFinite(OutPresentedHz) && OutPresentedHz > 0.f && OutPresentedFrames > 0;
	}
}
