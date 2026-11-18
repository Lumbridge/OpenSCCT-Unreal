#include "Settings/ScctGraphicsSettings.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "RHIGlobals.h"
#include "Settings/ScctVideoOptions.h"

using ScctVideoOptions::Option;
using ScctVideoOptions::SetCVar;

namespace
{
	const TCHAR* const GraphicsPage = TEXT("Graphics");

	const FName AutoScaleKey(TEXT("Graphics.AutoScaleFps"));
	const FName AntiAliasingKey(TEXT("Graphics.AntiAliasing"));
	const FName FilteringKey(TEXT("Graphics.TextureFiltering"));
	const FName AmbientOcclusionKey(TEXT("Graphics.AmbientOcclusion"));

	/** Automatic render scale: the frame rates it can hold (0: off). */
	const int32 AutoScaleTargets[] = { 0, 30, 45, 60, 75, 90, 120, 144 };

	/** r.AntiAliasingMethod of each choice: TSR, TAA, FXAA, none. */
	const int32 AntiAliasingMethods[] = { 4, 2, 1, 0 };

	/** r.MaxAnisotropy of each choice. */
	const int32 Anisotropy[] = { 0, 2, 4, 8, 16 };

	/** The lowest the automatic render scale goes, percent. */
	constexpr float AutoScaleMinPercent = 50.0f;
}

void ScctGraphicsSettings::Register()
{
	// The overall quality, its groups and the render scale are the settings page's own (UGameUserSettings and
	// UScctUserSettings): only what it has no row for is registered here, on its Video tab.
	int32 Order = 0;
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.AutoScaleFps"), GraphicsPage, TEXT("Image"), TEXT("Automatic render scale"),
			TEXT("Lowers the 3D resolution, down to 50%, while the frame rate is under the chosen rate, and raises it again when there is room. The render scale on the Video tab is the highest it goes."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		for (const int32 Target : AutoScaleTargets)
		{
			Desc.Choices.Add(FText::FromString(Target == 0 ? FString(TEXT("Off")) : FString::Printf(TEXT("Hold %d FPS"), Target)));
		}
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.AntiAliasing"), GraphicsPage, TEXT("Image"), TEXT("Anti-aliasing"),
			TEXT("TSR is the sharpest and also upscales a lower render scale. TAA is cheaper, FXAA cheaper still and blurrier; Off leaves jagged edges."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("TSR"), TEXT("TAA"), TEXT("FXAA"), TEXT("Off") });
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.TextureFiltering"), GraphicsPage, TEXT("Image"), TEXT("Texture filtering"),
			TEXT("Keeps floors and walls seen at a slant sharp."), EScctSettingType::Choice, TEXT("3"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Trilinear (off)"), TEXT("2x anisotropic"), TEXT("4x anisotropic"), TEXT("8x anisotropic"), TEXT("16x anisotropic") });
		FScctSettings::Register(Desc);
	}
	FScctSettings::Register(Option(TEXT("Graphics.AmbientOcclusion"), GraphicsPage, TEXT("Image"), TEXT("Ambient occlusion"),
		TEXT("Contact shading in corners and under objects (screen-space and Lumen's short-range occlusion)."), EScctSettingType::Bool, TEXT("true"), Order++));
}

bool ScctGraphicsSettings::Owns(FName Key)
{
	return Key.ToString().StartsWith(TEXT("Graphics."));
}

