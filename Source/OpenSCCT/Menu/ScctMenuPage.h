#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuKit.h"
#include "Widgets/SCompoundWidget.h"

class SScctDialogLayer;
class UScctOnlineSubsystem;

/**
 * What a page may ask of whatever shows it: the front end (Menu/SScctFrontEnd.h) or the match overlay
 * (Menu/ScctMatchMenuWidget.cpp).
 */
struct FScctMenuHost
{
	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SScctDialogLayer> Dialogs;

	/** Opens a page by name on top of this one ("play", "online", "settings/audio", ...). */
	TFunction<void(const FString&)> Open;

	/** Closes this page (as Escape does). */
	TFunction<void()> Back;

	/** The page is shown over a match (the pause menu's settings): nothing that would leave the match is offered. */
	bool bInMatch = false;
};

/**
 * A page of the menus: a title over its content. The host draws the title, the key hints and the backdrop, moves
 * between pages and handles Escape and the tab keys, asking the page first.
 */
class SScctMenuPage : public SCompoundWidget
{
public:
	/** The title the host shows above the page ("PLAY OFFLINE"); empty for a page that draws its own (the main menu). */
	virtual FString GetTitle() const = 0;

	/** A small line above the title ("OFFLINE · SKIRMISH"). */
	virtual FString GetKicker() const { return FString(); }

	/** Takes the focus when the page is shown. */
	virtual TSharedPtr<SWidget> GetDefaultFocus() const { return DefaultFocus.Pin(); }

	/** Escape or B: true when the page closed something of its own (a capture, a drawer) and stays. */
	virtual bool HandleBack() { return false; }

	/** Q/E, Page Up/Down, the shoulders. */
	virtual bool HasTabs() const { return false; }
	virtual void StepTab(int32 Direction) {}

	/** The part after the page's name in "settings/audio": a tab, a state. */
	virtual void ShowSub(const FString& Sub) {}

	/** The page is about to be closed: write what it changed. */
	virtual void OnClosing() {}

	/** The page's own key hints, added after Select and Back ("[R] Refresh"). */
	virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const {}

	/** A key no control of the page took (a shortcut such as R for refresh). */
	virtual bool HandleShortcut(const FKey& Key) { return false; }

	/** The arrows and the pad move the focus as everywhere in the menus (ScctUi::Navigate). */
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override { return ScctUi::Navigate(InNavigationEvent); }

protected:
	FScctMenuHost Host;
	TWeakPtr<SWidget> DefaultFocus;
};

namespace ScctMenuPages
{
	/**
	 * The front end's pages by name: home, play, online, address, host, hub, settings, credits, gear and map
	 * (Menu/ScctOverlayPages.h). Null for an unknown name.
	 */
	TSharedPtr<SScctMenuPage> Make(const FString& Name, const FScctMenuHost& Host);

	TSharedRef<SScctMenuPage> MakeHome(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakePlay(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeOnline(const FScctMenuHost& Host, bool bAddress);
	TSharedRef<SScctMenuPage> MakeSettings(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeCredits(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeLobby(const FScctMenuHost& Host);

	/** Create Session (Menu/SScctHostPage.h) and the hub list (Menu/SScctHubPage.h): Menu/SScctMenuHostHub.cpp. */
	TSharedRef<SScctMenuPage> MakeHostGame(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeHub(const FScctMenuHost& Host);
}

namespace ScctUi
{
	/** A string of the content's GUI.int, or Fallback (Menu/ScctMenuSubsystem.h). */
	FString GameText(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback);

	/**
	 * A GUI.int caption as a row label in the menus' sentence case: "Effects Volume" reads "Effects volume", "Game
	 * Length (min)" reads "Game length" (the value shows the unit). Short capitals (X, PMC) and words in brackets stay.
	 */
	FString GameLabel(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback);

	/** One entry of a GUI.int list ("(\"Spy\",\"Mercenary\")"), or Fallback. */
	FString GameTextItem(const TCHAR* Section, const TCHAR* Key, int32 Index, const TCHAR* Fallback);

	/** A card: a panel with a small title. */
	TSharedRef<SWidget> Card(const FString& Title, const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(24.0f, 20.0f));

	/** The help pane: what the focused control does (ScctUi::Describe). */
	TSharedRef<SWidget> DescriptionPane();

	/** Asks a yes/no question; Yes runs OnYes. The No button has the focus and is what Escape presses. */
	void Confirm(const TSharedPtr<SScctDialogLayer>& Dialogs, const FString& Title, const FString& Body, const FString& YesLabel, const FString& NoLabel, TFunction<void()> OnYes, bool bDanger = false);

	/** "OpenSCCT 0.8.150 · Unreal" for a corner of a page. */
	FString BuildLabel();
}
