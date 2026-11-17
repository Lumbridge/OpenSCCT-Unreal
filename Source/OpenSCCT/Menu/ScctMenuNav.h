#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class SScctDialogLayer;
class SWidget;

/**
 * How a controller works the menus, shared by every page host (the front end, the in-match menu and the pages over
 * them), after the Godot client's PadMenus (game/Scripts/Options/PadMenus.cs, PadMenuNavigation.cs):
 *
 *   - the D-pad and the left stick move the focus; the stick goes through Slate's analog navigation with the Godot
 *     client's feel (a push past 65% moves once, held it repeats after 0.4 s and then every 0.16 s);
 *   - A presses the focused control (Slate's accept);
 *   - B, Back and Start go back one page, as Escape does (Start also closes the in-match menu it opened);
 *   - LB and RB change tabs where a page has them;
 *   - a page that opens with nothing to focus gets its first focusable control.
 *
 * It also tells the player when a controller is plugged in or pulled out ("Gamepad connected", "Gamepad
 * disconnected", from Unreal's input device mapper) as a toast of the menu on screen, and asks once whether to turn
 * controller support on when a controller is connected while it is off (the answer is kept in GameUserSettings.ini,
 * [OpenSCCT.Pad] AskedToEnableController, so it is asked one time only; an unattended run never asks or keeps it).
 *
 * -ScctPadNavTest=Down,Down,A,B presses controller buttons one by one through FSlateApplication as a pad does, logs
 * the focused control after each and saves Saved/ScctShots/padnav_<n>_<step>.png. Steps: Up, Down, Left, Right
 * (D-pad), StickUp, StickDown (left stick), A, B, X, Y, Start, Back, LB, RB, Connect, Disconnect (a pretend
 * controller through the input device mapper's own connection event), PadOff (controller support off for this run,
 * not saved), Wait, Quit.
 */
namespace ScctMenuNav
{
	/** Escape, and the controller's B, Back and Start: back one page. */
	bool IsBackKey(const FKey& Key);

	/** Q / Page Up / LB is -1, E / Page Down / RB is +1, anything else 0. */
	int32 TabDirection(const FKey& Key);

	/** The controller's buttons that close the in-match menu when its own page does not have the focus. */
	const TArray<FKey>& PadCloseKeys();

	/** The first visible, enabled control under Root that takes the keyboard focus, in layout order. */
	TSharedPtr<SWidget> FindFirstFocusable(const TSharedPtr<SWidget>& Root);

	/** Installs the navigation config and starts the controller notices (once per run; later calls do nothing). */
	void Install();

	/**
	 * A menu's dialog layer that may show the controller notices and the enable question. The newest layer still on
	 * screen is used; CanAsk says whether the question may take the focus now (the in-match menu: only while open).
	 */
	void RegisterLayer(const TSharedRef<SScctDialogLayer>& Layer, TFunction<bool()> CanAsk);

	/** "Gamepad connected" or "Gamepad disconnected". */
	FString ConnectionText(bool bConnected);

	/** The focused control for a log line: its type and caption. */
	FString DescribeFocus();
}
