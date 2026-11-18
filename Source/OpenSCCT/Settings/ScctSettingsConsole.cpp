#include "Settings/ScctSettingsConsole.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/App.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Settings/ScctDisplaySettings.h"
#include "Settings/ScctGraphicsSettings.h"
#include "Settings/ScctPerformanceSettings.h"
#include "Settings/ScctSettings.h"

namespace
{
	/** A setting the registry does not hold: its owner's getter and setter, behind a registry-like description. */
	struct FScctConsoleOwned
	{
		FString Key;
		EScctSettingType Type = EScctSettingType::Bool;
		float Min = 0.0f;
		float Max = 1.0f;
		TArray<FString> Choices;
		/** A few words on what the value means ("0 = unlimited"), for Describe. */
		FString Note;
		/** The value as Set would take it (a choice by its index). */
		TFunction<FString()> Get;
		/** Takes a value already checked and clamped (a choice as its index, a switch as true/false). */
		TFunction<void(const FString&)> Set;
	};

	UGameUserSettings* ScctConsoleEngineSettings()
	{
		return GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	}

	/** What the settings page does after a change of UGameUserSettings' quality, vertical sync or frame limit. */
	void ScctConsoleApplyQuality()
	{
		if (UGameUserSettings* Engine = ScctConsoleEngineSettings())
		{
			Engine->ApplyNonResolutionSettings();
			Engine->SaveSettings();
		}
		// The scalability groups set some of the variables the registry's graphics options set: those go again on top.
		ScctGraphicsSettings::Apply();
	}

