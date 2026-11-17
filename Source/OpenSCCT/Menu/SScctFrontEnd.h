#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Runtime/ScctOnlineApi.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SEditableTextBox;
class SScrollBox;
class SVerticalBox;
class UScctOnlineSubsystem;

/**
 * The front end: the pages of the main menu and, while the player visits a server, the visit's pages (connecting,
 * the lobby, travelling, a failure). It is one widget whose middle is rebuilt when the page changes; the lobby's
 * player lists and chat are refilled whenever the online subsystem's revision changes.
 *
 *   Home      play offline, join by address, server list, quit
 *   Offline   map, mode, side, bots and their skill, start
 *   Join      name, address, password, side
 *   Servers   the master's list, refresh, sign in
 *   Visit     shown instead of the above while UScctOnlineSubsystem::IsVisiting
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

	/** Unattended: shows a page by name ("offline", "join", "servers"), for screenshots. */
	void ShowPageNamed(const FString& Name);

private:
	enum class EPage : uint8
	{
		Home,
		Offline,
		Join,
		Servers,
		Visit,
	};

	/** What the visit page is showing; a change rebuilds it. */
	enum class EVisitView : uint8
	{
		None,
		Connecting,
		Lobby,
		Travelling,
		Round,
		Failed,
	};

	void ShowPage(EPage NewPage);
	FReply HandleShowPage(EPage Target);
	void Rebuild();
	EVisitView FindVisitView() const;

	TSharedRef<SWidget> BuildHome();
	TSharedRef<SWidget> BuildOffline();
	TSharedRef<SWidget> BuildJoin();
	TSharedRef<SWidget> BuildServers();
	TSharedRef<SWidget> BuildVisit();
	TSharedRef<SWidget> BuildLobby();

	void RefillLobby();
	void RefillServers();
	void StartOffline();
	void StartJoin();
	void SendChat();
	void PickNextMap();

	/** A row of buttons of which one is picked: Value is set to the index clicked. */
	TSharedRef<SWidget> Choice(const TArray<FString>& Labels, int32& Value, const FLinearColor& Colour);

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SBox> PageBox;
	EPage Page = EPage::Home;
	EPage PageBeforeVisit = EPage::Home;
	EVisitView VisitView = EVisitView::None;
	int32 SeenRevision = -1;

	// Offline page.
	TArray<FString> Maps;
	int32 MapIndex = 0;
	int32 ModeIndex = 0;
	int32 SideIndex = 0;
	int32 BotsIndex = 3;
	int32 SkillIndex = 1;

	// Join page.
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> AddressBox;
	TSharedPtr<SEditableTextBox> PasswordBox;
	int32 JoinSideIndex = 0;

	// Servers page.
	TSharedPtr<SVerticalBox> ServerRows;

	// Lobby.
	TSharedPtr<SVerticalBox> TeamRows[3];
	TSharedPtr<SScrollBox> ChatRows;
	TSharedPtr<SEditableTextBox> ChatBox;
	int32 ChatLinesShown = 0;
};
