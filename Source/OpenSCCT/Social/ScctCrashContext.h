#pragma once

#include "CoreMinimal.h"

/**
 * What is being played, for crash reports (the Godot client's CrashLog.Context): the map, the mode, the side and
 * where (offline, an online server, a hub, a demo). Each change goes to
 *  - the engine's crash context as game data (ScctMap, ScctMode, ScctSide, ScctWhere, ScctPlaying), so a native crash
 *    report carries it, and
 *  - the bridge's CrashLog, whose crash-<utc>.log of an unhandled managed exception has it on its "Playing:" line.
 * The first call also names the folder of the managed reports: Saved/Crashes, beside the engine's own.
 *
 * -ScctCrashTest=managed throws on a bridge worker thread once a map is being played (the report is written and the
 * process ends); =report writes a report and goes on; =native ends with a fatal error of the engine.
 */
namespace ScctCrashContext
{
	/** Mode is EGameMode or -1; Side 0 spy, 1 merc, else none; Where is empty in the menus. Cheap when nothing changed. */
	void Update(const FString& Map, int32 Mode, int32 Side, const FString& Where);
}