	/** The window mode and resolution, kept at once (the console has no 15 second question); unattended runs keep the window. */
	void ScctConsoleApplyResolution()
	{
		UGameUserSettings* Engine = ScctConsoleEngineSettings();
		if (Engine == nullptr)
		{
			return;
		}
		UScctDisplaySettings::ApplyResolution(*Engine);
		if (FApp::IsUnattended())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctSettingsConsole: display %dx%d mode %d kept for the next run (unattended: the window is not changed)"),
				Engine->GetScreenResolution().X, Engine->GetScreenResolution().Y, static_cast<int32>(Engine->GetFullscreenMode()));
		}
		Engine->ConfirmVideoMode();
		Engine->SaveSettings();
	}

	void ScctConsoleApplyUser(bool bVolumes)
	{
		UScctUserSettings::Get()->ApplyAll();
		if (bVolumes)
		{
			if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent())
			{
				Menu->RefreshVolumes();
			}
		}
	}

	UScctControlSettings* ScctConsoleControls()
	{
		UScctControlSettings* Controls = UScctControlSettings::Get();
		Controls->EnsureLoaded();
		return Controls;
	}

	FString ScctConsoleBoolText(bool bValue) { return bValue ? TEXT("true") : TEXT("false"); }

	FString ScctConsoleNumberText(float Value)
	{
		FString Text = FString::Printf(TEXT("%.3f"), Value);
		while (Text.Contains(TEXT(".")) && (Text.EndsWith(TEXT("0")) || Text.EndsWith(TEXT("."))))
		{
			Text.LeftChopInline(1);
		}
		return Text;
	}

	bool ScctConsoleParseBool(const FString& Text, bool& bOut)
	{
		const FString Value = Text.TrimStartAndEnd();
		for (const TCHAR* Yes : { TEXT("true"), TEXT("on"), TEXT("yes"), TEXT("1") })
		{
			if (Value.Equals(Yes, ESearchCase::IgnoreCase)) { bOut = true; return true; }
		}
		for (const TCHAR* No : { TEXT("false"), TEXT("off"), TEXT("no"), TEXT("0") })
		{
			if (Value.Equals(No, ESearchCase::IgnoreCase)) { bOut = false; return true; }
		}
		return false;
	}

	/** A choice by index, by label, or by the start of a label (the first that starts so); INDEX_NONE for none. */
	int32 ScctConsoleChoiceIndex(const FString& Text, const TArray<FString>& Choices)
	{
		const FString Value = Text.TrimStartAndEnd();
		if (Value.IsNumeric() && !Value.Contains(TEXT(".")))
		{
			const int32 Index = FCString::Atoi(*Value);
			return Choices.IsValidIndex(Index) ? Index : INDEX_NONE;
		}
		for (int32 Index = 0; Index < Choices.Num(); ++Index)
		{
			if (Choices[Index].Equals(Value, ESearchCase::IgnoreCase)) { return Index; }
		}
		for (int32 Index = 0; Index < Choices.Num(); ++Index)
		{
			if (!Value.IsEmpty() && Choices[Index].StartsWith(Value, ESearchCase::IgnoreCase)) { return Index; }
		}
		return INDEX_NONE;
	}

	/**
	 * Checks a value against a type and range and writes it the way the setting stores it: a switch as true/false, a
	 * choice as its index, a number clamped. False with the reason when it cannot be read.
	 */
	bool ScctConsoleNormalise(EScctSettingType Type, float Min, float Max, const TArray<FString>& Choices, const FString& Value, FString& Out, FString& Error)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		switch (Type)
		{
		case EScctSettingType::Bool:
		{
			bool bValue = false;
			if (!ScctConsoleParseBool(Trimmed, bValue))
			{
				Error = FString::Printf(TEXT("'%s' is not true or false"), *Trimmed);
				return false;
			}
			Out = ScctConsoleBoolText(bValue);
			return true;
		}
		case EScctSettingType::Int:
		case EScctSettingType::Float:
		{
			if (!Trimmed.IsNumeric())
			{
				Error = FString::Printf(TEXT("'%s' is not a number"), *Trimmed);
				return false;
			}
			const float Number = FMath::Clamp(FCString::Atof(*Trimmed), Min, Max);
			Out = Type == EScctSettingType::Int ? FString::FromInt(FMath::RoundToInt32(Number)) : ScctConsoleNumberText(Number);
			return true;
		}
		case EScctSettingType::Choice:
		{
			const int32 Index = ScctConsoleChoiceIndex(Trimmed, Choices);
			if (Index == INDEX_NONE)
			{
				Error = FString::Printf(TEXT("'%s' is none of: %s"), *Trimmed, *FString::Join(Choices, TEXT(" | ")));
				return false;
			}
			Out = FString::FromInt(Index);
			return true;
		}
		default:
			Out = Trimmed;
			return true;
		}
	}

	FString ScctConsoleTakes(EScctSettingType Type, float Min, float Max, const TArray<FString>& Choices)
	{
		switch (Type)
		{
		case EScctSettingType::Bool: return TEXT("true | false");
		case EScctSettingType::Int: return FString::Printf(TEXT("%d to %d"), FMath::RoundToInt32(Min), FMath::RoundToInt32(Max));
		case EScctSettingType::Float: return FString::Printf(TEXT("%s to %s"), *ScctConsoleNumberText(Min), *ScctConsoleNumberText(Max));
		case EScctSettingType::Choice: return FString::Join(Choices, TEXT(" | "));
		default: return TEXT("text");
		}
	}

	const TArray<FString>& ScctConsoleQualityNames()
	{
		static const TArray<FString> Names = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
		return Names;
	}

	FScctConsoleOwned OwnedBool(const TCHAR* Key, TFunction<bool()> Get, TFunction<void(bool)> Set)
	{
		FScctConsoleOwned Owned;
		Owned.Key = Key;
		Owned.Type = EScctSettingType::Bool;
		Owned.Get = [Get] { return ScctConsoleBoolText(Get()); };
		Owned.Set = [Set](const FString& Value) { Set(Value.ToBool()); };
		return Owned;
	}

	FScctConsoleOwned OwnedNumber(const TCHAR* Key, bool bInt, float Min, float Max, TFunction<float()> Get, TFunction<void(float)> Set, const TCHAR* Note = TEXT(""))
	{
		FScctConsoleOwned Owned;
		Owned.Key = Key;
		Owned.Type = bInt ? EScctSettingType::Int : EScctSettingType::Float;
		Owned.Min = Min;
		Owned.Max = Max;
		Owned.Note = Note;
		Owned.Get = [Get, bInt] { return bInt ? FString::FromInt(FMath::RoundToInt32(Get())) : ScctConsoleNumberText(Get()); };
		Owned.Set = [Set](const FString& Value) { Set(FCString::Atof(*Value)); };
		return Owned;
	}

	FScctConsoleOwned OwnedChoice(const TCHAR* Key, const TArray<FString>& Choices, TFunction<int32()> Get, TFunction<void(int32)> Set)
	{
		FScctConsoleOwned Owned;
		Owned.Key = Key;
		Owned.Type = EScctSettingType::Choice;
		Owned.Choices = Choices;
		Owned.Max = static_cast<float>(Choices.Num() - 1);
		Owned.Get = [Get] { return FString::FromInt(Get()); };
		Owned.Set = [Set](const FString& Value) { Set(FCString::Atoi(*Value)); };
		return Owned;
	}

	/** A control setting: changed on UScctControlSettings, handed to the bridge (which clamps it) at once. */
	FScctConsoleOwned OwnedControl(const TCHAR* Key, float Min, float Max, float UScctControlSettings::* Field)
	{
		return OwnedNumber(Key, false, Min, Max,
			[Field] { return ScctConsoleControls()->*Field; },
			[Field](float Value) { UScctControlSettings* C = ScctConsoleControls(); C->*Field = Value; C->Apply(); });
	}

	FScctConsoleOwned OwnedControlSwitch(const TCHAR* Key, bool UScctControlSettings::* Field)
	{
		return OwnedBool(Key,
			[Field] { return ScctConsoleControls()->*Field; },
			[Field](bool bValue) { UScctControlSettings* C = ScctConsoleControls(); C->*Field = bValue; C->Apply(); });
	}

	FScctConsoleOwned OwnedLook(const TCHAR* Key, FScctLookOptions UScctControlSettings::* Look, float FScctLookOptions::* Field)
	{
		return OwnedNumber(Key, false, 0.01f, 20.0f,
			[Look, Field] { return (ScctConsoleControls()->*Look).*Field; },
			[Look, Field](float Value) { UScctControlSettings* C = ScctConsoleControls(); (C->*Look).*Field = Value; C->Apply(); });
	}

	FScctConsoleOwned OwnedVolume(const TCHAR* Key, int32 UScctUserSettings::* Field)
	{
		return OwnedNumber(Key, true, 0.0f, 100.0f,
			[Field] { return static_cast<float>(UScctUserSettings::Get()->*Field); },
			[Field](float Value) { UScctUserSettings::Get()->*Field = FMath::RoundToInt32(Value); ScctConsoleApplyUser(true); });
	}

	TArray<FScctConsoleOwned> BuildOwned()
	{
		TArray<FScctConsoleOwned> Out;

		// UGameUserSettings.
		Out.Add(OwnedChoice(TEXT("Display.WindowMode"), { TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed") },
			[] { const UGameUserSettings* E = ScctConsoleEngineSettings(); return E != nullptr ? FMath::Clamp(static_cast<int32>(E->GetFullscreenMode()), 0, 2) : 2; },
			[](int32 Value) { if (UGameUserSettings* E = ScctConsoleEngineSettings()) { E->SetFullscreenMode(static_cast<EWindowMode::Type>(Value)); ScctConsoleApplyResolution(); } }));
		{
			FScctConsoleOwned Resolution;
			Resolution.Key = TEXT("Display.Resolution");
			Resolution.Type = EScctSettingType::Text;
			Resolution.Note = TEXT("width x height, as 1920x1080. Borderless uses the desktop size.");
			Resolution.Get = [] { const UGameUserSettings* E = ScctConsoleEngineSettings(); const FIntPoint Size = E != nullptr ? E->GetScreenResolution() : FIntPoint::ZeroValue; return FString::Printf(TEXT("%dx%d"), Size.X, Size.Y); };
			Resolution.Set = [](const FString& Value)
			{
				FString W, H;
				if (UGameUserSettings* E = ScctConsoleEngineSettings(); E != nullptr && Value.Replace(TEXT(" "), TEXT("")).Split(TEXT("x"), &W, &H, ESearchCase::IgnoreCase))
				{
					E->SetScreenResolution(FIntPoint(FMath::Clamp(FCString::Atoi(*W), 640, 7680), FMath::Clamp(FCString::Atoi(*H), 480, 4320)));
					ScctConsoleApplyResolution();
				}
			};
			Out.Add(Resolution);
		}
		Out.Add(OwnedBool(TEXT("Display.VSync"),
			[] { const UGameUserSettings* E = ScctConsoleEngineSettings(); return E != nullptr && E->IsVSyncEnabled(); },
			[](bool bValue) { if (UGameUserSettings* E = ScctConsoleEngineSettings()) { E->SetVSyncEnabled(bValue); ScctConsoleApplyQuality(); } }));
		Out.Add(OwnedNumber(TEXT("Performance.MaxFps"), true, 0.0f, 1000.0f,
			[] { const UGameUserSettings* E = ScctConsoleEngineSettings(); return E != nullptr ? E->GetFrameRateLimit() : 0.0f; },
			[](float Value) { if (UGameUserSettings* E = ScctConsoleEngineSettings()) { E->SetFrameRateLimit(static_cast<float>(FMath::RoundToInt32(Value))); ScctConsoleApplyQuality(); } },
			TEXT("0 or unlimited for no limit; monitor for the monitor's refresh rate")));
		Out.Add(OwnedChoice(TEXT("Graphics.Quality"), ScctConsoleQualityNames(),
			[] { const UGameUserSettings* E = ScctConsoleEngineSettings(); return E != nullptr ? FMath::Clamp(E->GetOverallScalabilityLevel(), 0, 4) : 3; },
			[](int32 Value) { if (UGameUserSettings* E = ScctConsoleEngineSettings()) { E->SetOverallScalabilityLevel(Value); ScctConsoleApplyQuality(); } }));
		struct FGroup
		{
			const TCHAR* Key;
			int32 (UGameUserSettings::*Get)() const;
			void (UGameUserSettings::*Set)(int32);
		};
		static const FGroup Groups[] = {
			{ TEXT("Graphics.GlobalIllumination"), &UGameUserSettings::GetGlobalIlluminationQuality, &UGameUserSettings::SetGlobalIlluminationQuality },
			{ TEXT("Graphics.Reflections"), &UGameUserSettings::GetReflectionQuality, &UGameUserSettings::SetReflectionQuality },
			{ TEXT("Graphics.Shadows"), &UGameUserSettings::GetShadowQuality, &UGameUserSettings::SetShadowQuality },
			{ TEXT("Graphics.PostProcessing"), &UGameUserSettings::GetPostProcessingQuality, &UGameUserSettings::SetPostProcessingQuality },
			{ TEXT("Graphics.Textures"), &UGameUserSettings::GetTextureQuality, &UGameUserSettings::SetTextureQuality },
			{ TEXT("Graphics.Effects"), &UGameUserSettings::GetVisualEffectQuality, &UGameUserSettings::SetVisualEffectQuality },
			{ TEXT("Graphics.AntiAliasingQuality"), &UGameUserSettings::GetAntiAliasingQuality, &UGameUserSettings::SetAntiAliasingQuality },
			{ TEXT("Graphics.ViewDistance"), &UGameUserSettings::GetViewDistanceQuality, &UGameUserSettings::SetViewDistanceQuality },
			{ TEXT("Graphics.Foliage"), &UGameUserSettings::GetFoliageQuality, &UGameUserSettings::SetFoliageQuality },
			{ TEXT("Graphics.Shading"), &UGameUserSettings::GetShadingQuality, &UGameUserSettings::SetShadingQuality },
		};
		for (const FGroup& Group : Groups)
		{
			const FGroup* G = &Group;
			Out.Add(OwnedChoice(Group.Key, ScctConsoleQualityNames(),
				[G] { const UGameUserSettings* E = ScctConsoleEngineSettings(); return E != nullptr ? FMath::Clamp((E->*(G->Get))(), 0, 4) : 3; },
				[G](int32 Value) { if (UGameUserSettings* E = ScctConsoleEngineSettings()) { (E->*(G->Set))(Value); ScctConsoleApplyQuality(); } }));
		}

		// UScctUserSettings.
		Out.Add(OwnedVolume(TEXT("Audio.Master"), &UScctUserSettings::MasterVolume));
		Out.Add(OwnedVolume(TEXT("Audio.Effects"), &UScctUserSettings::EffectsVolume));
		Out.Add(OwnedVolume(TEXT("Audio.Ambient"), &UScctUserSettings::AmbientVolume));
		Out.Add(OwnedVolume(TEXT("Audio.Music"), &UScctUserSettings::MusicVolume));
		Out.Add(OwnedVolume(TEXT("Audio.MenuMusic"), &UScctUserSettings::MenuMusicVolume));
		Out.Add(OwnedVolume(TEXT("Audio.MenuSounds"), &UScctUserSettings::MenuSoundsVolume));
		Out.Add(OwnedVolume(TEXT("Audio.Objective"), &UScctUserSettings::ObjectiveVolume));
		Out.Add(OwnedVolume(TEXT("Audio.Voice"), &UScctUserSettings::VoiceVolume));
		Out.Add(OwnedVolume(TEXT("Audio.HubJukebox"), &UScctUserSettings::HubJukeboxVolume));
		Out.Add(OwnedVolume(TEXT("Audio.HubChimes"), &UScctUserSettings::HubChimesVolume));
		Out.Add(OwnedNumber(TEXT("Display.Gamma"), false, 1.6f, 3.0f,
			[] { return UScctUserSettings::Get()->Gamma; },
			[](float Value) { UScctUserSettings::Get()->Gamma = Value; ScctConsoleApplyUser(false); }, TEXT("2.2 is the engine's own; higher is brighter")));
		Out.Add(OwnedNumber(TEXT("Graphics.RenderScale"), true, 50.0f, 100.0f,
			[] { return static_cast<float>(UScctUserSettings::Get()->RenderScale); },
			[](float Value) { UScctUserSettings::Get()->RenderScale = FMath::RoundToInt32(Value); ScctConsoleApplyUser(false); ScctGraphicsSettings::Apply(); }, TEXT("per cent")));
		Out.Add(OwnedBool(TEXT("Graphics.MotionBlur"),
			[] { return UScctUserSettings::Get()->bMotionBlur; },
			[](bool bValue) { UScctUserSettings::Get()->bMotionBlur = bValue; ScctConsoleApplyUser(false); }));
		Out.Add(OwnedBool(TEXT("Graphics.LiveMenuBackground"),
			[] { return UScctUserSettings::Get()->bLiveMenuBackground; },
			[](bool bValue) { UScctUserSettings::Get()->bLiveMenuBackground = bValue; ScctConsoleApplyUser(false); }));
		Out.Add(OwnedBool(TEXT("Hud.DistanceInFeet"),
			[] { return UScctUserSettings::Get()->bDistanceInFeet; },
			[](bool bValue) { UScctUserSettings::Get()->bDistanceInFeet = bValue; ScctConsoleApplyUser(false); }));

		// UScctControlSettings (the bridge's controls).
		Out.Add(OwnedControlSwitch(TEXT("Display.Widescreen"), &UScctControlSettings::bWidescreen));
		Out.Add(OwnedControl(TEXT("Display.MercFovCap"), 95.0f, 112.0f, &UScctControlSettings::MercFovCap));
		Out.Add(OwnedControlSwitch(TEXT("Controls.InvertY"), &UScctControlSettings::bInvertY));
		Out.Add(OwnedControlSwitch(TEXT("Controls.InvertXSpy"), &UScctControlSettings::bInvertXSpy));
		Out.Add(OwnedControlSwitch(TEXT("Controls.SpyRollFreeLook"), &UScctControlSettings::bSpyRollFreeLook));
		Out.Add(OwnedControlSwitch(TEXT("Controls.AutoHackComms"), &UScctControlSettings::bAutoHackComms));
		Out.Add(OwnedControlSwitch(TEXT("Controls.MouseWheelSpeed"), &UScctControlSettings::bMouseWheelSpeed));
		Out.Add(OwnedControl(TEXT("Controls.MouseScale"), 0.05f, 10.0f, &UScctControlSettings::MouseScale));
		Out.Add(OwnedLook(TEXT("Controls.SpyFreeLook"), &UScctControlSettings::SpyMouse, &FScctLookOptions::FreeLook));
		Out.Add(OwnedLook(TEXT("Controls.SpyLook"), &UScctControlSettings::SpyMouse, &FScctLookOptions::Look));
		Out.Add(OwnedLook(TEXT("Controls.SpyScope"), &UScctControlSettings::SpyMouse, &FScctLookOptions::Scope));
		Out.Add(OwnedLook(TEXT("Controls.SpyCam"), &UScctControlSettings::SpyMouse, &FScctLookOptions::Cam));
		Out.Add(OwnedLook(TEXT("Controls.MercLook"), &UScctControlSettings::MercMouse, &FScctLookOptions::Look));
		Out.Add(OwnedLook(TEXT("Controls.MercScope"), &UScctControlSettings::MercMouse, &FScctLookOptions::Scope));
		Out.Add(OwnedLook(TEXT("Controls.MercScopeVertical"), &UScctControlSettings::MercMouse, &FScctLookOptions::ScopeVertical));
		Out.Add(OwnedLook(TEXT("Controls.MercCam"), &UScctControlSettings::MercMouse, &FScctLookOptions::Cam));
		Out.Add(OwnedControlSwitch(TEXT("Controller.Enabled"), &UScctControlSettings::bPadEnabled));
		Out.Add(OwnedControlSwitch(TEXT("Controller.Rumble"), &UScctControlSettings::bPadRumble));
		Out.Add(OwnedControl(TEXT("Controller.RumbleStrength"), 0.0f, 1.0f, &UScctControlSettings::PadRumbleStrength));
		Out.Add(OwnedControl(TEXT("Controller.DeadzoneLeft"), 0.0f, 0.9f, &UScctControlSettings::PadDeadzoneLeft));
		Out.Add(OwnedControl(TEXT("Controller.DeadzoneRight"), 0.0f, 0.9f, &UScctControlSettings::PadDeadzoneRight));
		Out.Add(OwnedControl(TEXT("Controller.VerticalScale"), 0.1f, 3.0f, &UScctControlSettings::PadVerticalScale));
		Out.Add(OwnedControl(TEXT("Controller.LookScale"), 0.1f, 5.0f, &UScctControlSettings::PadLookScale));
		Out.Add(OwnedLook(TEXT("Controller.SpyFreeLook"), &UScctControlSettings::SpyPad, &FScctLookOptions::FreeLook));
		Out.Add(OwnedLook(TEXT("Controller.SpyLook"), &UScctControlSettings::SpyPad, &FScctLookOptions::Look));
		Out.Add(OwnedLook(TEXT("Controller.MercLook"), &UScctControlSettings::MercPad, &FScctLookOptions::Look));
		return Out;
	}

	const TArray<FScctConsoleOwned>& OwnedSettings()
	{
		static const TArray<FScctConsoleOwned> Settings = BuildOwned();
		return Settings;
	}

	const FScctConsoleOwned* FindOwned(const FString& Key)
	{
		return OwnedSettings().FindByPredicate([&Key](const FScctConsoleOwned& Owned) { return Owned.Key.Equals(Key, ESearchCase::IgnoreCase); });
	}

	const FScctSettingDesc* FindRegistered(const FString& Key)
	{
		for (const FScctSettingDesc* Desc : FScctSettings::AllOptions())
		{
			if (Desc->Key.ToString().Equals(Key, ESearchCase::IgnoreCase))
			{
				return Desc;
			}
		}
		return nullptr;
	}

	TArray<FString> ChoiceLabels(const FScctSettingDesc& Desc)
	{
		TArray<FString> Labels;
		for (const FText& Choice : Desc.Choices)
		{
			Labels.Add(Choice.ToString());
		}
		return Labels;
	}

	/** A stored choice index as its label (what Get shows); anything else as it is. */
	FString ShownValue(EScctSettingType Type, const TArray<FString>& Choices, const FString& Stored)
	{
		if (Type == EScctSettingType::Choice)
		{
			const int32 Index = FCString::Atoi(*Stored);
			return Choices.IsValidIndex(Index) ? Choices[Index] : Stored;
		}
		if (Type == EScctSettingType::Float)
		{
			return ScctConsoleNumberText(FCString::Atof(*Stored));
		}
		return Stored;
	}

	FString PageOf(const FString& Key)
	{
		FString Page, Rest;
		return Key.Split(TEXT("."), &Page, &Rest) ? Page : FString(TEXT("Other"));
	}
}

