#pragma once

#include "CoreMinimal.h"

/**
 * The player's settings, as one registry every feature puts its own options into.
 *
 * A feature (audio, voice, the HUD, graphics, the crosshair...) registers each option it reads once, at module or
 * subsystem start, with its page, label, type and default. The settings page (Menu/SScctSettingsPanel.h) draws every registered option of a
 * page without knowing the feature, and the feature reads the value back (and listens to OnChanged to apply a change
 * at once). Values are kept as text in GGameUserSettingsIni under [OpenSCCT.Settings], so they survive a restart.
 *
 * Pages are plain names. The settings page draws Display, Graphics and Performance at the end of its Video tab, Audio at
 * the end of its Audio tab, Spy, Mercenary and Controller at the end of its Controls tab, and any other page (Hud,
 * Online, Voice...) as a tab of its own, in this order: Spy, Mercenary, Display, Graphics, Performance, Audio, Voice,
 * Hud, Controller, Online, then the rest.
 *
 * Options the settings page has a row of its own for are NOT registered: those values have one home elsewhere
 * (volumes, gamma, render scale and motion blur in UScctUserSettings; window mode, resolution, vertical sync, frame
 * limit and quality in UGameUserSettings; the widescreen field of view in UScctControlSettings).
 * Key bindings are not here: they live in UScctControlSettings with the rest of the controls.
 */
enum class EScctSettingType : uint8
{
	Bool,
	/** A number between Min and Max, moved in steps of Step (0: any). */
	Float,
	Int,
	/** One of Choices; the stored value is the index. */
	Choice,
	Text,
};

struct OPENSCCT_API FScctSettingDesc
{
	/** Unique across all pages ("Audio.Master", "Hud.ShowPing"). */
	FName Key;
	FName Page;
	/** A heading inside the page the option is listed under; options of one group stay together. */
	FText Group;
	FText Label;
	FText Tooltip;
	EScctSettingType Type = EScctSettingType::Bool;
	FString Default;
	float Min = 0.f;
	float Max = 1.f;
	float Step = 0.f;
	/** For Choice. */
	TArray<FText> Choices;
	/** Optional hardware filter. Stored choice indices remain stable when a profile moves to another PC. */
	TFunction<bool(int32)> SupportsChoice;
	/** Some supported choices require a different graphics backend at the next launch. */
	TFunction<bool(int32)> ChoiceRequiresRestart;
	/** Lower sorts first within the page; ties keep registration order. */
	int32 Order = 0;
	/** False hides the option from the settings page (a value only code or the console sets). */
	bool bVisible = true;
	/**
	 * For Float and Int: how the settings page writes a value beside its slider ("Unlimited", "+10%"); unset writes
	 * the number. Asked each time the value is drawn, so it may read the moment (the monitor's refresh rate).
	 */
	TFunction<FString(float)> FormatValue;
};

namespace FScctSettings
{
	/** Adds an option, or replaces the description of one already registered under the same key (its value is kept). */
	OPENSCCT_API void Register(const FScctSettingDesc& Desc);

	OPENSCCT_API const FScctSettingDesc* Find(FName Key);

	/** The visible options of a page, sorted by Order then registration. */
	OPENSCCT_API TArray<const FScctSettingDesc*> OptionsOf(FName Page);

	/** Every page that has at least one visible option, in the settings page's order. */
	OPENSCCT_API TArray<FName> Pages();

	/** Every registered option, hidden ones included, in registration order (the settings console lists them all). */
	OPENSCCT_API TArray<const FScctSettingDesc*> AllOptions();

	/**
	 * The stored text, or the default when the player never set it (empty for an unknown key). A value forced for
	 * the run with -ScctSettingSet (Settings/ScctSettingsForced.h) comes before both until the option is set.
	 */
	OPENSCCT_API FString GetString(FName Key);
	OPENSCCT_API bool GetBool(FName Key);
	OPENSCCT_API float GetFloat(FName Key);
	OPENSCCT_API int32 GetInt(FName Key);

	/** Stores a value (clamped to the option's range) and raises OnChanged when it differs. Saved by Save(). */
	OPENSCCT_API void SetString(FName Key, const FString& Value);
	OPENSCCT_API void SetBool(FName Key, bool bValue);
	OPENSCCT_API void SetFloat(FName Key, float Value);
	OPENSCCT_API void SetInt(FName Key, int32 Value);

	/** Back to the default (one option, or every option of a page). */
	OPENSCCT_API void Reset(FName Key);
	OPENSCCT_API void ResetPage(FName Page);

	/** Writes the user settings file. */
	OPENSCCT_API void Save();

	/** Raised after a value changed, with its key. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnChanged, FName /*Key*/);
	OPENSCCT_API FOnChanged& OnChanged();
}
