#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctMatchChat.generated.h"

class AScctPlayerController;
class SScctMatchChat;

/**
 * The chat line over the HUD in an online round (the Godot client's ChatInput and ChatPanel): the recent chat and
 * system lines above a text line that sends with SendChat. ALL or TEAM, switched with Tab; Enter sends and closes,
 * Escape closes without sending. While it is open the keyboard is the line's, so the pawn stands (the controls see no
 * play input while a text field has the focus) and the keys typed reach nobody else.
 *
 * Closed, it shows only the server's own lines of the last seconds (a kick, an admin's answer): the players' lines
 * are in the HUD's message log already, which the round's chat relay fills.
 */
UCLASS()
class OPENSCCT_API UScctMatchChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Opens the line on the ALL or TEAM channel and gives it the keyboard. */
	void Open(bool bTeam);

	/** Closes the line (the draft is dropped) and gives the keyboard back to the game. */
	void Close();

	bool IsOpen() const;

	/** Puts a draft into the open line (unattended runs). */
	void SetDraft(const FString& Text);

	/** Enter: sends the draft and closes. False when nothing was sent (an empty line, a team line without a side). */
	bool Send();

	/** Once a frame, before Slate draws (from the subsystem's tick): the lines follow the visit's chat. */
	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SScctMatchChat> Chat;
};

/**
 * Puts the chat line over the HUD in a level that presents an online round, and opens it on the player's own
 * bindings: Talk (F5 and Enter by default) for everyone, TeamTalk (F6) for the player's side. They arrive as
 * interface commands (AScctPlayerController::OnLocalCommand). The line is open only while the round is played.
 *
 * Console: scct.OpenChat [team] opens it.
 * Unattended switches (for screenshots):
 *   -ScctOpenChat=S[,team]   open the line S seconds after the round started playing
 *   -ScctChatDraft=<text>    type this into the line when it opens
 *   -ScctChatSendAfter=S     press Enter S seconds after the line opened
 */
UCLASS()
class OPENSCCT_API UScctMatchChatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Opens the chat line (false when the level presents no online round being played). */
	bool OpenChat(bool bTeam);

	// UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);
	bool InRound() const;

	UPROPERTY()
	TObjectPtr<UScctMatchChatWidget> Widget;

	TWeakObjectPtr<AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;

	// Unattended.
	float OpenAfter = -1.0f;
	bool bOpenTeam = false;
	FString Draft;
	float SendAfter = -1.0f;
	float RoundSeconds = 0.0f;
	float OpenSeconds = -1.0f;
};
