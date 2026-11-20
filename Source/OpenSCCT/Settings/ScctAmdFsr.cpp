#include "Settings/ScctAmdFsr.h"

#if SCCT_WITH_AMD_FSR
#include "DynamicRHI.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "RHIGlobals.h"
#include "Windows/IDXGISwapchainProvider.h"
#include "Windows/WindowsHWrapper.h"
THIRD_PARTY_INCLUDES_START
#include "ffx_upscale.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace ScctAmdFsr
{
#if SCCT_WITH_AMD_FSR
	namespace
	{
		PfnFfxQuery GetVendorQuery()
		{
			HMODULE Loader = ::GetModuleHandleW(L"amd_fidelityfx_loader_dx12.dll");
			return Loader ? reinterpret_cast<PfnFfxQuery>(FPlatformProcess::GetDllExport(Loader, TEXT("ffxQuery"))) : nullptr;
		}

		bool HasFidelityFxProvider(uint64 Effect, const TCHAR* FeatureDll)
		{
			if (!FApp::CanEverRender() || IsRunningCommandlet() || !GDynamicRHI ||
				RHIGetInterfaceType() != ERHIInterfaceType::D3D12 ||
				!FModuleManager::Get().IsModuleLoaded(TEXT("FFXD3D12Backend"))) return false;

			// The plugin logs a DLL load failure but still starts its module. Ask the loaded FidelityFX
			// runtime for providers compatible with the actual D3D device before exposing a setting.
			void* Device = GDynamicRHI->RHIGetNativeDevice();
			PfnFfxQuery Query = GetVendorQuery();
			if (!Device || !Query || !::GetModuleHandleW(FeatureDll)) return false;
			uint64_t Count = 0;
			ffxQueryDescGetVersions Versions = {};
			Versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
			Versions.createDescType = Effect;
			Versions.device = Device;
			Versions.outputCount = &Count;
			return Query(nullptr, &Versions.header) == FFX_API_RETURN_OK && Count > 0;
		}

		bool SetVendorOption(const TCHAR* Name, int32 Value)
		{
			IConsoleVariable* Option = IConsoleManager::Get().FindConsoleVariable(Name);
			if (!Option) return Value == 0;
			if (Option->GetInt() != Value) Option->Set(Value, ECVF_SetByCode);
			return Option->GetInt() == Value;
		}
	}
#endif

	bool IsUpscalingSupported()
	{
#if SCCT_WITH_AMD_FSR
		// Our PC cook targets SM6. The SM5 renderer omits the reflections denoiser shaders
		// requested when the plugin builds its reactive mask.
		return GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6 &&
			FModuleManager::Get().IsModuleLoaded(TEXT("FFXFSRTemporalUpscaling")) &&
			HasFidelityFxProvider(FFX_API_EFFECT_ID_UPSCALE, TEXT("amd_fidelityfx_upscaler_dx12.dll"));
#else
		return false;
#endif
	}

	bool IsFrameGenerationHardwareSupported()
	{
#if SCCT_WITH_AMD_FSR
		// D3D12 builds only a texture for -RenderOffScreen. There is no swapchain to present generated frames.
		return !GIsEditor && !FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen")) &&
			FModuleManager::Get().IsModuleLoaded(TEXT("FFXFrameInterpolation")) &&
			HasFidelityFxProvider(FFX_API_EFFECT_ID_FRAMEGENERATION, TEXT("amd_fidelityfx_framegeneration_dx12.dll"));
#else
		return false;
#endif
	}

	bool IsFrameGenerationSupported()
	{
#if SCCT_WITH_AMD_FSR
		if (!IsFrameGenerationHardwareSupported()) return false;
		// WindowsD3D12Viewport uses the first compatible provider for the lifetime of the swapchain.
		for (IDXGISwapchainProvider* Provider : IModularFeatures::Get().GetModularFeatureImplementations<IDXGISwapchainProvider>(
			IDXGISwapchainProvider::GetModularFeatureName()))
		{
			if (Provider->SupportsRHI(ERHIInterfaceType::D3D12))
				return FCString::Strcmp(Provider->GetProviderName(), TEXT("FSRSwapchainProvider")) == 0;
		}
#endif
		return false;
	}

	float GetScreenPercentage(int32 QualityMode)
	{
#if SCCT_WITH_AMD_FSR
		if (QualityMode > 0 && QualityMode <= 4 && IsUpscalingSupported())
		{
			float Ratio = 0.0f;
			ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode Query = {};
			Query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE;
			Query.qualityMode = QualityMode;
			Query.pOutUpscaleRatio = &Ratio;
			if (GetVendorQuery()(nullptr, &Query.header) == FFX_API_RETURN_OK && Ratio >= 1.0f) return 100.0f / Ratio;
		}
#endif
		return 100.0f;
	}

	bool SetUpscaling(bool bEnabled, int32 QualityMode)
	{
#if SCCT_WITH_AMD_FSR
		if (bEnabled && (QualityMode < 0 || QualityMode > 4 || !IsUpscalingSupported() ||
			!SetVendorOption(TEXT("r.FidelityFX.FSR.QualityMode"), QualityMode))) return false;
		return SetVendorOption(TEXT("r.FidelityFX.FSR.Enabled"), bEnabled ? 1 : 0);
#else
		return !bEnabled;
#endif
	}

	bool SetFrameGeneration(bool bEnabled)
	{
#if SCCT_WITH_AMD_FSR
		if (bEnabled && !IsFrameGenerationSupported()) return false;
		return SetVendorOption(TEXT("r.FidelityFX.FI.Enabled"), bEnabled ? 1 : 0);
#else
		return !bEnabled;
#endif
	}
}
