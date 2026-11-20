#include "Settings/ScctGraphicsSettings.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "RHIGlobals.h"
#include "Settings/ScctAmdFsr.h"
#include "Settings/ScctNvidiaDlss.h"
#include "Settings/ScctVideoOptions.h"

using ScctVideoOptions::Option;
using ScctVideoOptions::SetCVar;

namespace
{
	const TCHAR* const GraphicsPage = TEXT("Graphics");

	const FName AutoScaleKey(TEXT("Graphics.AutoScaleFps"));
	const FName UpscalingKey(TEXT("Graphics.Upscaling"));
	const FName FrameGenerationKey(TEXT("Graphics.FrameGeneration"));
	const FName AntiAliasingKey(TEXT("Graphics.AntiAliasing"));
	const FName FilteringKey(TEXT("Graphics.TextureFiltering"));
	const FName AmbientOcclusionKey(TEXT("Graphics.AmbientOcclusion"));

	/** Automatic render scale: the frame rates it can hold (0: off). */
	const int32 AutoScaleTargets[] = { 0, 30, 45, 60, 75, 90, 120, 144 };

	/** r.AntiAliasingMethod of each choice: TSR, TAA, FXAA, none. */
	const int32 AntiAliasingMethods[] = { 4, 2, 1, 0 };
	constexpr int32 AutoAntiAliasing = UE_ARRAY_COUNT(AntiAliasingMethods);

	/** r.MaxAnisotropy of each choice. */
	const int32 Anisotropy[] = { 0, 2, 4, 8, 16 };

	/** The lowest the automatic render scale goes, percent. */
	constexpr float AutoScaleMinPercent = 50.0f;

	// Stored choice indices are shared by the menu and existing profiles, independent of the current GPU.
	using EDlssPreset = ScctNvidiaDlss::ESuperResolutionPreset;
	constexpr EDlssPreset DlssPresets[] = { EDlssPreset::Quality, EDlssPreset::Balanced, EDlssPreset::Performance };
	constexpr int32 FirstFsrChoice = 4;
	constexpr int32 FsrGenerationChoice = 2;
	constexpr int32 GenerationMultipliers[] = { 0, 2, 2, 3, 4, 5, 6 };

	bool SupportsUpscaling(int32 Choice)
	{
		if (Choice == 0) return true;
		if (Choice > 0 && Choice < FirstFsrChoice) return ScctNvidiaDlss::SupportsSuperResolutionPreset(DlssPresets[Choice - 1]);
		return Choice >= FirstFsrChoice && Choice <= 6 && ScctAmdFsr::IsUpscalingSupported();
	}

	bool SupportsGeneration(int32 Choice)
	{
		if (Choice == 0) return true;
		if (Choice == FsrGenerationChoice) return ScctAmdFsr::IsFrameGenerationSupported();
		return Choice > 0 && Choice < UE_ARRAY_COUNT(GenerationMultipliers)
			&& ScctNvidiaDlss::SupportsFrameGenerationMode(GenerationMultipliers[Choice]);
	}

	bool HasGenerationHardware(int32 Choice)
	{
		if (Choice == 0) return true;
		if (Choice == FsrGenerationChoice) return ScctAmdFsr::IsFrameGenerationHardwareSupported();
		return Choice > 0 && Choice < UE_ARRAY_COUNT(GenerationMultipliers)
			&& ScctNvidiaDlss::SupportsFrameGenerationHardwareMode(GenerationMultipliers[Choice]);
	}

	bool ApplyUpscaling(int32 Choice, FIntPoint Resolution, float& Scale)
	{
		// FSR restores the previous screen percentage when disabled. Release the outgoing provider first.
		const bool bFsr = Choice >= FirstFsrChoice && Choice <= 6;
		const bool bDlss = Choice > 0 && Choice < FirstFsrChoice;
		float DlssScale = 0.f;
		if (!bFsr) ScctAmdFsr::SetUpscaling(false, 0);
		if (!bDlss) ScctNvidiaDlss::SetSuperResolution(false, EDlssPreset::Quality, Resolution, DlssScale);
		if (bDlss && ScctNvidiaDlss::SetSuperResolution(true, DlssPresets[Choice - 1], Resolution, DlssScale))
		{
			Scale = DlssScale;
			return true;
		}
		if (bFsr && ScctAmdFsr::SetUpscaling(true, Choice - FirstFsrChoice + 1))
		{
			Scale = ScctAmdFsr::GetScreenPercentage(Choice - FirstFsrChoice + 1);
			return true;
		}
		// A saved preset can become unavailable after a device or driver change.
		if (bFsr) ScctAmdFsr::SetUpscaling(false, 0);
		if (bDlss) ScctNvidiaDlss::SetSuperResolution(false, EDlssPreset::Quality, Resolution, DlssScale);
		return false;
	}