TArray<FString> FScctSettingsConsole::Keys()
{
	TArray<FString> Out;
	for (const FScctConsoleOwned& Owned : OwnedSettings())
	{
		Out.Add(Owned.Key);
	}
	for (const FScctSettingDesc* Desc : FScctSettings::AllOptions())
	{
		Out.AddUnique(Desc->Key.ToString());
	}
	Out.Sort();
	return Out;
}

TArray<TPair<FString, int32>> FScctSettingsConsole::Pages()
{
	TMap<FString, int32> Counts;
	for (const FString& Key : Keys())
	{
		++Counts.FindOrAdd(PageOf(Key));
	}
	TArray<TPair<FString, int32>> Out;
	for (const TPair<FString, int32>& Page : Counts)
	{
		Out.Add(Page);
	}
	Out.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Key < B.Key; });
	return Out;
}

bool FScctSettingsConsole::Has(const FString& Key)
{
	return !CanonicalKey(Key).IsEmpty();
}

FString FScctSettingsConsole::CanonicalKey(const FString& Key)
{
	if (const FScctConsoleOwned* Owned = FindOwned(Key))
	{
		return Owned->Key;
	}
	if (const FScctSettingDesc* Desc = FindRegistered(Key))
	{
		return Desc->Key.ToString();
	}
	return FString();
}

