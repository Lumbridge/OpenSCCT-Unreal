#pragma once

#include "CoreMinimal.h"
#include "Online/ScctHostSetup.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SEditableTextBox;
class SScctMapPicker;
class UScctOnlineSubsystem;

/**
 * Create Session (the Godot client's HostScreen and CreateSessionModel, Enhanced 4.0's Create_Match page): the map
 * list with pictures on the left; on the right the session's name, its rules and the hosting choices in three tabs.
 *
 *   GAME      session name, game mode, game length, voice chat; player slots, bots (off, per side, fill slots,
 *             custom teams), bots per side or bot pairs, spy and mercenary bots, bot skill
 *   RULES     spy lives (kills to win in Merc Deathmatch), mercenary lives, respawn, active disks and disks to take
 *             (Disk Hunt); revenge, games in a row, random map, auto balance, voice in lobby; practice and
 *             alternate rules (each side's abilities, gadgets and slots)
 *   HOSTING   password, listed on the master, signed-in players only, UPnP, UDP port, LAN beacon, record demos
 *
 * The rules are OpenSCCT's own (the bridge runs CreateSessionModel: ranges, which rows show in which mode, what a
 * change of map or mode reloads); the page holds the match as the setup line FScctHostRequest::SetupOptions takes.
 * The hosting choices start from the Online settings page and are kept there on CREATE, as is the last setup.
 * CREATE hosts the game (UScctOnlineSubsystem::Host) and the front end then shows the lobby; a server that cannot
 * start (the port is in use, the map is missing) leaves the page up with the reason under the buttons.
 */
class SScctHostPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHostPage) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_ARGUMENT(TWeakPtr<class SScctDialogLayer>, Dialogs)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * Unattended (-ScctHostPreset): fills the form from an options line. MapName and Mode (a mode key) pick the map
	 * and mode with their defaults; any setup value (TimeLimit, SvmBots, LivesATT...) goes on top; Name, Password,
	 * Port, Players, Listed, Upnp, Accounts, Demos and Lan set the session and hosting fields, Loopback=1 binds the
	 * loopback addresses only (a test run: no firewall prompt, no router, no master).
	 */
	void ApplyPreset(const FString& Preset);

	/** CREATE: hosts the game. False when it could not start (the reason is shown on the page). -ScctHostSubmit[=S] presses it S seconds (default 2) after the page is first up. */
	bool Create();

	const FString& GetSetup() const { return Setup; }

private:
	enum class ETab : uint8 { Game, Rules, Hosting };

	void Edit(const FString& Change);
	void EditAlternate(const FString& Change);
	void PickMap(const FString& Map);
	const FScctHostMap* FindMap(const FString& Id) const;
	void SetSetup(const FString& Line);
	void Refresh();
	int32 PlayersArgument() const { return bPlayersChosen ? FCString::Atoi(*ScctHostSetup::Option(Setup, TEXT("MaxPlayers"))) : 0; }

	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> BuildAlternate();
	TSharedRef<SWidget> RowWidget(const FString& Name);
	TSharedRef<SWidget> Toggle(const FString& Caption, bool& Value, const TAttribute<bool>& Enabled = true);
	void AddRows(const TSharedRef<class SVerticalBox>& Box, std::initializer_list<const TCHAR*> Names);
	const FScctSetupRow* FindRow(const FString& Name) const;

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FSimpleDelegate OnBack;

	/** The match as an options line (LobbySetup.ToOptions). */
	FString Setup;
	/** The host picked the player slots; until then they follow the map. */
	bool bPlayersChosen = false;
	TArray<FScctSetupRow> Rows;
	FString VoiceHint, BotHint, Description;

	TArray<FScctHostMap> Maps;
	TSharedPtr<SScctMapPicker> Picker;
	TSharedPtr<SBox> SettingsBox;
	ETab Tab = ETab::Game;
	bool bAlternateOpen = false;

	// The session and hosting fields (kept across rebuilds of the tabs).
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> PasswordBox;
	TSharedPtr<SEditableTextBox> PortBox;
	bool bListed = true;
	bool bRequireAccounts = false;
	bool bUpnp = true;
	bool bLan = true;
	bool bRecordDemos = false;
	bool bLoopbackOnly = false;
	int32 LanPort = 0;

	/** Why the last CREATE failed, or empty. */
	FString Error;

	/** -ScctHostSubmit: seconds until CREATE is pressed, negative when it is not to be. */
	float SubmitAfter = -1.0f;
};
