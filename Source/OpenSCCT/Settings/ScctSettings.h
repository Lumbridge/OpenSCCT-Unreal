#pragma once

#include "CoreMinimal.h"

/**
 * The player's settings, as one registry every feature puts its own options into.
 *
 * A feature (audio, voice, the HUD, graphics, the crosshair...) registers each option it reads once, at module or
 * subsystem start, with its page, label, type and default. The Options screen draws every registered option of a
 * page without knowing the feature, and the feature reads the value back (and listens to OnChanged to apply a change
 * at once). Values are kept as text in GGameUserSettingsIni under [OpenSCCT.Settings], so they survive a restart.
 *
 * Pages are plain names; the Options screen shows the ones below in this order and any other page after them:
 *   Spy, Mercenary, Display, Graphics, Performance, Audio, Voice, Hud, Controller, Online
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
	/** Lower sorts first within the page; ties keep registration order. */
	int32 Order = 0;
	/** False hides the option from the Options screen (a value only code or the console sets). */
	bool bVisible = true;
};

namespace FScctSettings
{
	/** Adds an option, or replaces the description of one already registered under the same key (its value is kept). */
	OPENSCCT_API void Register(const FScctSettingDesc& Desc);

	OPENSCCT_API const FScctSettingDesc* Find(FName Key);

	/** The visible options of a page, sorted by Order then registration. */
	OPENSCCT_API TArray<const FScctSettingDesc*> OptionsOf(FName Page);

	/** Every page that has at least one visible option, in the Options screen's order. */
	OPENSCCT_API TArray<FName> Pages();

	/** The stored text, or the default when the player never set it (empty for an unknown key). */
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
