#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"

/**
 * Help & feedback (the Godot client's UI/HelpFeedbackScreen.cs), a page of the front end ("feedback") and of the in-match
 * menu (its HELP & FEEDBACK entry):
 *   - REPORT A BUG opens the project website's report form in the browser, prefilled with the report details; SUGGEST A
 *     FEATURE and BROWSE EXISTING ISSUES open GitHub;
 *   - CAPTURE GAME SCREENSHOT saves the game view without the menus (Saved/Screenshots/report-<time>.png); with ATTACH THE
 *     SCREENSHOT on, REPORT A BUG first uploads it to the website's report capture endpoint and the form opens with it.
 *     Nothing is uploaded until the player presses REPORT A BUG, and never in an unattended run;
 *   - the report details (Runtime/ScctReportApi.h: version, build, session, map, mode, side and the local player's position
 *     and movement state, as GameReportContext.cs) and a summary of the display settings, with COPY DETAILS. No logs,
 *     account details or server addresses are part of it;
 *   - the documentation and the contribution guides.
 * The page logs its report text when it opens. In an unattended or scripted run links are logged, not opened.
 */
namespace ScctHelpFeedback
{
	TSharedRef<SScctMenuPage> MakePage(const FScctMenuHost& Host);

	/** The report as the page shows and copies it: the bridge's details, then the settings summary. */
	FString ReportText();
}
