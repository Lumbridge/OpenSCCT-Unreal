#include "Settings/ScctGraphicsSettings.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Settings/ScctVideoOptions.h"

using ScctVideoOptions::Option;
using ScctVideoOptions::SetCVar;

namespace
{
	const TCHAR* const Page = TEXT("Graphics");

	const FName QualityKey(TEXT("Graphics.Quality"));
	const FName RenderScaleKey(TEXT("Graphics.RenderScale"));
	const FName AutoScaleKey(TEXT("Graphics.AutoScaleFps"));
	const FName AntiAliasingKey(TEXT("Graphics.AntiAliasing"));
	const FName FilteringKey(TEXT("Graphics.TextureFiltering"));
	const FName AmbientOcclusionKey(TEXT("Graphics.AmbientOcclusion"));

	/** Unreal's scalability levels, as the overall quality lists them (the stored value is the level). */
	const TArray<const TCHAR*> Levels = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };

	/** A scalability group the player can set apart from the overall quality. */
	struct FGroup
	{
		const TCHAR* Key;
		const TCHAR* Label;
		const TCHAR* Tooltip;
		void (UGameUserSettings::*Set)(int32);
	};

	const FGroup Groups[] = {
		{ TEXT("Graphics.ViewDistance"), TEXT("View distance"), TEXT("How far away small things are still drawn."), &UGameUserSettings::SetViewDistanceQuality },
		{ TEXT("Graphics.Shadows"), TEXT("Shadows"), TEXT("The virtual shadow maps' resolution and how many lights cast shadows. Sharp shadows show where a spy can hide."), &UGameUserSettings::SetShadowQuality },
		{ TEXT("Graphics.GlobalIllumination"), TEXT("Lighting"), TEXT("Lumen's bounced light and its ambient occlusion: how light fills a room around the lamps."), &UGameUserSettings::SetGlobalIlluminationQuality },
		{ TEXT("Graphics.Reflections"), TEXT("Reflections"), TEXT("Lumen's reflections on glass, water and polished floors."), &UGameUserSettings::SetReflectionQuality },
		{ TEXT("Graphics.PostProcess"), TEXT("Post-processing"), TEXT("Bloom, motion blur, depth of field and the other passes over the finished picture."), &UGameUserSettings::SetPostProcessingQuality },
		{ TEXT("Graphics.Textures"), TEXT("Textures"), TEXT("Texture resolution kept in video memory."), &UGameUserSettings::SetTextureQuality },
		{ TEXT("Graphics.Effects"), TEXT("Effects"), TEXT("Particles, smoke and other effects."), &UGameUserSettings::SetVisualEffectQuality },
		{ TEXT("Graphics.Shading"), TEXT("Shading"), TEXT("The detail of the materials' lighting."), &UGameUserSettings::SetShadingQuality },
	};

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
	int32 Order = 0;
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.Quality"), Page, TEXT("Quality"), TEXT("Overall quality"),
			TEXT("Sets every group below that is on \"Same as overall\". Lower is faster."), EScctSettingType::Choice, TEXT("3"), Order++);
		for (const TCHAR* Level : Levels)
		{
			Desc.Choices.Add(FText::FromString(Level));
		}
		FScctSettings::Register(Desc);
	}
	for (const FGroup& Group : Groups)
	{
		FScctSettingDesc Desc = Option(Group.Key, Page, TEXT("Quality"), Group.Label, Group.Tooltip, EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices.Add(FText::FromString(TEXT("Same as overall")));
		for (const TCHAR* Level : Levels)
		{
			Desc.Choices.Add(FText::FromString(Level));
		}
		FScctSettings::Register(Desc);
	}

	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.RenderScale"), Page, TEXT("Image"), TEXT("Render scale"),
			TEXT("The 3D picture's resolution against the screen's. Lower is faster and softer; the HUD and menus stay sharp."),
			EScctSettingType::Float, TEXT("1.0"), Order++);
		Desc.Min = 0.5f;
		Desc.Max = 1.0f;
		Desc.Step = 0.05f;
		Desc.FormatValue = [](float Value) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f)); };
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.AutoScaleFps"), Page, TEXT("Image"), TEXT("Automatic render scale"),
			TEXT("Lowers the 3D resolution, down to 50%, while the frame rate is under the chosen rate, and raises it again when there is room. Render scale is the highest it goes."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		for (const int32 Target : AutoScaleTargets)
		{
			Desc.Choices.Add(FText::FromString(Target == 0 ? FString(TEXT("Off")) : FString::Printf(TEXT("Hold %d FPS"), Target)));
		}
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.AntiAliasing"), Page, TEXT("Image"), TEXT("Anti-aliasing"),
			TEXT("TSR is the sharpest and also upscales a lower render scale. TAA is cheaper, FXAA cheaper still and blurrier; Off leaves jagged edges."),
			EScctSettingType::Choice, TEXT("0"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("TSR"), TEXT("TAA"), TEXT("FXAA"), TEXT("Off") });
		FScctSettings::Register(Desc);
	}
	{
		FScctSettingDesc Desc = Option(TEXT("Graphics.TextureFiltering"), Page, TEXT("Image"), TEXT("Texture filtering"),
			TEXT("Keeps floors and walls seen at a slant sharp."), EScctSettingType::Choice, TEXT("3"), Order++);
		Desc.Choices = ScctVideoOptions::Texts({ TEXT("Trilinear (off)"), TEXT("2x anisotropic"), TEXT("4x anisotropic"), TEXT("8x anisotropic"), TEXT("16x anisotropic") });
		FScctSettings::Register(Desc);
	}
	FScctSettings::Register(Option(TEXT("Graphics.AmbientOcclusion"), Page, TEXT("Image"), TEXT("Ambient occlusion"),
		TEXT("Contact shading in corners and under objects (screen-space and Lumen's short-range occlusion)."), EScctSettingType::Bool, TEXT("true"), Order++));
}

