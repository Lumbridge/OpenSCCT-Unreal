#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The developer console's OpenSCCT commands (the Godot client's UI/DevConsole.cs and Options/SettingsConsole.cs), in
 * the menus and in play alike; the console opens on the key left of 1 in development builds:
 *
 *   Get <key>                  a setting's value ("Get Performance.MaxFps")
 *   Set <key> <value>          sets it, puts it in force and saves it ("Set Audio.Music 40", "Set Hud.FpsMode Simple")
 *   Settings [page]            the pages and their sizes, or every key of a page with its value and what it takes
 *   AdminLogin <password>      logs in as an admin of the dedicated server played on (the password is never logged)
 *   Admin <command>            an admin command to that server (or the player's own server's console when hosting)
 *   Scct.ImportEnhanced [folder]  imports an Enhanced SCCT Versus install's settings (no folder: this PC's profile)
 *
 * Keys and values are those of Settings/ScctSettingsConsole.h. Get and Set are also registered as Scct.Get and
 * Scct.Set; the plain names reach these commands through UScctLocalPlayer (Play/ScctLocalPlayer.h), because the
 * engine's own GET and SET (class default properties) would otherwise take them first.
 */
namespace ScctConsoleCommands
{
	/**
	 * Runs "Get ..." or "Set ..." when the key is a setting; false for anything else (the engine's own GET and SET of a
	 * class property still work).
	 */
	bool ExecSettingCommand(const TCHAR* Cmd, UWorld* World, FOutputDevice& Ar);
}
