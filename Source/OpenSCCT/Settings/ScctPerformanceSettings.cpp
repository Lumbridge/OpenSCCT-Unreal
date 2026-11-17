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

	const FName VSyncKey(TEXT("Performance.VSync"));
	const FName MaxFpsKey(TEXT("Performance.MaxFps"));
	const FName LowLatencyKey(TEXT("Performance.LowLatency"));
	const FName RendererKey(TEXT("Performance.Renderer"));

	/** The frame rate limit's ends (Godot's DisplayFrameCap: 30 to 700, here 30 to 500) and its two notches past them. */
	constexpr int32 MinFps = 30;
	constexpr int32 MaxFps = 500;
	constexpr int32 FollowMonitor = MinFps - 1;
	constexpr int32 Unlimited = MaxFps + 1;

	/** The refresh rate assumed when the monitor's cannot be read. */
	constexpr float FallbackHz = 60.0f;

	const TCHAR* const RhiSection = TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings");

	/** What Performance.Renderer's choices write to DefaultGraphicsRHI. */
	const TCHAR* const RhiValues[] = { TEXT("DefaultGraphicsRHI_DX12"), TEXT("DefaultGraphicsRHI_Vulkan") };

	float LastHz = 0.0f;
	double NextMonitorCheck = 0.0;

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

	int32 ScreenFps(float Hz)
	{
		return Hz >= 1.0f ? FMath::RoundToInt(Hz) : FMath::RoundToInt(FallbackHz);
	}

	/** The cap the player asked for, in frames per second (0: none), before vertical sync is considered. */
	int32 Target(float Hz)
	{
		const int32 Value = FScctSettings::GetInt(MaxFpsKey);
		return Value <= FollowMonitor ? ScreenFps(Hz) : Value >= Unlimited ? 0 : Value;
	}

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
	FScctSettings::Register(Option(TEXT("Performance.VSync"), PerformancePage, TEXT("Frame pacing"), TEXT("Vertical sync"),
		TEXT("Waits for the screen's refresh before showing a frame: no tearing, a little more input lag."), EScctSettingType::Bool, TEXT("false"), Order++));
	{
		FScctSettingDesc Desc = Option(TEXT("Performance.MaxFps"), PerformancePage, TEXT("Frame pacing"), TEXT("Max FPS"),
			TEXT("The frame rate limit in menus, the lobby and matches. All the way left matches the refresh rate of the monitor the game is on; all the way right is unlimited. Vertical sync can still limit it."),
			EScctSettingType::Int, FString::FromInt(FollowMonitor), Order++);
		Desc.Min = FollowMonitor;
		Desc.Max = Unlimited;
		Desc.Step = 1.0f;
		Desc.FormatValue = [](float Value)
		{
			const int32 Fps = FMath::RoundToInt(Value);
			return Fps <= FollowMonitor ? FString::Printf(TEXT("Monitor (%d Hz)"), ScreenFps(MonitorHz()))
				: Fps >= Unlimited ? FString(TEXT("Unlimited")) : FString::FromInt(Fps);
		};
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Performance.LowLatency"), PerformancePage, TEXT("Latency"), TEXT("Reduce input lag"),
			TEXT("How far the game may prepare frames ahead of the screen. On waits for the previous frame to be handed to the graphics card; Ultra waits for it to be shown, the least lag at some cost in frame rate."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Off"), TEXT("On"), TEXT("Ultra") });
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Performance.Renderer"), PerformancePage, TEXT("Renderer"), TEXT("Renderer (applies after restart)"),
			*FString::Printf(TEXT("The graphics interface the game draws with. Takes effect the next time the game starts; this run uses %s."), *FApp::GetGraphicsRHI()),
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
	const float Hz = MonitorHz();
	const int32 Cap = Target(Hz);
	// With vertical sync on, a cap at or above the screen's rate is the swap's to keep.
	if (FScctSettings::GetBool(VSyncKey) && (Cap == 0 || Cap >= ScreenFps(Hz)))
	{
		return 0;
	}
	return Cap;
}

void ScctPerformanceSettings::Apply(bool bAtStart)
{
	UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr)
	{
		return;
	}
	LastHz = MonitorHz();
	const bool bVSync = FScctSettings::GetBool(VSyncKey);
	const int32 Limit = EffectiveLimit();
	Settings->SetVSyncEnabled(bVSync);
	Settings->SetFrameRateLimit(static_cast<float>(Limit));
	Settings->ApplyNonResolutionSettings();
	// ApplyNonResolutionSettings sets these through the engine; set at the game setting priority as well, so the
	// project's ini cannot win over them.
	SetCVar(TEXT("r.VSync"), bVSync ? 1 : 0);
	SetCVar(TEXT("t.MaxFPS"), static_cast<float>(Limit));

	const int32 LowLatency = FMath::Clamp(FScctSettings::GetInt(LowLatencyKey), 0, 2);
	SetCVar(TEXT("r.GTSyncType"), LowLatency);

	const int32 Renderer = FScctSettings::GetInt(RendererKey);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctPerformance: applied%s vsync %s, max FPS %s -> limit %d (t.MaxFPS %s, r.VSync %s; monitor %.0f Hz), r.GTSyncType %d; renderer %s chosen, %s running"),
		bAtStart ? TEXT(" at start:") : TEXT(":"), bVSync ? TEXT("on") : TEXT("off"), *FScctSettings::GetString(MaxFpsKey), Limit,
		*ScctVideoOptions::GetCVar(TEXT("t.MaxFPS")), *ScctVideoOptions::GetCVar(TEXT("r.VSync")), LastHz, LowLatency,
		Renderer == 1 ? TEXT("Vulkan") : TEXT("DirectX 12"), *FApp::GetGraphicsRHI());
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

	// The window may have been moved to a monitor of another refresh rate.
	if (Now >= NextMonitorCheck)
	{
		NextMonitorCheck = Now + 1.0;
		const float Hz = MonitorHz();
		if (!FMath::IsNearlyEqual(Hz, LastHz, 0.5f) && FScctSettings::GetInt(MaxFpsKey) <= FollowMonitor)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPerformance: the monitor is now %.0f Hz (was %.0f)"), Hz, LastHz);
			Apply(false);
		}
		LastHz = Hz;
	}

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
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPerf: cap %s, measured ~%.0f (%d frames in %.1f s; vsync %s, t.MaxFPS %s, monitor %.0f Hz; dynamic resolution %s, r.ScreenPercentage %s)"),
			Limit > 0 ? *FString::FromInt(Limit) : TEXT("none"), Fps, Check.Frames, Seconds,
			FScctSettings::GetBool(VSyncKey) ? TEXT("on") : TEXT("off"), *ScctVideoOptions::GetCVar(TEXT("t.MaxFPS")), LastHz,
			*DynamicText, *ScctVideoOptions::GetCVar(TEXT("r.ScreenPercentage")));
	}
}
