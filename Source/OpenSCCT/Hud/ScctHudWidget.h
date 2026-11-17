#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScctHudWidget.generated.h"

class FScctHudModel;
class SEditableText;

/** A chat line the player typed and sent with Enter: its text and whether it is for the team only. */
DECLARE_DELEGATE_TwoParams(FScctChatSubmitted, const FString& /*Text*/, bool /*bTeamOnly*/);

/**
 * The HUD's root widget in the viewport. It is built in code (no Blueprint asset): two painted layers, one over
 * the whole picture and one inside the display's safe zone, and the chat line. The viewport's DPI scaling applies
 * to all of it, so the HUD is the same size on screen at 1080p and 4K.
 *
 * The chat line (the original's Talk and TeamTalk) is the one part that takes the keyboard: a text field above the
 * log, shown only while the player types. Enter sends the line, Escape drops it, Tab switches between everyone and
 * the team. The HUD subsystem opens it and holds the player's keys back meanwhile.
 */
UCLASS()
class OPENSCCT_API UScctHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** What the layers draw from. Set before the widget is added to the viewport. */
	void SetModel(const TSharedPtr<const FScctHudModel>& InModel) { Model = InModel; }

	/** Shows the chat line, empty, and gives it the keyboard. bTeamOnly: the line goes to the player's team. */
	void OpenChat(bool bTeamOnly);

	/** Takes the chat line away without sending. OnChatClosed is raised either way the line closes. */
	void CloseChat();

	bool IsChatOpen() const { return bChatOpen; }
	bool IsChatForTeam() const { return bChatTeam; }

	/** Unattended runs: puts text in the open line as if typed, and sends it as Enter does. */
	void SetChatText(const FString& Text);
	void SubmitChat();

	FScctChatSubmitted OnChatSubmitted;
	FSimpleDelegate OnChatClosed;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<const FScctHudModel> Model;
	TSharedPtr<SEditableText> ChatText;
	bool bChatOpen = false;
	bool bChatTeam = false;
};
