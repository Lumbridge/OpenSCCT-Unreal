#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// "Import from Enhanced SCCT Versus", from the C# bridge (Bridge/OpenScct.UnrealBridge/Controls/EnhancedImport.cs and
// EnhancedImportExports.cs, a port of the Godot client's Options/EnhancedImport.cs). An import reads a Reloaded install
// (System\SCCT_Versus.config, the profile <name>_prf.ini and its key files <name>_kbd_0/1.ini, found through the
// install's _Profiles_.lnk), puts the controls and the key layouts it found in use in the bridge, and returns the rest
// as lines of text (Menu/ScctEnhancedImport.h applies them):
//
//   source <folder>              the install (or profile folder) read
//   set <key>=<value>            a setting Unreal keeps, by its settings console key (Settings/ScctSettingsConsole.h)
//   loadout <kit>                the profile's gear ("spy:A,B,C,D;merc:...;weapon:W", ScctProfileOptions::ApplyLoadoutText)
//   note <line>                  the report for the player: a line per group taken over or left out
//   controls <line>              what the controls and key layouts became, for the log
//   error <reason>               nothing was imported (the only line then)
//
// This mirrors EnhancedImportExports.cs; change both together and bump the version.

constexpr int32 ScctEnhancedImportApiVersion = 1;

struct FScctEnhancedImportApi
{
	int32 Version;

	/**
	 * Imports from Folder (any folder of an install, or a folder of profiles; ignored with bWithoutInstall, which
	 * takes the profile on this PC). Writes the lines, cut to fit and zero terminated; returns their full length.
	 */
	int32 (__cdecl* Import)(const TCHAR* Folder, int32 bWithoutInstall, TCHAR* Buffer, int32 Capacity);

	/** Where the game keeps its Versus profiles on this PC (ProgramData\Ubisoft\...\Saved Games\Versus). */
	int32 (__cdecl* GetUsualProfileFolder)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctEnhancedImportApi) == 24, "FScctEnhancedImportApi must match EnhancedImportApi (64-bit)");

template <> struct TScctFeatureInfo<FScctEnhancedImportApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.EnhancedImportExports"); }
	static constexpr int32 Version = ScctEnhancedImportApiVersion;
};
