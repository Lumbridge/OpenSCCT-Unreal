#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"

class SOverlay;
class SScctGearPage;
class SScctStrategicMapPage;
class UScctOnlineSubsystem;

/**
 * The Equipment page (Menu/SScctGearPage.h) and the strategic map (Menu/SScctStrategicMapPage.h) in the front end.
 * Both open over a layer of whatever shows them:
 *
 *   gear[/spy|merc]   a page of the front end's stack (EQUIPMENT on the offline page; the side given, else the spy)
 *   map/<Map>         a page of the front end's stack: a map's overview, without markers
 *   the lobby         EQUIPMENT (under the lobby's rules, for the player's side) and STRATEGIC MAP (the lobby's map),
 *                     each over the lobby page itself, which the front end shows in place of its stack
 *
 * The in-match menu opens them over itself (Menu/SScctMatchMenuEntries.cpp).
 */
namespace ScctOverlayPages
{
	/** "Your gear" with the side's kit in one line, and the EQUIPMENT and STRATEGIC MAP buttons (the lobby). */
	TSharedRef<SWidget> MakeRow(const TWeakObjectPtr<UScctOnlineSubsystem>& Online, TFunction<void()> OnGear, TFunction<void()> OnMap);

	/** The Equipment page over Layer under the lobby's rules, on the player's side (the spy's while spectating). */
	TSharedPtr<SScctGearPage> OpenGear(const TSharedPtr<SOverlay>& Layer, const FScctMenuHost& Host);

	/** The lobby map's strategic map over Layer. */
	TSharedPtr<SScctStrategicMapPage> OpenStrategicMap(const TSharedPtr<SOverlay>& Layer);

	/** Escape or B: the page over Layer goes back (the Equipment page asks first when its drafts were changed). False when none is open. */
	bool Back(const TSharedPtr<SOverlay>& Layer);

	/** The front end's own gear and map pages, while one is on show (unattended switches act on them). */
	TSharedPtr<SScctGearPage> FindFrontEndGear();
	TSharedPtr<SScctStrategicMapPage> FindFrontEndStrategicMap();
}

namespace ScctMenuPages
{
	/** The front end's "gear" and "map" pages (ScctMenuPages::Make). */
	TSharedRef<SScctMenuPage> MakeGear(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeStrategicMap(const FScctMenuHost& Host);
}
