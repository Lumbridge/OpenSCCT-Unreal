#include "Menu/ScctUserSettings.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* const Section = TEXT("OpenSCCT.Settings");

	void SetVariable(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}
}

UScctUserSettings* UScctUserSettings::Get()
{
	UScctUserSettings* Settings = GetMutableDefault<UScctUserSettings>();
	if (!Settings->bLoaded)
	{
		Settings->bLoaded = true;
		Settings->Load();
	}
	return Settings;
}

void UScctUserSettings::Load()
{
	const FString& Ini = GGameUserSettingsIni;
	GConfig->GetInt(Section, TEXT("MasterVolume"), MasterVolume, Ini);
	GConfig->GetInt(Section, TEXT("EffectsVolume"), EffectsVolume, Ini);
	GConfig->GetInt(Section, TEXT("AmbientVolume"), AmbientVolume, Ini);
	GConfig->GetInt(Section, TEXT("MusicVolume"), MusicVolume, Ini);
	GConfig->GetInt(Section, TEXT("MenuMusicVolume"), MenuMusicVolume, Ini);
	GConfig->GetInt(Section, TEXT("MenuSoundsVolume"), MenuSoundsVolume, Ini);
	GConfig->GetInt(Section, TEXT("ObjectiveVolume"), ObjectiveVolume, Ini);
	GConfig->GetInt(Section, TEXT("VoiceVolume"), VoiceVolume, Ini);
	GConfig->GetInt(Section, TEXT("HubJukeboxVolume"), HubJukeboxVolume, Ini);
	GConfig->GetInt(Section, TEXT("HubChimesVolume"), HubChimesVolume, Ini);
	GConfig->GetBool(Section, TEXT("MotionBlur"), bMotionBlur, Ini);
	GConfig->GetFloat(Section, TEXT("Gamma"), Gamma, Ini);
	GConfig->GetInt(Section, TEXT("RenderScale"), RenderScale, Ini);
	GConfig->GetBool(Section, TEXT("DistanceInFeet"), bDistanceInFeet, Ini);

	MasterVolume = FMath::Clamp(MasterVolume, 0, 100);
	EffectsVolume = FMath::Clamp(EffectsVolume, 0, 100);
	AmbientVolume = FMath::Clamp(AmbientVolume, 0, 100);
	MusicVolume = FMath::Clamp(MusicVolume, 0, 100);
	MenuMusicVolume = FMath::Clamp(MenuMusicVolume, 0, 100);
	MenuSoundsVolume = FMath::Clamp(MenuSoundsVolume, 0, 100);
	ObjectiveVolume = FMath::Clamp(ObjectiveVolume, 0, 100);
	VoiceVolume = FMath::Clamp(VoiceVolume, 0, 100);
	HubJukeboxVolume = FMath::Clamp(HubJukeboxVolume, 0, 100);
	HubChimesVolume = FMath::Clamp(HubChimesVolume, 0, 100);
	Gamma = FMath::Clamp(Gamma, 1.6f, 3.0f);
	RenderScale = FMath::Clamp(RenderScale, 50, 100);
}

void UScctUserSettings::Save()
{
	const FString& Ini = GGameUserSettingsIni;
	GConfig->SetInt(Section, TEXT("MasterVolume"), MasterVolume, Ini);
	GConfig->SetInt(Section, TEXT("EffectsVolume"), EffectsVolume, Ini);
	GConfig->SetInt(Section, TEXT("AmbientVolume"), AmbientVolume, Ini);
	GConfig->SetInt(Section, TEXT("MusicVolume"), MusicVolume, Ini);
	GConfig->SetInt(Section, TEXT("MenuMusicVolume"), MenuMusicVolume, Ini);
	GConfig->SetInt(Section, TEXT("MenuSoundsVolume"), MenuSoundsVolume, Ini);
	GConfig->SetInt(Section, TEXT("ObjectiveVolume"), ObjectiveVolume, Ini);
	GConfig->SetInt(Section, TEXT("VoiceVolume"), VoiceVolume, Ini);
	GConfig->SetInt(Section, TEXT("HubJukeboxVolume"), HubJukeboxVolume, Ini);
	GConfig->SetInt(Section, TEXT("HubChimesVolume"), HubChimesVolume, Ini);
	GConfig->SetBool(Section, TEXT("MotionBlur"), bMotionBlur, Ini);
	GConfig->SetFloat(Section, TEXT("Gamma"), Gamma, Ini);
	GConfig->SetInt(Section, TEXT("RenderScale"), RenderScale, Ini);
	GConfig->SetBool(Section, TEXT("DistanceInFeet"), bDistanceInFeet, Ini);
	if (UGameUserSettings* Engine = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr)
	{
		// Writes its own section and flushes the file, ours with it.
		Engine->SaveSettings();
	}
	GConfig->Flush(false, Ini);
	LogValues(TEXT("saved"));
}

