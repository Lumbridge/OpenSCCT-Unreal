#include "DynamicRHI.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "RHI.h"
#include "Windows/IDXGISwapchainProvider.h"

class FOpenSCCTGraphicsStartupModule final : public IModuleInterface
{
public:
	void StartupModule() override
	{
		// The graphics menu saves its choice after the game's swapchain has been created.
		// PostConfigInit can read it before either vendor registers a D3D12 swapchain provider.
		const TCHAR* CommandLine = FCommandLine::Get();
#if WITH_EDITOR
		if (!FParse::Param(CommandLine, TEXT("game"))) return;
#endif
		FModuleManager::Get().OnModulesChanged().AddRaw(this, &FOpenSCCTGraphicsStartupModule::OnModulesChanged);
		int32 FrameGeneration = 0;
		if (!GConfig || !GConfig->GetInt(TEXT("OpenSCCT.Settings"), TEXT("Graphics.FrameGeneration"), FrameGeneration, GGameUserSettingsIni)
			|| FrameGeneration != 2 || FParse::Param(CommandLine, TEXT("slswapchainprovider"))
			|| FParse::Param(CommandLine, TEXT("slnoswapchainprovider"))
			|| !IPluginManager::Get().FindEnabledPlugin("FSR").IsValid()) return;

		// Suppress only Streamline's frame-generation swapchain. DLSS upscaling remains available.
		FCommandLine::Append(TEXT(" -slnoswapchainprovider"));
		UE_LOG(LogInit, Display, TEXT("OpenSCCT selected the AMD FSR frame-generation swapchain for this run."));
	}

	void ShutdownModule() override
	{
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	}

private:
	void OnModulesChanged(FName ModuleName, EModuleChangeReason Reason)
	{
		// Each vendor registers during its module's StartupModule. Decide after those callbacks
		// return, before D3D12 picks the first provider for the first game window.
		if (Reason != EModuleChangeReason::ModuleLoaded || (ModuleName != TEXT("FFXD3D12Backend") && ModuleName != TEXT("StreamlineRHI"))
			|| !GDynamicRHI || RHIGetInterfaceType() != ERHIInterfaceType::D3D12) return;

		IModularFeatures& Features = IModularFeatures::Get();
		const FName Type = IDXGISwapchainProvider::GetModularFeatureName();
		IDXGISwapchainProvider* Amd = nullptr;
		IDXGISwapchainProvider* Nvidia = nullptr;
		for (IDXGISwapchainProvider* Provider : Features.GetModularFeatureImplementations<IDXGISwapchainProvider>(Type))
		{
			if (!Provider->SupportsRHI(ERHIInterfaceType::D3D12)) continue;
			const TCHAR* Name = Provider->GetProviderName();
			if (FCString::Strcmp(Name, TEXT("FSRSwapchainProvider")) == 0) Amd = Provider;
			else if (FCString::Strcmp(Name, TEXT("FStreamlineD3D12DXGISwapchainProvider")) == 0) Nvidia = Provider;
		}
		if (Amd && Nvidia)
		{
			const TCHAR* CommandLine = FCommandLine::Get();
			const bool bUseNvidia = FParse::Param(CommandLine, TEXT("slswapchainprovider")) ||
				(IsRHIDeviceNVIDIA() && !FParse::Param(CommandLine, TEXT("slnoswapchainprovider")));
			// Only one provider can ever present this window. Keep both plugins loaded for upscaling.
			Features.UnregisterModularFeature(Type, bUseNvidia ? Amd : Nvidia);
			UE_LOG(LogInit, Display, TEXT("OpenSCCT selected %s for D3D12 presentation."), (bUseNvidia ? Nvidia : Amd)->GetProviderName());
		}
	}
};

IMPLEMENT_MODULE(FOpenSCCTGraphicsStartupModule, OpenSCCTGraphicsStartup)
