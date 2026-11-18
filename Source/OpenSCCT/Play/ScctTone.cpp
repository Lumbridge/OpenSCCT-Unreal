#include "Play/ScctTone.h"

#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Menu/ScctUserSettings.h"
#include "OpenSCCT.h"
#include "Play/ScctLightPlan.h"

namespace
{
	const TCHAR* const ToneMaterialPath = TEXT("/Game/OpenSCCT/Materials/M_ScctTone.M_ScctTone");

	TAutoConsoleVariable<int32> CVarLightTone(TEXT("scct.Light.Tone"), 1,
		TEXT("With scct.Light.Plan 1: 1 the plan's tone curve (the original's display values, its black gate, a shoulder above 200 of 255); ")
		TEXT("0 the engine's film curve at the fixed exposure, with the level's own bloom and lens flares."));
	TAutoConsoleVariable<float> CVarLightToneGate(TEXT("scct.Light.ToneGate"), 1.0f,
		TEXT("With the plan's tone curve: how much of the original's glow gate is in the picture (frame x saturate(16 (L - 0.03)), which draws ")
		TEXT("what the original shows under 8 of 255 as black and is fully open from 24). 1 the original's, 0 none."));
	TAutoConsoleVariable<float> CVarLightToneKnee(TEXT("scct.Light.ToneKnee"), 200.0f,
		TEXT("With the plan's tone curve: the original display value (of 255) up to which the picture shows it exactly; above, a shoulder ")
		TEXT("rolls the overbright range the original clipped off towards white. 254 is nearly the original's hard clip."));
	TAutoConsoleVariable<float> CVarLightToneGain(TEXT("scct.Light.ToneGain"), 1.0f,
		TEXT("With the plan's tone curve: a correction from the ramp solve (Tools/Light/solve_tone.py), display value x this. 1 when the ramp is met."));
	TAutoConsoleVariable<float> CVarLightToneGamma(TEXT("scct.Light.ToneGamma"), 1.0f,
		TEXT("With the plan's tone curve: a correction from the ramp solve, display value (0..1) to this power. 1 when the ramp is met."));
	TAutoConsoleVariable<float> CVarLightBloomThreshold(TEXT("scct.Light.BloomThreshold"), -1.0f,
		TEXT("With the plan's tone curve: the display value (of 255) above which the picture blooms. -1 takes the map's LevelInfo.GlowThreshold ")
		TEXT("(224 on most maps; 255 is no bloom at all, as the original)."));
	TAutoConsoleVariable<float> CVarLightBloomIntensity(TEXT("scct.Light.BloomIntensity"), 8.0f,
		TEXT("With the plan's tone curve: the engine's bloom intensity for what passes the threshold (a look: not measured against the original's glow)."));
	TAutoConsoleVariable<int32> CVarLightLensFlares(TEXT("scct.Light.LensFlares"), 0,
		TEXT("With the plan's tone curve: 0 no lens flares (their ghosts put up to 10 of 255 on black parts of the picture), 1 the level's own."));

	TAutoConsoleVariable<int32> CVarLightVignette(TEXT("scct.Light.Vignette"), 0,
		TEXT("With the plan's tone curve: 0 no vignette, so a display value is the same anywhere on the screen as in the original (the level's ")
		TEXT("vignette took 10 of 255 off the original's 200 a quarter of the screen from the middle); 1 the level's own."));

	// The original's composite: saturate(16 (L - 0.03)).
	constexpr float ToneGateSlope = 16.0f;
	constexpr float ToneGateFloor = 0.03f;

	// LevelInfo.GlowThreshold's class default, for a level whose value has not arrived.
	constexpr int32 ToneDefaultGlow = 224;

	constexpr float TonePlanGammaMin = 2.0f;
	constexpr float TonePlanGammaMax = 2.4f;
	constexpr float ToneFreeGammaMin = 1.6f;
	constexpr float ToneFreeGammaMax = 3.0f;

	const FName ToneExposureParameter(TEXT("Exposure"));
	const FName ToneGainParameter(TEXT("Gain"));
	const FName ToneGammaParameter(TEXT("Gamma"));
	const FName ToneKneeParameter(TEXT("Knee"));
	const FName ToneGateParameter(TEXT("Gate"));
	const FName ToneGateSlopeParameter(TEXT("GateSlope"));
	const FName ToneGateFloorParameter(TEXT("GateFloor"));