void ScctGraphicsSettings::DetectFirstLaunch()
{
	if (GIsEditor || FApp::IsUnattended() || !FApp::CanEverRender() || !GIsRHIInitialized || GUsingNullRHI
		|| FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")))
	{
		return;
	}
	UGameUserSettings* Engine = GEngine->GetGameUserSettings();
	const TCHAR* Section = TEXT("OpenSCCT.Settings");
	int32 StoredValue;
	// Older versions have no detection flag. Either settings writer is evidence of an existing video profile.
	if (!Engine || GConfig->DoesSectionExist(TEXT("ScalabilityGroups"), GGameUserSettingsIni)
		|| GConfig->GetInt(Section, TEXT("RenderScale"), StoredValue, GGameUserSettingsIni)
		|| GConfig->GetInt(TEXT("/Script/Engine.GameUserSettings"), TEXT("Version"), StoredValue, GGameUserSettingsIni))
	{
		return;
	}

	// Use Unreal's short CPU/GPU benchmark and the installed scalability thresholds for each quality group.
	Engine->RunHardwareBenchmark(1);
	const bool bEntryIntel = GRHIAdapterName.Contains(TEXT("HD Graphics"))
		|| (GRHIAdapterName.Contains(TEXT("Iris")) && GRHIAdapterName.Contains(TEXT(" Xe")));
	// These older Intel families can score well on the synthetic passes. On other integrated adapters use
	// UE's GPU-only illumination tier to distinguish those with enough headroom for its full desktop profile.
	const bool bBudgetIntegrated = GRHIDeviceIsIntegrated && (bEntryIntel || Engine->GetGlobalIlluminationQuality() < 3);
	if (bBudgetIntegrated)
	{
		Engine->SetOverallScalabilityLevel(0);
	}
	const int32 Scale = bBudgetIntegrated ? 50 : FMath::Clamp(FMath::RoundToInt(Engine->ScalabilityQuality.ResolutionQuality), 50, 100);
	UScctUserSettings::Get()->RenderScale = Scale;
	GConfig->SetInt(Section, TEXT("RenderScale"), Scale, GGameUserSettingsIni);
	Engine->ApplyHardwareBenchmarkResults();
	SetCVar(TEXT("r.ScreenPercentage"), Scale);
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGraphics: first launch on %s (integrated %d, CPU %.1f, GPU %.1f), lighting %d, shadows %d, textures %d, render scale %d%%"),
		*GRHIAdapterName, GRHIDeviceIsIntegrated, Engine->GetLastCPUBenchmarkResult(), Engine->GetLastGPUBenchmarkResult(),
		Engine->GetGlobalIlluminationQuality(), Engine->GetShadowQuality(), Engine->GetTextureQuality(), Scale);
}

void ScctGraphicsSettings::Apply()
{
	UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr)
	{
		return;
	}

	// The render scale is UScctUserSettings' (r.ScreenPercentage, Menu/ScctUserSettings.h); the automatic one goes up to it.
	const float RenderScale = FMath::Clamp(UScctUserSettings::Get()->RenderScale / 100.0f, 0.5f, 1.0f);

	// Automatic render scale: dynamic resolution, as the game user settings ask for it, within a frame time budget.
	const int32 Target = AutoScaleTargets[FMath::Clamp(FScctSettings::GetInt(AutoScaleKey), 0, static_cast<int32>(UE_ARRAY_COUNT(AutoScaleTargets)) - 1)];
	SetCVar(TEXT("r.DynamicRes.OperationMode"), 1);
	SetCVar(TEXT("r.DynamicRes.MinScreenPercentage"), FMath::Min(AutoScaleMinPercent, RenderScale * 100.0f));
	SetCVar(TEXT("r.DynamicRes.MaxScreenPercentage"), RenderScale * 100.0f);
	if (Target > 0)
	{
		SetCVar(TEXT("r.DynamicRes.FrameTimeBudget"), 1000.0f / Target);
	}
	Settings->SetDynamicResolutionEnabled(Target > 0);

	Settings->ApplyNonResolutionSettings();

	// After the scalability groups, which set some of the same variables at a lower priority.
	const int32 Method = AntiAliasingMethods[FMath::Clamp(FScctSettings::GetInt(AntiAliasingKey), 0, 3)];
	SetCVar(TEXT("r.AntiAliasingMethod"), Method);
	const int32 MaxAnisotropy = Anisotropy[FMath::Clamp(FScctSettings::GetInt(FilteringKey), 0, 4)];
	SetCVar(TEXT("r.MaxAnisotropy"), MaxAnisotropy);
	const bool bAmbientOcclusion = FScctSettings::GetBool(AmbientOcclusionKey);
	SetCVar(TEXT("r.AmbientOcclusionLevels"), bAmbientOcclusion ? -1 : 0);
	SetCVar(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO"), bAmbientOcclusion ? 1 : 0);

	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGraphics: applied render scale %.0f%% (r.ScreenPercentage %s), automatic %s (dynamic resolution %s, budget %s ms); r.AntiAliasingMethod %d, r.MaxAnisotropy %d, ambient occlusion %s; sg.ShadowQuality %s sg.GlobalIlluminationQuality %s sg.PostProcessQuality %s"),
		RenderScale * 100.0f, *ScctVideoOptions::GetCVar(TEXT("r.ScreenPercentage")),
		Target > 0 ? *FString::Printf(TEXT("holds %d FPS"), Target) : TEXT("off"), Settings->IsDynamicResolutionEnabled() ? TEXT("on") : TEXT("off"),
		*ScctVideoOptions::GetCVar(TEXT("r.DynamicRes.FrameTimeBudget")), Method, MaxAnisotropy, bAmbientOcclusion ? TEXT("on") : TEXT("off"),
		*ScctVideoOptions::GetCVar(TEXT("sg.ShadowQuality")), *ScctVideoOptions::GetCVar(TEXT("sg.GlobalIlluminationQuality")),
		*ScctVideoOptions::GetCVar(TEXT("sg.PostProcessQuality")));
}
