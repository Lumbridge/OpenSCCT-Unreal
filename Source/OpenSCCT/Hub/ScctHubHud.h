#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctHubSocialApi.h"

class AScctPlayerController;
class SScctHubHud;
class UScctHubSubsystem;
class UWorld;

/** A name tag as the HUD draws it: where over the head (UE units), the side's colour and the lines. */
struct FScctHubHudTag
{
	FVector World = FVector::ZeroVector;
	float Distance = 0.0f;
	int32 Team = 0;
	int32 Rank = 0; // packed, without the title; 0 for a guest
	FString Name;
	FString Caption;
	FString Bubble;
};

/** A world sign as the HUD draws it (projected text): where, how tall a line stands in the world, its colour. */
struct FScctHubHudSign
{
	FVector World = FVector::ZeroVector;
	float LineHeight = 36.0f;
	float Alpha = 1.0f;
	FLinearColor Colour = FLinearColor::White;
	FString Text;
};

struct FScctHubHudChat
{
	int32 Serial = 0;
	EScctHubChatChannel Channel = EScctHubChatChannel::Everyone;
	int32 Team = -1;
	FString Name;
	FString Text;
	double At = 0.0;
};

struct FScctHubHudPlayer
{
	int32 PlayerId = 0;
	int32 Team = 0;
	int32 PingMs = 0;
	int32 Flags = 0;
	FString Name;
};

struct FScctHubHudDestination
{
	FString Id;
	FString Title;
	int32 BestMs = -1;
};

/** What the hub HUD's widget draws, filled by FScctHubHud each frame. */
struct FScctHubHudModel
{
	TArray<FScctHubHudTag> Tags;
	TArray<FScctHubHudSign> Signs;
	TArray<FScctHubHudChat> Chat;
	TArray<FScctHubHudPlayer> Players;
	TArray<FScctHubHudDestination> Destinations;
	FString Help;
	FString Prompt;
	FString EmoteMenu;
	FString Queue;
	FLinearColor QueueColour = FLinearColor::White; // the state colour of the pad queued on (Hub/ScctHubPads.h)
	FString Note;                                   // the note about the last pad match (going, back, why)
	FString Activity;                               // the course run's or the range round's clock (Hub/ScctHubActivities.h)
	FString Notices;                                // the activities' recent notices, one a line
	FString BoardTitle;                             // the nearest score board in reach that is not a world sign
	FString BoardLines;
	FString HubName;
	int32 Team = -1;

	/** Seconds on the HUD's own clock (the chat lines' ages). */
	double Clock = 0.0;

	/** Goes up when the set of players changed (the player list is laid out again). */
	int32 RosterRevision = 0;

	/** Goes up when the travel menu's districts changed. */
	int32 DestinationRevision = 0;

	// The panels.
	bool bComposing = false;
	EScctHubChatChannel Channel = EScctHubChatChannel::Everyone;
	bool bEmoteMenu = false;
	bool bPlayerList = false;
	bool bTravel = false;

	/** Shown in the travel menu: travelling cancels an activity or a pad's queue. */
	bool bBusy = false;

	/** Name tags and signs drawn on the last paint (for the log line). */
	mutable int32 TagsDrawn = 0;
	mutable int32 SignsDrawn = 0;
};

/**
 * The hub's own HUD (an OpenSCCT addition; port of the Godot client's game/Scripts/Hub/HubHud.cs, HubHud.Travel.cs,
 * HubHud.Bindings.cs, HubPlayerList.cs and HubSignsView.cs). It belongs to the hub subsystem and runs only while the
 * hub level presents the visit; its Slate overlay (SScctHubHud) sits over the match HUD and under the in-match menu.
 *
 * What it shows, all read from the bridge's hub social table (Runtime/ScctHubSocialApi.h):
 *   - every other player's name over his head in his side's colour (cyan spies, amber mercs), smaller with distance,
 *     with the emote he plays and a bubble of what he said in the last six seconds;
 *   - the chat panel (bottom left): the hub's lines and the players' on the everyone, side and nearby channels; the
 *     chat key (Talk, Enter or F5) opens the composer on everyone, the team chat key (TeamTalk, F6) on the side; Tab
 *     switches between everyone, side and nearby; Enter sends, Escape cancels; "/wave" plays an emote, "/l" speaks to
 *     the players near;
 *   - the hub's keys (top left, from the player's own bindings), what U does where he stands (bottom middle), his
 *     place in a pad's queue;
 *   - the world signs (pad signs, leaderboards) as text projected where they stand, hidden behind walls, faded out
 *     from 60 to 70 metres;
 *   - P: the player list with each player's ping, a voice volume (0 to 200 %) and a voice mute (kept in the hub
 *     subsystem for the voice chat to use) and a chat mute (the visit's, kept by the bridge);
 *   - G then 1 to 8: the emote menu; F4: the travel menu of the hub's districts (also opened by the hub when the
 *     player walks into a travel point).
 * J and K ask for the spy's or the merc's side, N for the plaza, U uses what is near, X stops an activity, H watches
 * the queued pad's match. The keys are the bindings' hub commands (Bridge/.../Hub/HubBindings.cs), which arrive on
 * AScctPlayerController::OnLocalCommand like every interface command; this HUD subscribes there.
 *
 * The pad panel (the queue line on glass with its pad's state colour, the note about the last match) reads the match
 * pads' view (Hub/ScctHubPads.h). The activities' clock (top middle), their notices under it and the nearest score
 * board (a panel on the right) read the hub's activities (Hub/ScctHubActivities.h). Not ported here: rank emblems on
 * the name tags, the voice overlay (no voice transport yet). O opens the Armoury over the hub through the in-match
 * menu (Menu/SScctArmouryOverlay.h); closing it is back in the hub, and a look picked there goes to the hub at once.
 *
 * Unattended switches (for runs with nobody at the keyboard):
 *   -ScctHubOpen=<panel>[@seconds][,...]   open players, chat, travel, emotes or armoury that long after the player stands in
 *                                          the hub (2 s when no time is given)
 *   -ScctHubChat=<text>[@seconds]          say that line to everyone (4 s after standing in the hub by default)
 * Console (also from -ExecCmds, before the hub is up: it waits):
 *   ScctHubCommand [seconds] <command>     press the key the bindings put a hub command on (HubMerc, HubPlaza,
 *                                          HubPlayers...) that long after the player stands in the hub, or now; a
 *                                          command without a key is run directly
 */
