#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"

/**
 * The main menu's dashboard (the Godot client's game/Scripts/UI/MainMenu.cs, DashboardDetailsScreen.cs and
 * BuildLabel.cs over Social/ScctDashboardSubsystem.h): the cards the main menu shows beside its entries, the What's new
 * and Announcement pages, the practice starts and the master's version line under the build label.
 */
namespace ScctDashboard
{
	/** Players online and the sessions on the master, with its announcement or message of the day. */
	TSharedRef<SWidget> MakeOnlineCard(const FScctMenuHost& Host);

	/** Your progress: name, rank, the bar to the next level, recent results and the next unlock (Account subsystem). */
	TSharedRef<SWidget> MakeProgressCard(const FScctMenuHost& Host);

	/** What's new: the newest release's highlights, and the page with the latest five. */
	TSharedRef<SWidget> MakeWhatsNewCard(const FScctMenuHost& Host);

	/** Explore: the player website and the releases on GitHub (opened in the browser). */
	TSharedRef<SWidget> MakeExploreCard(const FScctMenuHost& Host);

	/** Must be called from the Tick of a page showing the dashboard: the master is asked only while one is drawn. */
	void NoteShown();

	/** A quick offline match with three bots as a spy or a mercenary, on the map last played offline (or the first). */
	void StartPractice(const FScctMenuHost& Host, bool bMerc);

	/** "Practice on Enhanced Aquarius with three bots" for the entries' second line. */
	FString PracticeLine(bool bMerc);

	/** The master's version line for a corner under the build label ("Master 1.1.31  build ..."); empty before the bridge. */
	FString MasterLabel();

	/** The What's new page ("whatsnew") and the Announcement page ("announcement"). */
	TSharedRef<SScctMenuPage> MakeWhatsNewPage(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeAnnouncementPage(const FScctMenuHost& Host);
}
