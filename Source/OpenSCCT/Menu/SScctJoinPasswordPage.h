#pragma once

#include "CoreMinimal.h"
#include "Social/ScctSocialSubsystem.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;

/**
 * The front end's password page for a join that came from outside the menus (a friend's Join on Discord, the
 * website's Join button) to a server that asks for a password: Discord and the website never carry the password, so
 * the player types it here (the Godot client's server list and Join with IP asking for it). JOIN goes on with the
 * join through UScctSocialSubsystem; CANCEL drops it.
 */
class SScctJoinPasswordPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctJoinPasswordPage) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctSocialSubsystem>, Social)
		SLATE_ARGUMENT(FScctSocialPasswordPrompt, Prompt)
		/** JOIN or CANCEL: the front end goes home (a visit that starts takes the screen over). */
		SLATE_EVENT(FSimpleDelegate, OnDone)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void Join();

	TWeakObjectPtr<UScctSocialSubsystem> Social;
	FScctSocialPasswordPrompt Prompt;
	FSimpleDelegate OnDone;
	TSharedPtr<SEditableTextBox> PasswordBox;
	int32 SideIndex = 0;
};