void UScctUserSettings::ApplyAll()
{
	FApp::SetVolumeMultiplier(MasterVolume / 100.0f);
	++AudioRevision;

	// As game settings: a console variable given on the command line or typed in the console still wins.
	SetVariable(TEXT("r.MotionBlurQuality"), bMotionBlur ? 4.0f : 0.0f);
	SetVariable(TEXT("r.ScreenPercentage"), static_cast<float>(RenderScale));
	if (GEngine != nullptr)
	{
		GEngine->DisplayGamma = Gamma;
	}
	if (const FScctHudApi* Hud = ScctFeature<FScctHudApi>())
	{
		Hud->SetOption(EScctHudOption::DistanceInFeet, bDistanceInFeet ? 1 : 0);
	}
	OnApplied.Broadcast();
}

void UScctUserSettings::ResetVideo()
{
	bMotionBlur = true;
	Gamma = 2.2f;
	RenderScale = 100;
	if (UGameUserSettings* Engine = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr)
	{
		Engine->SetOverallScalabilityLevel(3);
		Engine->SetVSyncEnabled(false);
		Engine->SetFrameRateLimit(0.0f);
	}
}

void UScctUserSettings::ResetAudio()
{
	MasterVolume = 100;
	EffectsVolume = 80;
	AmbientVolume = 80;
	MusicVolume = 70;
	MenuMusicVolume = 5;
	MenuSoundsVolume = 5;
	ObjectiveVolume = 80;
	VoiceVolume = 100;
	HubJukeboxVolume = 100;
	HubChimesVolume = 100;
}

void UScctUserSettings::ResetInterface()
{
	bDistanceInFeet = false;
}

float UScctUserSettings::GetGain(EScctSoundKind Kind) const
{
	switch (Kind)
	{
	case EScctSoundKind::Effects: return EffectsVolume / 100.0f;
	case EScctSoundKind::Ambient: return AmbientVolume / 100.0f;
	case EScctSoundKind::Music: return MusicVolume / 100.0f;
	case EScctSoundKind::MenuMusic: return MenuMusicVolume / 100.0f;
	case EScctSoundKind::Objective: return ObjectiveVolume / 100.0f;
	case EScctSoundKind::Voice: return VoiceVolume / 100.0f;
	case EScctSoundKind::HubJukebox: return HubJukeboxVolume / 100.0f;
	case EScctSoundKind::HubChimes: return HubChimesVolume / 100.0f;
	default: return MenuSoundsVolume / 100.0f;
	}
}

void UScctUserSettings::LogValues(const TCHAR* Why) const
{
	const UGameUserSettings* Engine = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	UE_LOG(LogOpenSCCT, Log, TEXT("Settings %s: volumes master %d effects %d ambient %d music %d menu music %d menu sounds %d objective %d voice %d hub jukebox %d hub chimes %d; motion blur %d, gamma %.2f, render scale %d, feet %d"),
		Why, MasterVolume, EffectsVolume, AmbientVolume, MusicVolume, MenuMusicVolume, MenuSoundsVolume, ObjectiveVolume, VoiceVolume, HubJukeboxVolume, HubChimesVolume, bMotionBlur ? 1 : 0, Gamma, RenderScale, bDistanceInFeet ? 1 : 0);
	if (Engine != nullptr)
	{
		const FIntPoint Size = Engine->GetScreenResolution();
		UE_LOG(LogOpenSCCT, Log, TEXT("Settings %s: %dx%d window mode %d, vsync %d, frame limit %.0f, quality overall %d (shadows %d, lighting %d, reflections %d, post %d, textures %d, effects %d, anti-aliasing %d, view distance %d)"),
			Why, Size.X, Size.Y, static_cast<int32>(Engine->GetFullscreenMode()), Engine->IsVSyncEnabled() ? 1 : 0, Engine->GetFrameRateLimit(),
			Engine->GetOverallScalabilityLevel(), Engine->GetShadowQuality(), Engine->GetGlobalIlluminationQuality(), Engine->GetReflectionQuality(),
			Engine->GetPostProcessingQuality(), Engine->GetTextureQuality(), Engine->GetVisualEffectQuality(), Engine->GetAntiAliasingQuality(), Engine->GetViewDistanceQuality());
	}
}
