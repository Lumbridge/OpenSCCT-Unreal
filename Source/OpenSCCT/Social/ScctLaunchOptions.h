#pragma once

#include "CoreMinimal.h"

/**
 * The launcher's options, as the Godot client reads them (App/LaunchOptions.cs and ProcessTuning.cs), so a launcher
 * starts either client with one command line:
 *
 *   --svm-content=<dir>          the converted content folder (as -ScctContent=)
 *   --svm-game-root=<dir>        the original game's folder (Packages, System): the bridge reads it as SVM_GAME_ROOT
 *   --svm-map-downloads=<dir>    where downloaded maps and packs are kept (the bridge's packs folder, OPENSCCT_PACKS)
 *   --svm-maps=<dir>             one more folder of maps, searched first (repeatable; as -ScctMaps=)
 *   --priority=<name>            idle, low, below, normal, medium, above, high
 *   --affinity=<cpus>            a mask (0xF, 15) or a list of CPUs (0,2-3)
 *   --svm-setting=<Key>=<Value>  an option of the settings registry for this run only, never saved (repeatable; as
 *                                -ScctSettingSet=)
 *
 * and the Windows power throttling opt-out (Reloaded's throttle_stop): the Performance option
 * "Stop Windows power throttling", read at start.
 */
namespace ScctLaunchOptions
{
	/** Reads the command line and applies the process options; once, from the module's startup. */
	void Apply();

	/** --svm-content's folder, or empty. */
	const FString& ContentRoot();

	/** --svm-maps' folders, in order. */
	const TArray<FString>& MapRoots();
}
