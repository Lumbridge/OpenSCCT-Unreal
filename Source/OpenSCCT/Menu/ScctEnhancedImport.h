#pragma once

#include "CoreMinimal.h"

/** What an import from Enhanced SCCT Versus took over. */
struct FScctEnhancedImportResult
{
	bool bOk = false;
	/** Why nothing was imported (bOk false). */
	FString Error;
	/** The install (or profile folder) read. */
	FString Source;
	/** The report for the player: a line per group taken over or left out. */
	TArray<FString> Notes;
	/** "Key = value" of every setting Unreal keeps that was set, and "Key: reason" of those that were refused. */
	TArray<FString> Applied;
	TArray<FString> Refused;
	/** The profile's gear as a kit text, and whether the saved kit took it. */
	FString Loadout;
	bool bLoadoutApplied = false;
	/** What the controls and key layouts became, for the log. */
	TArray<FString> ControlLines;
};

/**
 * "Import from Enhanced SCCT Versus" (the Godot client's Options/EnhancedImport.cs and OptionsMenu.Import.cs): reads a
 * Reloaded install's System\SCCT_Versus.config, its profile <name>_prf.ini and key layouts <name>_kbd_0/1.ini through the
 * bridge (Runtime/ScctEnhancedImportApi.h) and puts them where each setting lives:
 *   - sensitivities, inversions, the wheel, widescreen and the controller's tuning, and both key layouts, onto the
 *     bridge's controls (UScctControlSettings, saved to controls.json and bindings.json);
 *   - volumes, display, graphics, HUD and voice through the settings console (Settings/ScctSettingsConsole.h), so each
 *     goes to its owner (UGameUserSettings, UScctUserSettings or the settings registry) and is put in force;
 *   - the gear onto the saved kit (UScctProfileOptions::ApplyLoadoutText).
 * The settings page's IMPORT FROM ENHANCED button, the console's Scct.ImportEnhanced and -ScctImportEnhanced=<folder>
 * (an unattended import once the game is up; "profile" for this PC's profile without an install) run it; each logs every
 * key it set ("ScctEnhancedImport: set Audio.Music = 25").
 */
namespace ScctEnhancedImport
{
	/** Imports from Folder (any folder of an install, or a folder of profiles), or this PC's profile with bWithoutInstall. */
	FScctEnhancedImportResult Import(const FString& Folder, bool bWithoutInstall);

	/** Where the game keeps its Versus profiles on this PC (empty without the bridge). */
	FString UsualProfileFolder();

	/** The result in a few lines for the player. */
	FString Summary(const FScctEnhancedImportResult& Result);

	/** Asks for a folder with Windows' folder picker; false when it was cancelled (or under -unattended, never shown). */
	bool PickFolder(const FString& Title, FString& OutFolder);
}
