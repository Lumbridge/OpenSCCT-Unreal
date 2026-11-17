#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Runtime/ScctBrowserApi.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SVerticalBox;
class SWrapBox;
class UScctOnlineSubsystem;

/**
 * The server list (the Godot client's ServerBrowserScreen): the master's servers, a LAN search and the player's saved
 * servers in one table (Runtime/ScctBrowserApi.h), each with a measured ping. The columns are Godot's, with the
 * master's region added: Server (with the PASSWORD, DEDICATED, ACCOUNT and PRACTICE marks), Map, Mode, Players, Ping,
 * State (the session's phase), Region, Reached by (Internet direct, NAT punch or relay, LAN, saved) and Joinable (or
 * why not: another build, a missing map, another version of it, sign in, full; such rows are greyed). A click on a
 * header sorts by that column, a second click turns the order round. A click on a row selects it and shows its
 * players by side under the table.
 *
 * Joining: a side row (AUTO, SPY, MERCENARY, WATCH) and the connect path (AUTO, DIRECT, NAT PUNCH, RELAY: the Online
 * page's Connect mode) are above JOIN. Selecting a server with a password opens the password prompt under the table,
 * with the keyboard in it (Enter joins). Join goes the way the row is reached: a listed server through the master, a
 * LAN or saved one straight to its address.
 *
 * Below: the master server and invite key (the Online page's values, APPLY saves them and searches again), an address
 * line that joins (and saves) a typed address or only saves it, the saved servers with JOIN and REMOVE each, and the
 * account (the official servers take signed-in players only).
 *
 * Unattended switches:
 *   -ScctBrowserJoin=N        select the Nth row of the table as sorted once it has answered (or after 8 s), then type
 *                             -ScctBrowserPassword=<pw> into the prompt and press JOIN on the -ScctSide side
 *                             (spy|merc|watch; default AUTO); each step waits 2 s so a screenshot can show it
 *   -ScctBrowserSort=<column>[,desc]  sort by server, map, mode, players, ping, state, region, reach or joinable
 *   -ScctSaveServer=<address>, -ScctForgetServer=<address>  add an address to the saved servers, or remove it, as the
 *                             page opens (SAVE ADDRESS and REMOVE do the same)
 */
class SScctServerBrowserPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctServerBrowserPage) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		/** BACK: the front end shows its home page. */
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctServerBrowserPage() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	enum class EColumn : uint8
	{
		Server,
		Map,
		Mode,
		Players,
		Ping,
		State,
		Region,
		Reach,
		Joinable,
		Count,
	};

	/** A new search: the master's list, the LAN and the saved servers. */
	void Refresh();

	/** Reads the rows again and makes the table, sorted, keeping the selection. */
	void Refill();
	void Sort();
	FReply HandleHeader(EColumn Column);
	void Select(const FString& Key);

	/** The selected row, or null; its index in the bridge's order in OutIndex. */
	const FScctBrowserRow* FindSelected(int32* OutIndex = nullptr) const;

	void JoinSelected();
	void JoinAddress(const FString& Address, bool bSave);
	void SaveAddress();
	void RefillSaved();
	void ApplyMaster();

	/** -1 the smaller side, 0 spies, 1 mercs, 255 watch, from the side row. */
	int32 PreferredTeam() const;

	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildRow(int32 Index);
	TSharedRef<SWidget> BuildJoinBar();
	TSharedRef<SWidget> BuildAddressBar();
	TSharedRef<SWidget> BuildAccount();

	FString CellText(const FScctBrowserRow& Row, EColumn Column) const;

	void ReadSwitches();
	void RunUnattended(float DeltaSeconds);
	void LogRows();

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FSimpleDelegate OnBack;

	/** The rows in the bridge's order, and the order shown. */
	TArray<FScctBrowserRow> Rows;
	TArray<int32> Order;
	EColumn SortColumn = EColumn::Ping;
	bool bDescending = false;
	FString SelectedKey;
	/** The selected row's key when the password prompt last took the keyboard. */
	FString PromptedKey;
	int32 SideIndex = 0;
	int32 SeenRevision = -2;
	FString Message;
	bool bMessageIsError = false;

	TSharedPtr<SVerticalBox> RowBox;
	TSharedPtr<SWrapBox> SavedBox;
	TSharedPtr<SEditableTextBox> MasterBox;
	TSharedPtr<SEditableTextBox> InviteBox;
	TSharedPtr<SEditableTextBox> AddressBox;
	TSharedPtr<SEditableTextBox> PasswordBox;

	/** The rows as last logged (without the pings), so a change is logged once. */
	FString LoggedRows;
	double LoggedPingsAt = 0.0;

	// Unattended.
	int32 AutoJoinIndex = -1;
	FString AutoPassword;
	int32 AutoStep = 0;
	float AutoClock = 0.0f;
	float AutoStepClock = 0.0f;
};
