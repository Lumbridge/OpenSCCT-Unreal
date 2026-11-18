#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The developer console's OpenSCCT commands (the Godot client's UI/DevConsole.cs, UI/ConsoleCommands.cs and
 * Options/SettingsConsole.cs), in the menus and in play alike; the console opens on the key left of 1 in development
 * builds.
 *
 * Every word the Godot console knows is typed the same here, with the same arguments, usage lines and rules, because
 * the line is run by that console's own parser in the bridge (Runtime/ScctConsoleApi.h):
 *
 *   help [command|bots]                              the commands, or one command's help
 *   login <password> | cmd <server command> | say <text>          the server played on
 *   resign | pings | map <name> [mode] | rmap_next | rmap_prev    sent as the matching chat command
 *   practice [on|off] | heal | regen | tank | kill | ammo         a practice server's commands
 *   infinite [on|off|toggle] (also: resources)       infinite ammo, gadget charges and mask air of a practice round,
 *                                                    online or in an offline match started with ?Practice=1
 *   addbot | kickbot | placebot | botstop | botresume | bothealth | botweapon | botteam   practice bots
 *   whereami [copy]                                  the map, position and view of the local player
 *
 * The answers of a server arrive a moment later and are printed as they come. The words Unreal runs itself:
 *
 *   get <key> | set <key> <value> | current | <key> [value]   the settings, by the keys of Settings/ScctSettingsConsole.h
 *                                                    ("Performance.MaxFps 144"); the Godot settings file's keys are not known
 *   Settings [page]                                  the pages and their sizes, or every key of a page with what it takes
 *   grabarea [on|off]                                the practice grab-area overlay (Test/ScctGrabAreaSubsystem.h)
 *   quit | exit                                      the engine's own
 *   AdminLogin <password>, Admin <command>           the names login and cmd had here first; they stay (Admin is not
 *                                                    the Godot console's alias of login)
 *   Scct.ImportEnhanced [folder]                     imports an Enhanced SCCT Versus install's settings
 *   Scct.After <seconds> <command>                   runs a console line later (unattended runs: -ExecCmds runs before a match is up)
 *
 * A line nothing knows, the engine included, is answered as the Godot console does: unknown command 'x' (type help).
 * The words reach these commands through UScctLocalPlayer (Play/ScctLocalPlayer.h), before the engine's own words of
 * the same name (GET, SET, HELP, SAY); they are also registered with the console manager, which is what the console's
 * auto-complete lists.
 */
namespace ScctConsoleCommands
{
	/**
	 * Runs "Get ..." or "Set ..." when it is about a setting; false for the engine's own GET and SET of a class
	 * property ("GET <class> <property>", "SET <class> <property> <value>").
	 */
	bool ExecSettingCommand(const TCHAR* Cmd, UWorld* World, FOutputDevice& Ar);

	/** Runs a line that is one of the Godot console's words (or a settings key typed alone); false for anything else. */
	bool ExecGameCommand(const TCHAR* Cmd, UWorld* World, FOutputDevice& Ar);

	/** The answer to a line nothing knew. */
	void SayUnknown(const TCHAR* Cmd, FOutputDevice& Ar);
}
