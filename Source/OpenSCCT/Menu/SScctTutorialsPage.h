#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"

/**
 * The Tutorials pages (gaps 54 and 101; the Godot client's UI/Tutorials screens, the original's GUI.Tutorials_Page
 * and the pages it opens), pages of the front end's page host:
 *
 *   tutorials            the hub: View HUD, Tips, Gadget Details; Spy and Mercenary Tutorial Maps, Visit a Map; the
 *                        player's progress (Menu/SScctTutorialsPage.cpp)
 *   tutorialmaps/spy     a side's tutorial maps with a done mark each; a row plays its map alone as that side
 *   tutorialmaps/merc    (Menu/ScctTutorialPlay.h), and a won tutorial marks it done and plays the next one
 *   visitmap             any Story map alone with the help paths shown, as the side picked
 *   hudlegend/spy|merc|keyboard1|keyboard2   the HUD and keyboard pictures with their labels laid over them
 *   tips/spy|merc        a side's tips; one shows its picture, legend and explanation with the player's keys
 *   gadgets/spy|merc     a side's gadgets; one shows its HUD icon, specifications and how to use it
 *                        (the reference pages: Menu/SScctTutorialReference.cpp)
 *
 * The words are the content's own (GUI.int sections Tutorials_Page, Tuto_MapsList, Visit_Map_Set, View_Hud_Spy,
 * View_Hud_Sentry, View_KeyboardConfigOne, Tutorials_List, Tutorials_Tuto, Tuto_Gadgets, Tuto_OneGadget), with English
 * fallbacks; the pictures are the content's Tutorials_PC, GUIContent and SHudTextures textures.
 */
namespace ScctTutorialPages
{
	/** The page of that name (the part before any '/'), or null when the name is not one of these. */
	TSharedPtr<SScctMenuPage> Make(const FString& Name, const FScctMenuHost& Host);

	TSharedRef<SScctMenuPage> MakeTutorials(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeTutorialMaps(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeVisitMap(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeHudLegend(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeTips(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeGadgets(const FScctMenuHost& Host);
}

/** The tutorial pages' words: GUI.int as written, with its '|' line breaks and its ("a","b") lists. */
namespace ScctTutorialText
{
	/** [Section] Key of GUI.int with '|' as line breaks, or Fallback (also with '|' as line breaks). */
	FString Lines(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback);

	/** A GUI.int list ("(\"a\",\"b\")"), quoted items unescaped; empty without the content. */
	TArray<FString> List(const TCHAR* Section, const TCHAR* Key);

	/** One item of a list with '|' as line breaks, or an empty string. */
	FString Item(const TCHAR* Section, const TCHAR* Key, int32 Index);

	/** The key tokens of a text ("\USE") as the side's keys (the bridge's PlayExports.ExpandKeys). */
	FString WithKeys(const FString& Text, bool bMerc);
}
