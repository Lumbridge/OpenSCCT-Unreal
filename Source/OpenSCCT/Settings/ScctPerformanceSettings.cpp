#include "Settings/ScctPerformanceSettings.h"

#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Settings/ScctSettingsForced.h"
#include "Settings/ScctVideoOptions.h"
#include "Widgets/SWindow.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

using ScctVideoOptions::Option;
using ScctVideoOptions::SetCVar;

namespace
{
	const TCHAR* const PerformancePage = TEXT("Performance");

	const FName LowLatencyKey(TEXT("Performance.LowLatency"));
	const FName RendererKey(TEXT("Performance.Renderer"));

	/** The refresh rate assumed when the monitor's cannot be read. */
	constexpr float FallbackHz = 60.0f;

	const TCHAR* const RhiSection =TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings");

	/** What Performance.Renderer's choices write to DefaultGraphicsRHI. */
	const TCHAR* const RhiValues[] = { TEXT("DefaultGraphicsRHI_DX12"), TEXT("DefaultGraphicsRHI_Vulkan") };

	/** -ScctPerfCheck: when the measured window starts and ends, and the frames counted in it. */
	struct FPerfCheck
	{
		bool bWanted = false;
		bool bDone = false;
		double Start = 0.0;
		double End = 0.0;
		int32 Frames = 0;
	} Check;

	constexpr double PerfSettleSeconds = 3.0;
	constexpr double PerfMeasureSeconds = 10.0;

	bool VulkanAvailable()
	{
#if WITH_EDITOR
		// The editor compiles shaders for any RHI as it needs them.
		return true;
#else
		TArray<FString> Formats;
		return GConfig != nullptr && GConfig->GetArray(RhiSection, TEXT("VulkanTargetedShaderFormats"), Formats, GEngineIni) > 0 && Formats.Num() > 0;
#endif
	}

	bool PlayingWorld()
	{
		if (GEngine == nullptr)
		{
			return false;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (World != nullptr && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && World->HasBegunPlay())
			{
				return true;
			}
		}
		return false;
	}
}

void ScctPerformanceSettings::Register()
{
	int32 Order = 0;
	// Vertical sync and the frame limit are the settings page's own (UGameUserSettings, the Video tab).
	{
		FScctSettingDesc Desc = Option(TEXT("Performance.LowLatency"), PerformancePage, TEXT("Latency"), TEXT("Reduce input lag"),
			TEXT("Limits how far the CPU prepares frames ahead of the GPU. On and Ultra can reduce input delay. Ultra waits longer between frames and can reduce FPS."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Off"), TEXT("On"), TEXT("Ultra") });
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Performance.Renderer"), PerformancePage, TEXT("Graphics API"), TEXT("Graphics API (applies after restart)"),
			*FString::Printf(TEXT("Chooses the graphics API at the next launch. Performance depends on your GPU and driver. This session uses %s."), *FApp::GetGraphicsRHI()),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = VulkanAvailable() ? ScctVideoOptions::Texts({ TEXT("DirectX 12"), TEXT("Vulkan") }) : ScctVideoOptions::Texts({ TEXT("DirectX 12") });
		FScctSettings::Register(Desc);
	}

	Check.bWanted = FParse::Param(FCommandLine::Get(), TEXT("ScctPerfCheck"));
}

bool ScctPerformanceSettings::Owns(FName Key)
{
	return Key.ToString().StartsWith(TEXT("Performance."));
}

float ScctPerformanceSettings::MonitorHz()
{
#if PLATFORM_WINDOWS
	HMONITOR Monitor = nullptr;
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow(); Window.IsValid() && Window->GetNativeWindow().IsValid())
		{
			if (HWND Handle = static_cast<HWND>(Window->GetNativeWindow()->GetOSWindowHandle()))
			{
				Monitor = MonitorFromWindow(Handle, MONITOR_DEFAULTTONEAREST);
			}
		}
	}
	MONITORINFOEXW Info = {};
	Info.cbSize = sizeof(Info);
	DEVMODEW Mode = {};
	Mode.dmSize = sizeof(Mode);
	const bool bRead = Monitor != nullptr && GetMonitorInfoW(Monitor, &Info)
		? EnumDisplaySettingsW(Info.szDevice, ENUM_CURRENT_SETTINGS, &Mode) != 0
		: EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &Mode) != 0;
	// 0 and 1 mean "the hardware's default" rather than a rate.
	if (bRead && Mode.dmDisplayFrequency > 1)
	{
		return static_cast<float>(Mode.dmDisplayFrequency);
	}