	struct FToneState
	{
		TWeakObjectPtr<APostProcessVolume> Volume;
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		int32 Blendable = INDEX_NONE;
		bool bMissing = false;
		bool bOn = false;
		bool bHeldGamma = false;
		// The level's GlowThreshold as last read (the level's row is not there on every frame while a match starts).
		int32 LevelGlow = ToneDefaultGlow;
		// What the last log line said, so a change is logged once.
		int32 LoggedGlow = -2;
		int32 LoggedOn = -1;
	};
	FToneState ToneHeld;

	/** The light the engine's tone pass must get to show a display value (0..1): the sRGB decoding. */
	float ToneLightOf(float Display)
	{
		return Display <= 0.04045f ? Display / 12.92f : FMath::Pow((Display + 0.055f) / 1.055f, 2.4f);
	}

	UMaterialInstanceDynamic* ToneEnsureMaterial(APostProcessVolume* Volume)
	{
		if (ToneHeld.Volume.Get() == Volume)
		{
			return ToneHeld.Material.Get();
		}
		const bool bHeldGamma = ToneHeld.bHeldGamma;
		const bool bWasMissing = ToneHeld.bMissing;
		ToneHeld = FToneState();
		ToneHeld.bHeldGamma = bHeldGamma;
		ToneHeld.bMissing = bWasMissing;
		ToneHeld.Volume = Volume;
		UMaterialInterface* Base = ToneHeld.bMissing ? nullptr : LoadObject<UMaterialInterface>(nullptr, ToneMaterialPath);
		if (Base == nullptr)
		{
			if (!ToneHeld.bMissing)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Tone: %s is missing; run Tools/Materials/Build-ToneMaterial.ps1. The plan's picture keeps the engine's film curve."), ToneMaterialPath);
			}
			ToneHeld.bMissing = true;
			return nullptr;
		}
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, Volume);
		ToneHeld.Material = Instance;
		// Weight 0 keeps the material out of the frame until the curve is on.
		ToneHeld.Blendable = Volume->Settings.WeightedBlendables.Array.Emplace(0.0f, Instance);
		return Instance;
	}
}

