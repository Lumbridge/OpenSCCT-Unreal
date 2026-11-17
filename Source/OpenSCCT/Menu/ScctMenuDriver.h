#pragma once

#include "CoreMinimal.h"

/**
 * Works the menus with nobody at the keyboard, for pictures of every page and state and for checks that a control
 * does what it says. Off unless the command line has
 *
 *   -ScctMenuDo="page settings;key Down;key Right 3;shot settings_video;press BACK;quit"
 *
 * The commands, separated by semicolons, run in order, one per frame at most:
 *   wait S          let S seconds pass (the script's clock only runs while a menu is on screen)
 *   page NAME       show a page of the front end by name (see SScctMenuRoot::ShowPageNamed: "play", "online",
 *                   "online/address", "settings", "settings/audio", "credits", "quit", ...)
 *   key NAME [N]    press and release a key N times (Unreal's key names: Down, Enter, Escape, Q,
 *                   Gamepad_FaceButton_Bottom, Gamepad_DPad_Down ...); it goes to the focused control as a real key does
 *   press LABEL     press the button or row with that caption (the newest when several have it)
 *   focus LABEL     give it the keyboard focus
 *   hover LABEL     draw it as under the pointer until "hover" with another caption or none
 *   type TEXT       type into the focused text field
 *   shot NAME       save Saved/ScctShots/NAME.png (or under -ScctShotDir), the interface included
 *   where           log which control has the keyboard focus
 *   log TEXT        a line in the log
 *   quit            end the game
 * The script goes on through level changes: the front end and the match overlay both run it.
 */
class FScctMenuDriver
{
public:
	/** What the menu on screen lets a script do besides keys and presses. */
	struct FHost
	{
		TFunction<void(const FString&)> ShowPage;
	};

	/** Once a frame from whichever menu is on screen. */
	static void Tick(float DeltaSeconds, const FHost& Host);

	/** A script is given (whether or not it has finished): menus then skip what would wait for a person. */
	static bool IsScripted();
};