#endif
	return FallbackHz;
}

int32 ScctPerformanceSettings::EffectiveLimit()
{
	// The settings page's frame limit (UGameUserSettings); vertical sync takes the place of a limit at or above the
	// monitor's rate.
	const UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	const int32 Cap = Settings != nullptr ? FMath::RoundToInt(Settings->GetFrameRateLimit()) : 0;
	if (Settings != nullptr && Settings->IsVSyncEnabled() && (Cap == 0 || Cap >= FMath::RoundToInt(MonitorHz())))
	{
		return 0;
	}
	return Cap;
}

void ScctPerformanceSettings::Apply(bool bAtStart)
{
	const int32 LowLatency = FMath::Clamp(FScctSettings::GetInt(LowLatencyKey), 0, 2);
	SetCVar(TEXT("r.GTSyncType"), LowLatency);

	const int32 Renderer = FScctSettings::GetInt(RendererKey);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctPerformance: applied%s r.GTSyncType %d; renderer %s chosen, %s running"),
		bAtStart ? TEXT(" at start:") : TEXT(":"), LowLatency, Renderer == 1 ? TEXT("Vulkan") : TEXT("DirectX 12"), *FApp::GetGraphicsRHI());
}

void ScctPerformanceSettings::WriteRenderer()
{
	if (GConfig == nullptr)
	{
		return;
	}
	const int32 Renderer = FMath::Clamp(FScctSettings::GetInt(RendererKey), 0, 1);
	// Forced for a run (-ScctSettingSet), it must not reach the next start.
	if (FScctSettingsForced::IsForced(RendererKey))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPerformance: renderer %s is forced for this run; not written for the next start"), RhiValues[Renderer]);
		return;
	}
	GConfig->SetString(RhiSection, TEXT("DefaultGraphicsRHI"), RhiValues[Renderer], GEngineIni);
	GConfig->Flush(false, GEngineIni);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctPerformance: DefaultGraphicsRHI=%s written to %s for the next start (running %s)"),
		RhiValues[Renderer], *GEngineIni, *FApp::GetGraphicsRHI());
}

void ScctPerformanceSettings::Tick(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();

	if (!Check.bWanted || Check.bDone)
	{
		return;
	}
	if (Check.Start == 0.0)
	{
		if (PlayingWorld())
		{
			Check.Start = Now + PerfSettleSeconds;
			Check.End = Check.Start + PerfMeasureSeconds;
		}
		return;
	}
	if (Now < Check.Start)
	{
		return;
	}
	++Check.Frames;
	if (Now >= Check.End)
	{
		Check.bDone = true;
		const double Seconds = Now - Check.Start;
		const double Fps = Check.Frames / FMath::Max(Seconds, 0.001);
		FDynamicResolutionStateInfos Dynamic;
		GEngine->GetDynamicResolutionCurrentStateInfos(Dynamic);
		const int32 Limit = EffectiveLimit();
		const FString DynamicText = Dynamic.Status == EDynamicResolutionStatus::Enabled
			? FString::Printf(TEXT("on, at %.0f%%"), Dynamic.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction] * 100.0f)
			: FString(TEXT("off"));
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPerf: cap %s, measured ~%.0f (%d frames in %.1f s; r.VSync %s, t.MaxFPS %s, monitor %.0f Hz; dynamic resolution %s, r.ScreenPercentage %s)"),
			Limit > 0 ? *FString::FromInt(Limit) : TEXT("none"), Fps, Check.Frames, Seconds,
			*ScctVideoOptions::GetCVar(TEXT("r.VSync")), *ScctVideoOptions::GetCVar(TEXT("t.MaxFPS")), MonitorHz(),
			*DynamicText, *ScctVideoOptions::GetCVar(TEXT("r.ScreenPercentage")));
	}
}
