#pragma once

#include "CoreMinimal.h"
#include "Online/ScctHostSetup.h"
#include "Runtime/ScctOnlineApi.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SEditableTextBox;
class SScrollBox;
class SScctMapPicker;
class SVerticalBox;
class UScctOnlineSubsystem;
class UTexture2D;
struct FSlateBrush;

/**
 * The lobby of a server the player visits or hosts (the Godot client's LobbyRoomScreen and OnlineLobby): the setup's
 * map picture and rules, a line about who has that map, the two sides and the spectators, the chat, and the buttons
 * (side, ready, start, session options, leave). It refills its rows when the online subsystem's revision changes.
 *
 * Launch feedback (OnlineLobby's map line, the server's LaunchProblem): who lacks the setup's map or has another
 * version of it (FScctOnlinePlayer::MapState), whether this game has a level for it (a round on a map with no imported
 * level here sits the local player out), and, under START, why a launch would be refused now (nobody on a side, a
 * map missing, someone not ready); START is off while that is so.
 *
 * Session Options (LobbyRoomScreen.Settings, SessionRowsView): the map picker and the setup's rows in GAME and RULES
 * tabs with the Alternate rules page, drawn from the server's setup line (EScctOnlineText::SetupOptions) through the
 * bridge's CreateSessionModel. Whoever may change the setup (CanControl) edits a draft: APPLY sends only the values
 * that differ from the server's (SetSetupOptions; the server un-readies the players), DISCARD goes back to the
 * server's. Everyone else sees the same rows read-only. RANDOM MAP on the lobby itself draws a map that offers the
 * mode and sets it with its own rules (SetMatch).
 *
 * Players (LobbyRoomScreen's rows): each row has a rank slot (the packed rank as "P2 41" with its title in the
 * tooltip; the column is there only while somebody has a rank), the name, the HOST and ADMIN markers, the state and
 * ping, the map check and VOICE BANNED. For the host or an admin (CanModerate) every other person's row has MANAGE,
 * which opens the moderation bar under the sides: KICK, BAN and VOICE BAN (VOICE UNBAN when banned), each asked
 * again as Godot does ("Do you want to kick NAME?") before it is sent. The bar is outside the rows, which are made
 * again on every change of the lobby, so a ping update does not close it.
 *
 * Chat (LobbyRoomScreen.SendChat, ChatInput): ALL or TEAM, switched by the button beside the line or with Tab while
 * typing; a team line needs a side. Team lines are marked (TEAM) in the side's colour. Admin box: on a dedicated
 * server a password line logs in as an admin (AdminLogin), then the same line sends console commands (AdminCommand);
 * on the player's own server it is the host's console at once. The answers come back as system chat lines.
 *
 * Unattended switches:
 *   -ScctLobbyManage=<name>[,kick|ban|voiceban]  open the moderation bar for that player when he is in the lobby,
 *                                 and with an action its question too (YES is never pressed: -ScctKickAfter does that)
 *   -ScctLobbyTeamChat            the chat starts on the TEAM channel
 *   -ScctLobbyOptions             open Session Options when the lobby is first shown (any player)
 *   -ScctLobbyApply=<line>        the controller opens Session Options and puts an options line into the draft:
 *                                 MapName and Mode (a mode key) or GameMode (a number) first, with their rules, then any
 *                                 setup value ("?MapName=SteeE?Mode=DiskHunt?TimeLimit=5"); Tab=rules|alternate picks the tab
 *   -ScctLobbyApplyAfter=S        seconds (on the clock) before APPLY is pressed, counted from when enough players are
 *                                 in the lobby (default 4)
 *   -ScctLobbyApplyWhen=N         how many players that is (default 1: the host alone)
 */
class SScctLobbyPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctLobbyPage) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctLobbyPage() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Unattended: puts an options line into the draft (see -ScctLobbyApply) and opens Session Options. */
	void FillDraft(const FString& Line);

	/** APPLY: sends the draft's changed values. False when nothing was sent. */
	bool Apply();

	/** DISCARD: the draft goes back to the server's setup. */
	void Discard();