	bool ApplyGeneration(int32 Choice)
	{
		const bool bAmd = Choice == FsrGenerationChoice;
		if (!bAmd) ScctAmdFsr::SetFrameGeneration(false);
		if (Choice == 0 || bAmd) ScctNvidiaDlss::SetFrameGeneration(0);
		if (Choice == 0) return true;
		if (!SupportsGeneration(Choice)) return false;
		return bAmd ? ScctAmdFsr::SetFrameGeneration(true)
			: ScctNvidiaDlss::SetFrameGeneration(GenerationMultipliers[Choice]);
	}
}

void ScctGraphicsSettings::Register()
{
	// The overall quality, its groups and the render scale are the settings page's own (UGameUserSettings and
	// UScctUserSettings): only what it has no row for is registered here, on its Video tab.
	int32 Order = 0;
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.Upscaling"), GraphicsPage, TEXT("Image"), TEXT("Upscaling"),
			TEXT("DLSS and FSR reconstruct the output from fewer rendered pixels. Quality keeps more fine detail. Balanced and Performance reduce GPU work further. These presets control render scale and anti-aliasing. Only supported modes are listed."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Use render scale"), TEXT("NVIDIA DLSS Quality"), TEXT("NVIDIA DLSS Balanced"), TEXT("NVIDIA DLSS Performance"),
			TEXT("AMD FSR Quality"), TEXT("AMD FSR Balanced"), TEXT("AMD FSR Performance") });
		Desc.SupportsChoice = SupportsUpscaling;
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.FrameGeneration"), GraphicsPage, TEXT("Image"), TEXT("Frame generation"),
			TEXT("Inserts frames between rendered frames for smoother motion. Input still runs at the rendered frame rate. Uses additional GPU memory and can add latency or artifacts. Restart the game to apply a mode marked after restart."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Off"), TEXT("NVIDIA DLSS 2x"), TEXT("AMD FSR 2x"), TEXT("NVIDIA DLSS 3x"),
			TEXT("NVIDIA DLSS 4x"), TEXT("NVIDIA DLSS 5x"), TEXT("NVIDIA DLSS 6x") });
		Desc.SupportsChoice = HasGenerationHardware;
		Desc.ChoiceRequiresRestart = [](int32 Choice) { return !SupportsGeneration(Choice); };
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.AutoScaleFps"), GraphicsPage, TEXT("Image"), TEXT("Automatic render scale"),
			TEXT("Adjusts the 3D resolution between 50% and your Render scale to target a frame rate when the GPU is busy. DLSS and FSR use their selected preset instead."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		for (const int32 Target : AutoScaleTargets)
		{
			Desc.Choices.Add(FText::FromString(Target == 0 ? FString(TEXT("Off")) : FString::Printf(TEXT("Hold %d FPS"), Target)));
		}
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.AntiAliasing"), GraphicsPage, TEXT("Image"), TEXT("Anti-aliasing"),
			TEXT("At native resolution, Auto uses FXAA on Low, TAA on Medium through Epic, and TSR on Cinematic. It uses TSR when render scale is reduced or automatic. DLSS and FSR provide their own anti-aliasing."),
			EScctSettingType::Choice, FString::FromInt(AutoAntiAliasing), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("TSR"), TEXT("TAA"), TEXT("FXAA"), TEXT("Off"), TEXT("Auto") });
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.TextureFiltering"), GraphicsPage, TEXT("Image"), TEXT("Texture filtering"),
			TEXT("Higher anisotropic filtering keeps distant floors and angled walls sharper. It uses more GPU texture bandwidth."), EScctSettingType::Choice, TEXT("3"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Trilinear (off)"), TEXT("2x anisotropic"), TEXT("4x anisotropic"), TEXT("8x anisotropic"), TEXT("16x anisotropic") });
		FScctSettings::Register(Desc);
	}
	FScctSettings::Register(Option(TEXT("Graphics.AmbientOcclusion"), GraphicsPage, TEXT("Image"), TEXT("Ambient occlusion"),
		TEXT("Adds fine shading where surfaces meet. Turning it off can save GPU time where the renderer uses ambient occlusion."), EScctSettingType::Bool, TEXT("true"), Order++));
}

bool ScctGraphicsSettings::Owns(FName Key)
{
	return Key.ToString().StartsWith(TEXT("Graphics."));
}

