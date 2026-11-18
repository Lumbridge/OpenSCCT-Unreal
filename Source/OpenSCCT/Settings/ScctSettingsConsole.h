#pragma once

#include "CoreMinimal.h"

/**
 * Every setting of the game by one name, for the developer console's Get, Set and Settings (Play/ScctConsoleCommands.cpp,
 * the Godot client's Options/SettingsConsole.cs) and for the Enhanced SCCT Versus import (Menu/ScctEnhancedImport.h).
 *
 * A key names a setting where it lives, and nothing is copied:
 *   - the settings registry's own options (Settings/ScctSettings.h) under their registry keys ("Graphics.AntiAliasing",
 *     "Voice.PushToTalk", "Hud.FpsMode"...);
 *   - the settings other owners hold, under names given here:
 *       Display.WindowMode, Display.Resolution, Display.VSync, Performance.MaxFps, Graphics.Quality and the quality
 *         groups (Graphics.Shadows, Graphics.GlobalIllumination...)                    UGameUserSettings
 *       Audio.<volume>, Display.Gamma, Graphics.RenderScale, Graphics.MotionBlur, Hud.DistanceInFeet  UScctUserSettings
 *       Display.Widescreen, Display.MercFovCap, Controls.*, Controller.*               UScctControlSettings
 *
 * A value is written as the settings page shows it: a switch as true/false (on/off, yes/no and 1/0 are read too), a
 * choice by its index or its label (or the start of it: "8x" picks "8x anisotropic"), a number as a number (MaxFps
 * also takes "unlimited" and "monitor"). A value is put in force at once, as the settings page does; Save writes them.
 * Unattended runs (-unattended) keep a window mode or a resolution for the next run without changing the window.
 */
namespace FScctSettingsConsole
{
	/** Every key, sorted. */
	TArray<FString> Keys();

	/** The pages ("Audio", "Display", "Graphics"...) with how many keys each has, sorted. */
	TArray<TPair<FString, int32>> Pages();

	/** True for a key Get and Set know (the case does not matter). */
	bool Has(const FString& Key);

	/** The key as it is spelt here ("audio.effects" gives "Audio.Effects"); empty for an unknown one. */
	FString CanonicalKey(const FString& Key);

	/** The value as it would be written to Set; false (with the reason) for an unknown key. */
	bool Get(const FString& Key, FString& OutValue, FString& OutError);

	/**
	 * Sets a value and puts it in force. False with the reason for an unknown key or a value the setting cannot take.
	 * OutValue is the value it has now (clamped, as the setting took it).
	 */
	bool Set(const FString& Key, const FString& Value, FString& OutValue, FString& OutError);

	/** One line per key of a page (empty: every page): "Key = value   (what it takes)". */
	TArray<FString> Describe(const FString& Page);

	/** Writes everything Set may have changed: the user settings file, Unreal's game user settings, controls.json. */
	void Save();
}
