#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SVerticalBox;
class UScctHubSubsystem;
class UScctOnlineSubsystem;

/**
 * The front end's Hub page (the Godot client's HubScreen): the visit (the name, the side to arrive as), the master's
 * hub list (the official hubs first, each with its region and ping, then the hubs players host), a hub's address to
 * join, and hosting a hub on this PC. JOIN takes the hub picked in the list, or the typed address when none is picked
 * (typing in the address field drops the pick). While a visit connects the page says how it goes; a hub that refuses
 * the player or never answers brings him back here with the reason.
 *
 * Unattended (for screenshots and checks with nobody at the keyboard):
 *   -ScctHubPress=host|join|refresh   presses that button once, two seconds after the page is shown
 *   -ScctHubPress=joinlisted          picks the first hub of the list that can be joined and presses JOIN
 *   -ScctHubAddress=<host[:port]>     the address the page starts with (what JOIN takes with nothing picked)
 */
class SScctHubPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHubPage) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctHubSubsystem>, Hub)
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		/** BACK: the page has left the hub (a visit still connecting, a hub this PC opened); the front end goes home. */
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctHubPage() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void RefillHubs();
	void Join();
	void JoinAddress();
	void Host();
	void Refresh();
	void Back();

	/** The typed name goes to the online pages' saved name, which every visit uses. */
	void SaveName();

	/** The line under the panels: the visit's status or failure, or the page's own message. */
	FText StatusLine() const;
	FSlateColor StatusColour() const;

	TWeakObjectPtr<UScctHubSubsystem> Hub;
	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FSimpleDelegate OnBack;
	FDelegateHandle ChangedHandle;

	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> AddressBox;
	TSharedPtr<SVerticalBox> HubRows;
	int32 SideIndex = 0;

	/** The master's id of the hub picked in the list; empty when none is (JOIN takes the address). */
	FString PickedId;
	FString Message;
	FString SeenAddress;
	bool bListDirty = true;

	FString AutoPress;
	float AutoPressTimer = 2.0f;
};