void ScctGraphicsSettings::DetectFirstLaunch()
{
	if (GIsEditor || FApp::IsUnattended() || !FApp::CanEverRender() || !GIsRHIInitialized || GUsingNullRHI)
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
	// 0 means "use the project's default" in sg.ResolutionQuality. OpenSCCT owns an explicit 50..100 slider.
	UScctUserSettings* User = UScctUserSettings::Get();
	const float Recommended = Engine->ScalabilityQuality.ResolutionQuality;
	int32 Scale = User->RenderScale;
	if (bBudgetIntegrated) { Scale = 50; }
	else if (FMath::IsFinite(Recommended) && Recommended > 0.f) { Scale = FMath::Clamp(FMath::RoundToInt(Recommended), 50, 100); }
	User->RenderScale = Scale;
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

	const float RenderScale = FMath::Clamp(UScctUserSettings::Get()->RenderScale / 100.0f, 0.5f, 1.0f);
	FIntPoint Resolution = Settings->GetScreenResolution();
	if (GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint ViewSize = GEngine->GameViewport->Viewport->GetSizeXY();
		if (ViewSize.X > 0 && ViewSize.Y > 0) Resolution = ViewSize;
	}
	float ScalePercent = RenderScale * 100.0f;
	const int32 Upscaler = FScctSettings::GetInt(UpscalingKey);
	const bool bVendorUpscaling = ApplyUpscaling(Upscaler, Resolution, ScalePercent);

	// Automatic render scale: dynamic resolution, as the game user settings ask for it, within a frame time budget.
	const int32 Target = bVendorUpscaling ? 0 : AutoScaleTargets[FMath::Clamp(FScctSettings::GetInt(AutoScaleKey), 0, static_cast<int32>(UE_ARRAY_COUNT(AutoScaleTargets)) - 1)];
	SetCVar(TEXT("r.DynamicRes.OperationMode"), 1);
	SetCVar(TEXT("r.DynamicRes.MinScreenPercentage"), FMath::Min(AutoScaleMinPercent, RenderScale * 100.0f));
	SetCVar(TEXT("r.DynamicRes.MaxScreenPercentage"), RenderScale * 100.0f);
	if (Target > 0)
	{
		SetCVar(TEXT("r.DynamicRes.FrameTimeBudget"), 1000.0f / Target);
	}
	Settings->SetDynamicResolutionEnabled(Target > 0);

	Settings->ApplyNonResolutionSettings();

	// The content pipeline assigns generated detail to a texture group with explicit mip sizes.
	const int32 TextureQuality = Settings->GetTextureQuality();
	SetCVar(TEXT("r.TextureGroup.QualityLevel"), TextureQuality < 4 ? TextureQuality : -1);

	// After the scalability groups, which set some of the same variables at a lower priority.
	const int32 Choice = FMath::Clamp(FScctSettings::GetInt(AntiAliasingKey), 0, AutoAntiAliasing);
	constexpr int32 NativeMethodByQuality[] = { 1, 2, 2, 2, 4 };
	const int32 Quality = FMath::Clamp(Settings->GetAntiAliasingQuality(), 0, static_cast<int32>(UE_ARRAY_COUNT(NativeMethodByQuality)) - 1);
	const int32 AutoMethod = Target == 0 && RenderScale >= 1.0f ? NativeMethodByQuality[Quality] : 4;
	const int32 Method = Choice == AutoAntiAliasing ? AutoMethod : AntiAliasingMethods[Choice];
	SetCVar(TEXT("r.AntiAliasingMethod"), bVendorUpscaling ? 2 : Method);
	// AMD sets its own screen percentage at vendor priority and restores the previous value when disabled.
	if (!bVendorUpscaling || Upscaler < FirstFsrChoice) SetCVar(TEXT("r.ScreenPercentage"), ScalePercent);
	const int32 Generation = FScctSettings::GetInt(FrameGenerationKey);
	const bool bGenerationApplied = ApplyGeneration(Generation);
	if (!bGenerationApplied) ApplyGeneration(0);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGraphics: upscaling %d (requested %d), frame generation %d (requested %d), output %dx%d"),
		bVendorUpscaling ? Upscaler : 0, Upscaler, bGenerationApplied ? Generation : 0, Generation, Resolution.X, Resolution.Y);
	const int32 MaxAnisotropy = Anisotropy[FMath::Clamp(FScctSettings::GetInt(FilteringKey), 0, 4)];
	SetCVar(TEXT("r.MaxAnisotropy"), MaxAnisotropy);
	const bool bAmbientOcclusion = FScctSettings::GetBool(AmbientOcclusionKey);
	SetCVar(TEXT("r.AmbientOcclusionLevels"), bAmbientOcclusion ? -1 : 0);
	SetCVar(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO"), bAmbientOcclusion ? 1 : 0);

	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGraphics: applied render scale %.0f%% (r.ScreenPercentage %s), automatic %s (dynamic resolution %s, budget %s ms); r.AntiAliasingMethod %s, r.MaxAnisotropy %d, ambient occlusion %s; sg.ShadowQuality %s sg.GlobalIlluminationQuality %s sg.PostProcessQuality %s"),
		RenderScale * 100.0f, *ScctVideoOptions::GetCVar(TEXT("r.ScreenPercentage")),
		Target > 0 ? *FString::Printf(TEXT("holds %d FPS"), Target) : TEXT("off"), Settings->IsDynamicResolutionEnabled() ? TEXT("on") : TEXT("off"),
		*ScctVideoOptions::GetCVar(TEXT("r.DynamicRes.FrameTimeBudget")), *ScctVideoOptions::GetCVar(TEXT("r.AntiAliasingMethod")), MaxAnisotropy, bAmbientOcclusion ? TEXT("on") : TEXT("off"),
		*ScctVideoOptions::GetCVar(TEXT("sg.ShadowQuality")), *ScctVideoOptions::GetCVar(TEXT("sg.GlobalIlluminationQuality")),
		*ScctVideoOptions::GetCVar(TEXT("sg.PostProcessQuality")));
}