bool FScctSettingsConsole::Get(const FString& Key, FString& OutValue, FString& OutError)
{
	if (const FScctConsoleOwned* Owned = FindOwned(Key))
	{
		OutValue = ShownValue(Owned->Type, Owned->Choices, Owned->Get());
		return true;
	}
	if (const FScctSettingDesc* Desc = FindRegistered(Key))
	{
		OutValue = ShownValue(Desc->Type, ChoiceLabels(*Desc), FScctSettings::GetString(Desc->Key));
		return true;
	}
	OutError = FString::Printf(TEXT("no setting called '%s' (Settings lists them)"), *Key);
	return false;
}

bool FScctSettingsConsole::Set(const FString& Key, const FString& Value, FString& OutValue, FString& OutError)
{
	if (const FScctConsoleOwned* Owned = FindOwned(Key))
	{
		FString Wanted = Value.TrimStartAndEnd();
		if (Owned->Key == TEXT("Performance.MaxFps"))
		{
			if (Wanted.Equals(TEXT("unlimited"), ESearchCase::IgnoreCase) || Wanted.Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				Wanted = TEXT("0");
			}
			else if (Wanted.Equals(TEXT("monitor"), ESearchCase::IgnoreCase))
			{
				Wanted = FString::FromInt(FMath::RoundToInt32(ScctPerformanceSettings::MonitorHz()));
			}
		}
		FString Normalised;
		if (!ScctConsoleNormalise(Owned->Type, Owned->Min, Owned->Max, Owned->Choices, Wanted, Normalised, OutError))
		{
			return false;
		}
		Owned->Set(Normalised);
		OutValue = ShownValue(Owned->Type, Owned->Choices, Owned->Get());
		return true;
	}
	if (const FScctSettingDesc* Desc = FindRegistered(Key))
	{
		const TArray<FString> Labels = ChoiceLabels(*Desc);
		FString Normalised;
		if (Value.TrimStartAndEnd().Equals(TEXT("default"), ESearchCase::IgnoreCase))
		{
			FScctSettings::Reset(Desc->Key);
		}
		else if (!ScctConsoleNormalise(Desc->Type, Desc->Min, Desc->Max, Labels, Value, Normalised, OutError))
		{
			return false;
		}
		else
		{
			// The registry clamps again to the option's own step, and raises OnChanged for whoever applies it.
			FScctSettings::SetString(Desc->Key, Normalised);
		}
		OutValue = ShownValue(Desc->Type, Labels, FScctSettings::GetString(Desc->Key));
		return true;
	}
	OutError = FString::Printf(TEXT("no setting called '%s' (Settings lists them)"), *Key);
	return false;
}

