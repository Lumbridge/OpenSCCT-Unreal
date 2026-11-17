#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctChatTranscript.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctChatSubsystem.generated.h"

class AScctPlayerController;
class SScctChatPanel;
class SWidget;
struct FScctHudMessage;

/**
 * The chat of a match level (Godot's ChatInput with its ChatPanel and ChatTranscript): the composer the player types
 * a line in, and the transcript of what was said, drawn by SScctChatPanel over the HUD.
 *
 * OPENING. The player's bindings raise Talk (F5 and Enter by default) and TeamTalk (F6) as interface commands
 * (AScctPlayerController::OnLocalCommand): Talk opens the composer on ALL, TeamTalk on TEAM. While it is open the
 * player's keys and mouse are held back from the simulation (the controller's SetMenuOpen, which also lets go of
 * everything held) and the text box has the keyboard; the match menu cannot open meanwhile, since the controls raise
 * no commands. When it closes, the game gets the keys back once the key that closed it (Enter, Esc) is up again, so
 * that key is not read as a new press.
 *
 * SENDING. A line goes out on the session's own connection (Runtime/ScctHudChatApi.h), which works in a local match
 * (the in-process server) and in an online round alike; if that table is missing in a visit, the online subsystem's
 * SendChat is used. Nothing is added when a line is sent: the server relays it back to its sender as to anyone else.
 *
 * THE TRANSCRIPT (64 lines, ALL / TEAM / SYSTEM). In an online visit it is the visit's chat
 * (UScctOnlineSubsystem::GetChat: the lines of the lobby before the round included, with the server's notices as
 * SYSTEM), so it is the connection's history and not the level's. In a local match it is the chat lines the HUD
 * drains (Chat and TeamChat messages; a line from no player and no team is the server's, SYSTEM). The same line
 * arriving twice within a moment (an echo) is kept once. The HUD's log no longer repeats chat lines.
 *
 * OPTIONS (page Hud, group Chat): Hud.ChatFadeSeconds, how long a line stays in the passive view (8 s); and
 * Hud.ChatPassiveLines, how many lines the passive view shows (4; 0 hides it, the history is still there).
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard):
 *   -ScctChatOpen=all|team   open the composer -ScctChatAt seconds (default 8) after the chat came up in the match
 *   -ScctChatSay="text;..."  with -ScctChatOpen: type each line into the box and send it -ScctChatHold seconds later
 *                            (default 2), the next one 1.5 s after that; on the channel of -ScctChatOpen unless the
 *                            line starts with "team:" or "all:"
 *   -ScctChatHistory[=S]     open the composer and leave it open with its history, S seconds after the chat came up
 *                            (default: 2 s after the last -ScctChatSay line, else -ScctChatAt)
 *   -ScctChatFilter=all|team|system  the history's tab when -ScctChatHistory opens it
 *   -ScctChatScroll=N        scroll the history back N lines when -ScctChatHistory opens it
 * Every line sent, every line received and every open and close writes a "ScctChat:" line to the log.
 */
UCLASS()
class OPENSCCT_API UScctChatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UScctChatSubsystem();

	/** The composer is open (or still holds the keys until the closing key is up). */
	bool IsOpen() const;

	/** Opens the composer on ALL or TEAM; false when it cannot open now (a menu is open, no match). */
	bool Open(bool bTeam);
	void Close();

	/** Sends a line as the composer does. False when there was nothing to send it on. */
	bool Send(const FString& Text, bool bTeam);

	const FScctChatTranscript& GetTranscript() const { return *Transcript; }

	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static void RegisterSettings();

	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);
	void HandleHudMessage(const FScctHudMessage& Message);
	void HandleSubmit(const FString& Text, bool bTeam);
	void HandlePanelClosed();

	/** Adds a received line and logs it. */
	void Receive(EScctChatChannel Channel, const FString& Sender, int32 SenderTeam, const FString& Text, double Now);

	/** The online visit's chat lines that are new since the last look. */
	void PullOnlineChat(bool bSeed);

	/** In a level that plays a match, with a local player: the panel is put in the viewport. */
	bool EnsurePanel();
	void RemovePanel();

	/** Gives the keys back to the game after the composer closed. */
	void GiveInputToGame();

	bool IsVisiting() const;
	void RunUnattended(float DeltaTime);

	TSharedRef<FScctChatTranscript> Transcript;

	/** Seconds of the level's frames since the chat started: what lines are stamped with and fade by. */
	double Clock = 0.0;
	TSharedPtr<SScctChatPanel> Panel;
	TSharedPtr<SWidget> ViewportContent;

	TWeakObjectPtr<AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;
	FDelegateHandle HudMessageHandle;

	/** The player the composer took the keys from, while it has them. */
	TWeakObjectPtr<AScctPlayerController> InputHolder;

	/** When the composer last closed: a Talk press right after it (the key that sent the line) opens nothing. */
	double ClosedAt = 0.0;

	// The online transcript as last seen: how many lines it had, and the last one.
	int32 OnlineSeenCount = 0;
	bool bOnlineSeeded = false;
	bool bOnlineHasLast = false;
	bool bOnlineLastSystem = false;
	bool bOnlineLastTeam = false;
	FString OnlineLastSender, OnlineLastText;

	// Unattended switches: timed steps from the chat's start in the match.
	struct FStep
	{
		float Time = 0.0f;
		enum class EKind : uint8 { Open, Type, Submit, History } Kind = EKind::Open;
		bool bTeam = false;
		FString Text;
		bool bDone = false;
	};
	TArray<FStep> Steps;
	int32 HistoryFilter = 0;
	int32 HistoryScroll = 0;
	float ChatClock = 0.0f;
};