bool ScctGraphicsSettings::Owns(FName Key)
{
	return Key.ToString().StartsWith(TEXT("Graphics."));
}

void ScctGraphicsSettings::Apply()
{
	UGameUserSettings* Settings = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr)
	{
		return;
	}

	const int32 Overall = FMath::Clamp(FScctSettings::GetInt(QualityKey), 0, Levels.Num() - 1);
	Settings->SetOverallScalabilityLevel(Overall);
	FString GroupText;
	for (const FGroup& Group : Groups)
	{
		// 0 follows the overall level; 1 to 5 are Low to Cinematic.
		const int32 Choice = FScctSettings::GetInt(Group.Key);
		const int32 Level = Choice <= 0 ? Overall : FMath::Clamp(Choice - 1, 0, Levels.Num() - 1);
		(Settings->*Group.Set)(Level);
		GroupText += FString::Printf(TEXT(" %s=%d"), Group.Key + 9, Level);
	}

	// The render scale is the scalability resolution (r.ScreenPercentage); the overall level would otherwise set its own.
	const float RenderScale = FMath::Clamp(FScctSettings::GetFloat(RenderScaleKey), 0.5f, 1.0f);
	Settings->SetResolutionScaleValueEx(RenderScale * 100.0f);

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

	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGraphics: applied overall %d (%s),%s; render scale %.0f%% (r.ScreenPercentage %s), automatic %s (dynamic resolution %s, budget %s ms); r.AntiAliasingMethod %d, r.MaxAnisotropy %d, ambient occlusion %s; sg.ShadowQuality %s sg.GlobalIlluminationQuality %s sg.PostProcessQuality %s"),
		Overall, Levels[Overall], *GroupText, RenderScale * 100.0f, *ScctVideoOptions::GetCVar(TEXT("r.ScreenPercentage")),
		Target > 0 ? *FString::Printf(TEXT("holds %d FPS"), Target) : TEXT("off"), Settings->IsDynamicResolutionEnabled() ? TEXT("on") : TEXT("off"),
		*ScctVideoOptions::GetCVar(TEXT("r.DynamicRes.FrameTimeBudget")), Method, MaxAnisotropy, bAmbientOcclusion ? TEXT("on") : TEXT("off"),
		*ScctVideoOptions::GetCVar(TEXT("sg.ShadowQuality")), *ScctVideoOptions::GetCVar(TEXT("sg.GlobalIlluminationQuality")),
		*ScctVideoOptions::GetCVar(TEXT("sg.PostProcessQuality")));
}