private:
	enum class ETab : uint8 { Game, Rules };

	/** The online subsystem's revision moved: the rows, the setup and the picture follow it. */
	void HandleChanged();
	bool CanControl() const;
	bool IsDirty() const { return !Draft.IsEmpty() && Draft != Baseline; }

	/** The server's setup changed (or was first read): the baseline follows it, and so does an untouched draft. True when it changed. */
	bool SyncSetup();
	void Edit(const FString& Change);
	void EditAlternate(const FString& Change);
	void SetDraft(const FString& Line);
	int32 PlayersOf(const FString& Line) const;

	/** The names whose values differ between the draft and the server's setup, as an options line. */
	FString ChangedOptions(TArray<FString>* OutNames = nullptr) const;

	void ShowOptions(bool bOpen);
	void Rebuild();
	void RefillLobby();
	void RefreshOptions();
	void SendChat();
	void RandomMap();

	// Players, moderation, chat channel and the admin box.
	enum class EModerate : uint8 { None, Kick, Ban, VoiceBan, VoiceUnban };
	TSharedRef<SWidget> PlayerRow(const FScctOnlinePlayer& Player, bool bRanks);
	TSharedRef<SWidget> ModerationBar();
	TSharedRef<SWidget> AdminBox();
	void Manage(int32 PlayerId);
	void Ask(EModerate Action);
	void Answer(bool bYes);
	void ToggleChannel();
	void SendAdmin();
	FReply HandleChatKey(const FGeometry& Geometry, const FKeyEvent& Key);
	bool ShowsAdminBox() const;

	TSharedRef<SWidget> BuildLobby();
	TSharedRef<SWidget> BuildOptions();
	TSharedRef<SWidget> BuildRows();
	TSharedRef<SWidget> MapPicture(float Width);
	void LoadPicture(const FString& MapId);

	/** The map line: who lacks the setup's map or has another version, and whether a launch would sit the player out. */
	FString MapLine(bool& bProblem) const;

	/** Why START would be refused now (the server's LaunchProblem as the lobby can see it), or empty. */
	FString LaunchProblem() const;

	const FScctHostMap* FindMap(const FString& Id) const;
	/** The map the server's setup plays (what the lobby shows), and the one the Options page shows (the draft's for a controller). */
	FString ServerMap() const;
	FString ShownMap() const;

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FDelegateHandle ChangedHandle;
	TSharedPtr<SBox> Body;
	int32 SeenRevision = -1;
	bool bOptionsOpen = false;
	bool bAlternateOpen = false;
	ETab Tab = ETab::Game;

	// The lobby.
	TSharedPtr<SVerticalBox> TeamRows[3];
	TSharedPtr<SScrollBox> ChatRows;
	TSharedPtr<SEditableTextBox> ChatBox;
	int32 ChatLinesShown = 0;
	/** The chat goes to the player's own side only. */
	bool bTeamChat = false;
	/** Said under the chat line about the last send (a team line without a side). */
	FString ChatNotice;
	TSharedPtr<SEditableTextBox> AdminLine;
	/** The player the moderation bar is open for (-1: closed), and the action it asks about. */
	int32 ManagedPlayer = -1;
	FString ManagedName;
	EModerate Asking = EModerate::None;

	// Unattended moderation bar.
	FString PendingManage;
	FString PendingManageAction;

	// Session Options: the server's setup normalized by the model, and the controller's draft of the next one.
	FString ServerSetup;
	FString Baseline;
	FString Draft;
	/** The server's setup changed under a draft with changes of its own. */
	bool bDraftConflict = false;
	/** APPLY was sent: the next setup from the server replaces the draft (it may clamp a value). */
	bool bAwaitingEcho = false;
	bool bControlShown = false;
	TArray<FScctSetupRow> Rows;
	FString VoiceHint, BotHint, DraftDescription;
	TSharedPtr<SBox> RowsBox;
	TSharedPtr<SScctMapPicker> Picker;

	/** The hostable maps with a level here (pictures, names, modes). */
	TArray<FScctHostMap> Maps;
	TSet<FString> ImportedMaps;

	/** The setup map's picture, kept from garbage collection while the page lives. */
	FString PictureMap;
	TStrongObjectPtr<UTexture2D> PictureTexture;
	TSharedPtr<FSlateBrush> PictureBrush;

	// Unattended.
	FString PendingApply;
	float ApplyAfter = -1.0f;
	int32 ApplyWhen = 1;
	double ApplyAt = -1.0;
};