void FScctTone::Apply(UWorld* World, APostProcessVolume* Volume, bool bPlan, float ExposureScale)
{
	if (!bPlan || !IsValid(Volume) || World == nullptr)
	{
		// The released picture: nothing of this file touches the volume. A display gamma held for a plan level before
		// this one goes back to the setting's.
		ToneHeld.bOn = false;
		Leave();
		return;
	}

	// The brightness setting, held to the plan's range.
	if (GEngine != nullptr)
	{
		const float Gamma = DisplayGamma(UScctUserSettings::Get()->Gamma);
		if (!FMath::IsNearlyEqual(GEngine->DisplayGamma, Gamma, 0.0005f))
		{
			GEngine->DisplayGamma = Gamma;
		}
		ToneHeld.bHeldGamma = true;
	}

	UMaterialInstanceDynamic* Material = ToneEnsureMaterial(Volume);
	const bool bOn = Material != nullptr && CVarLightTone.GetValueOnGameThread() != 0;
	ToneHeld.bOn = bOn;
	FPostProcessSettings& Settings = Volume->Settings;
	if (Volume->Settings.WeightedBlendables.Array.IsValidIndex(ToneHeld.Blendable))
	{
		Volume->Settings.WeightedBlendables.Array[ToneHeld.Blendable].Weight = bOn ? 1.0f : 0.0f;
	}

	// The engine's film curve off: its tone pass is then light x exposure, clipped at 1 and sRGB encoded, and the
	// material has already shaped the light. No gamut expansion either (it pushes bright saturated colours outward).
	Settings.bOverride_ToneCurveAmount = bOn;
	Settings.ToneCurveAmount = 0.0f;
	Settings.bOverride_ExpandGamut = bOn;
	Settings.ExpandGamut = 0.0f;
	Settings.bOverride_BlueCorrection = bOn;
	Settings.BlueCorrection = 0.0f;

	const bool bNoFlares = bOn && CVarLightLensFlares.GetValueOnGameThread() == 0;
	Settings.bOverride_LensFlareIntensity = bNoFlares;
	Settings.LensFlareIntensity = 0.0f;

	const bool bNoVignette = bOn && CVarLightVignette.GetValueOnGameThread() == 0;
	Settings.bOverride_VignetteIntensity = bNoVignette;
	Settings.VignetteIntensity = 0.0f;

	// The bloom's bright pass. The original keeps display luminance above GlowThreshold / 255; the engine keeps light
	// above a threshold, read after the material, where light is the sRGB decoding of the display value.
	const UScctVisionSubsystem* Vision = UScctVisionSubsystem::Find(World);
	const float Asked = CVarLightBloomThreshold.GetValueOnGameThread();
	if (Vision != nullptr && Vision->GetData().HasLevel())
	{
		ToneHeld.LevelGlow = Vision->GetData().GetLevel().GlowThreshold;
	}
	const int32 Glow = Asked >= 0.0f ? FMath::RoundToInt32(Asked) : ToneHeld.LevelGlow;
	const float Intensity = Glow >= 255 ? 0.0f : FMath::Max(CVarLightBloomIntensity.GetValueOnGameThread(), 0.0f);
	Settings.bOverride_BloomThreshold = bOn;
	Settings.BloomThreshold = ToneLightOf(FMath::Clamp(Glow / 255.0f, 0.0f, 1.0f));
	Settings.bOverride_BloomIntensity = bOn;
	Settings.BloomIntensity = Intensity;

	const float Knee = FMath::Clamp(CVarLightToneKnee.GetValueOnGameThread(), 16.0f, 254.0f) / 255.0f;
	// Goggles read the scene before the composite that gates: the gate goes out as a mode comes in.
	const float Shown = Vision != nullptr ? FMath::Clamp(Vision->GetShownAmount(), 0.0f, 1.0f) : 0.0f;
	const float Gate = FMath::Clamp(CVarLightToneGate.GetValueOnGameThread(), 0.0f, 1.0f) * (1.0f - Shown);
	if (Material != nullptr)
	{
		Material->SetScalarParameterValue(ToneExposureParameter, FMath::Max(ExposureScale, 0.0001f));
		Material->SetScalarParameterValue(ToneGainParameter, FMath::Clamp(CVarLightToneGain.GetValueOnGameThread(), 0.25f, 4.0f));
		Material->SetScalarParameterValue(ToneGammaParameter, FMath::Clamp(CVarLightToneGamma.GetValueOnGameThread(), 0.5f, 2.0f));
		Material->SetScalarParameterValue(ToneKneeParameter, Knee);
		Material->SetScalarParameterValue(ToneGateParameter, Gate);
		Material->SetScalarParameterValue(ToneGateSlopeParameter, ToneGateSlope);
		Material->SetScalarParameterValue(ToneGateFloorParameter, ToneGateFloor);
	}

	if (ToneHeld.LoggedOn != (bOn ? 1 : 0) || (bOn && ToneHeld.LoggedGlow != Glow))
	{
		ToneHeld.LoggedOn = bOn ? 1 : 0;
		ToneHeld.LoggedGlow = Glow;
		if (bOn)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Tone: the plan's curve in %s: exposure %.3f, knee %.0f, gate %.2f, gain %.3f, gamma %.3f; bloom above %d of 255 (light %.3f) at intensity %.1f; lens flares %s; vignette %s; display gamma %.2f"),
				*World->GetName(), ExposureScale, Knee * 255.0f, CVarLightToneGate.GetValueOnGameThread(), CVarLightToneGain.GetValueOnGameThread(),
				CVarLightToneGamma.GetValueOnGameThread(), Glow, Settings.BloomThreshold, Intensity, bNoFlares ? TEXT("off") : TEXT("the level's"), bNoVignette ? TEXT("off") : TEXT("the level's"),
				GEngine != nullptr ? GEngine->DisplayGamma : 0.0f);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Tone: the engine's film curve in %s (%s)."), *World->GetName(),
				Material == nullptr ? TEXT("the tone material is missing") : TEXT("scct.Light.Tone 0"));
		}
	}
}

void FScctTone::Leave()
{
	if (ToneHeld.bHeldGamma)
	{
		ToneHeld.bHeldGamma = false;
		if (GEngine != nullptr)
		{
			GEngine->DisplayGamma = DisplayGamma(UScctUserSettings::Get()->Gamma);
		}
	}
}

bool FScctTone::IsOn()
{
	return ToneHeld.bOn && FScctLightPlan::IsOn();
}

float FScctTone::GammaMin()
{
	return FScctLightPlan::Requested() > 0 ? TonePlanGammaMin : ToneFreeGammaMin;
}

float FScctTone::GammaMax()
{
	return FScctLightPlan::Requested() > 0 ? TonePlanGammaMax : ToneFreeGammaMax;
}

float FScctTone::DisplayGamma(float Setting)
{
	return FScctLightPlan::Requested() > 0 ? FMath::Clamp(Setting, TonePlanGammaMin, TonePlanGammaMax) : Setting;
}
