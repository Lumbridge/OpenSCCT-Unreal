#pragma once

#include "CoreMinimal.h"

class SScctDialogLayer;

/**
 * The Godot client's BotNavigationPrompt: before an offline match or a hosted game starts with bots, the map's
 * prebuilt bot navigation is offered from OpenSCCT-Content when no prepared graphs are cached ("Download and play",
 * "Build locally", "Cancel"), so the bots need not build their routes on this PC first. The download's progress is
 * shown in the dialog; when it is done (or the player builds locally, or nothing is offered) the start goes on.
 *
 * Unattended switch:
 *   -ScctBotNavAnswer=download|local|cancel   answers the question two seconds after it is shown
 */
namespace ScctBotNavPrompt
{
	/**
	 * Offers the map's prebuilt navigation, then runs Launch (at once when nothing is offered). StillValid is asked
	 * while the question is up: false drops the start (the lobby changed its map, the player left).
	 */
	void OfferThenLaunch(const TSharedPtr<SScctDialogLayer>& Dialogs, const FString& Map, TFunction<bool()> StillValid, TFunction<void()> Launch);
}