TArray<FString> FScctSettingsConsole::Describe(const FString& Page)
{
	TArray<FString> Lines;
	for (const FString& Key : Keys())
	{
		if (!Page.IsEmpty() && !PageOf(Key).Equals(Page, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FString Value, Error;
		Get(Key, Value, Error);
		FString Takes;
		if (const FScctConsoleOwned* Owned = FindOwned(Key))
		{
			Takes = ScctConsoleTakes(Owned->Type, Owned->Min, Owned->Max, Owned->Choices);
			if (!Owned->Note.IsEmpty())
			{
				Takes += TEXT("; ") + Owned->Note;
			}
		}
		else if (const FScctSettingDesc* Desc = FindRegistered(Key))
		{
			Takes = ScctConsoleTakes(Desc->Type, Desc->Min, Desc->Max, ChoiceLabels(*Desc));
			if (!Desc->bVisible)
			{
				Takes += TEXT("; not on the settings page");
			}
		}
		Lines.Add(FString::Printf(TEXT("%s = %s   (%s)"), *Key, *Value, *Takes));
	}
	return Lines;
}

void FScctSettingsConsole::Save()
{
	UScctUserSettings::Get()->Save();
	FScctSettings::Save();
	if (UGameUserSettings* Engine = ScctConsoleEngineSettings())
	{
		Engine->SaveSettings();
	}
	ScctConsoleControls()->Save();
}