class FScctHubHud : public TSharedFromThis<FScctHubHud>
{
public:
	explicit FScctHubHud(UScctHubSubsystem* InOwner);
	~FScctHubHud();

	/** One frame of the drawn hub (the presenting level's session frame): reads the table, keeps the overlay up. */
	void Tick(UWorld* World, float DeltaSeconds, bool bVerbose);

	/** The hub is no longer drawn: the overlay goes, the panels close and the keys are the game's again. */
	void Stop();

	bool IsRunning() const { return Widget.IsValid(); }

	/** Queues a hub command (ScctHubCommand) for DelaySeconds after the player stands in the hub (now when already there). */
	void QueueCommand(float DelaySeconds, const FString& Command);

	// ---- what the widget calls ---------------------------------------------------------------------------------

	void OpenComposer(EScctHubChatChannel Channel);
	void CloseComposer();
	void SendChat(const FString& Text);
	void CycleChannel();

	void TogglePlayerList();
	void ToggleEmoteMenu();
	void OpenTravel();
	void CloseTravel();
	void Travel(const FString& Id);

	void SetChatMuted(const FString& Name, bool bMuted);
	float GetVoiceVolume(const FString& Name) const;
	void SetVoiceVolume(const FString& Name, float Volume);
	bool IsVoiceMuted(const FString& Name) const;
	void SetVoiceMuted(const FString& Name, bool bMuted);

	/** World to widget units, at paint time (the camera as drawn this frame). False behind the camera. */
	bool Project(const FVector& World, float Scale, FVector2D& OutLocal) const;

private:
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);
	void RunCommand(const FString& Command);
	void Subscribe(AScctPlayerController* Player);
	void ShowWidget(UWorld* World);
	void ReadTable(const FVector& Eye);
	void ReadChat();
	void ReadPlayers();
	void ReadDestinations();
	FString ReadText(EScctHubSocialText Kind) const;
	void TakeKeys(bool bTake);
	void WatchCloseKeys();
	void RunUnattended(float DeltaSeconds);
	void LogLine(float DeltaSeconds);

	UScctHubSubsystem* Owner = nullptr;
	const FScctHubSocialApi* Api = nullptr;
	bool bReportedNoApi = false;
	TSharedRef<FScctHubHudModel> Model;
	TSharedPtr<SScctHubHud> Widget;
	TWeakObjectPtr<UWorld> WidgetWorld;
	TWeakObjectPtr<AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;

	FScctHubSocialState State = {};
	int32 ChatSerial = 0;
	int32 SignRevision = -1;
	int32 TravelSerial = 0;
	TArray<int32> RosterIds;
	float PlayersTimer = 0.0f;
	float TextTimer = 0.0f;

	/** The keys were taken from the game for a panel (the player list, travel, the composer). */
	bool bKeysTaken = false;
	TArray<FKey> CloseKeys;
	bool bCloseKeysArmed = false;

	/** Seconds the player has stood in the drawn hub (the unattended switches count from there). */
	float StandingSeconds = -1.0f;
	float LogTimer = 0.0f;

	struct FTimedCommand
	{
		float At = 0.0f;
		FString Command;
		bool bDone = false;
	};
	TArray<FTimedCommand> Timed;
	FString AutoChat;
	float AutoChatAt = 4.0f;
	bool bAutoChatDone = false;
};
