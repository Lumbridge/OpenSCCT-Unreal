#pragma once

#include "CoreMinimal.h"

/**
 * Options forced for one run from the command line, so an unattended run can be made with any setting:
 *
 *   -ScctSettingSet=<Key>=<Value>[;<Key>=<Value>...]     -ScctSettingSet=Performance.MaxFps=30;Graphics.Quality=0
 *
 * A forced value is what FScctSettings::Get* returns for the whole run (clamped like any value), in place of the
 * stored one, and it is never written to the user settings file: the player's own settings are as they were the
 * next time the game starts. Once the option is set or reset (the Options screen applied an edit to it), the forced
 * value gives way to that.
 *
 * The Options screen's -ScctOptionsEdit edits options on the screen instead, as the player would, without applying.
 */
namespace FScctSettingsForced
{
	/** The forced text of an option, or null (the command line is read on the first call). */
	OPENSCCT_API const FString* Find(FName Key);

	OPENSCCT_API bool IsForced(FName Key);

	/** Forces an option again (what was forced before a change that is being undone). Raises no change event. */
	OPENSCCT_API void Force(FName Key, const FString& Value);

	/** Stops forcing an option; true when it was forced. */
	OPENSCCT_API bool Drop(FName Key);

	/** Writes every forced value to the log, and warns about keys no registered option has (call once options are registered). */
	OPENSCCT_API void Report();
}
