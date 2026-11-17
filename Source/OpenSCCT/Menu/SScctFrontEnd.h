#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Runtime/ScctOnlineApi.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SScctDialogLayer;
class SScctMenuPage;
class UScctOnlineSubsystem;

/**
 * The front end: a stack of pages (Menu/ScctMenuPage.h) over the backdrop, and, while the player visits a server,
 * the visit's own views (connecting, the lobby, travelling, a failure) in place of the stack. It draws what every
 * page shares: the page's title, the key hints, the build label, the dialogs and the toasts; it moves between the
 * pages and goes back on Escape or the controller's B.
 *
 *   home      the main menu (Menu/SScctMenuHome.cpp)
 *   play      an offline match: map, mode, side, bots, rules (Menu/SScctMenuPlay.cpp)
 *   online    the server list and joining by address (Menu/SScctMenuOnline.cpp)
 *   settings  video, audio, controls, bindings, game, profile (Menu/SScctSettingsPanel.cpp)
 *   credits   the people and the licences (Menu/SScctMenuCredits.cpp)
 *   account   sign-in, the claimed name, rank, XP, prestige, titles and career (Menu/SScctMenuSocial.cpp)
 *   password  a friend's or the website's join to a server with a password (Menu/SScctMenuSocial.cpp)
 *   the visit connecting, the lobby (Menu/SScctMenuLobby.cpp), travelling to a round, a failure
 */
class SScctFrontEnd : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctFrontEnd) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * Shows a page by name, over the main menu: "play", "online", "online/address", "settings/audio", "credits",
	 * "quit" (the main menu with the quit question), "home"; "start" opens the offline page and starts its match.
	 */
	void ShowPageNamed(const FString& Name);

private:
	/** What the visit is showing; a change rebuilds it. */
	enum class EVisitView : uint8
	{
		None,
		Connecting,
		Lobby,
		Travelling,
		Round,
		Failed,
	};

	void Open(const FString& Name);
	void Push(const TSharedRef<SScctMenuPage>& Page);
	void GoBack();
	void AskQuit();
	void Present();
	TSharedPtr<SScctMenuPage> GetTopPage() const;

	EVisitView FindVisitView() const;
	TSharedRef<SWidget> BuildVisit();
	TSharedRef<SWidget> BuildFooter();

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SScctDialogLayer> Dialogs;
	TSharedPtr<SBox> PageBox;
	TArray<TSharedPtr<SScctMenuPage>> Stack;

	/** Per page under the top one, the control that had the focus when the page above was opened. */
	TArray<TWeakPtr<SWidget>> CoveredFocus;

	/** Back on a page: the control the focus returns to, instead of the page's default. */
	TWeakPtr<SWidget> ReturnFocus;

	/** The lobby while it is shown (a page of its own, over nothing). */
	TSharedPtr<SScctMenuPage> LobbyPage;

	/** The other views of the visit (connecting, travelling, a failure). */
	TSharedPtr<SScctMenuPage> VisitPage;

	/** Per page of the stack, the name it was opened by ("online", "hub"), for the Discord status. */
	TArray<FString> StackNames;

	/** Tells UScctSocialSubsystem which page is shown, and takes a friend's join's password prompt or page to open. */
	void TickSocial();
	void ReportSocialPage();

	EVisitView VisitView = EVisitView::None;
	float Shown = 0.0f;
	bool bFocusPending = false;
};
