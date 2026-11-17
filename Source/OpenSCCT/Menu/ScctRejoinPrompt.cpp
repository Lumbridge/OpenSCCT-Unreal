#include "Menu/ScctRejoinPrompt.h"

#include "Menu/ScctMenuKit.h"
#include "Menu/ScctMenuSubsystem.h"
#include "OpenSCCT.h"
#include "Online/ScctRejoinSubsystem.h"

namespace
{
	FString ScctRejoinMapTitle(const FString& Map)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		const FString Title = Menu != nullptr && !Map.IsEmpty() ? Menu->GetMapTitle(Map) : FString();
		return Title.IsEmpty() ? Map : Title;
	}
}

FString ScctRejoinPrompt::Describe(const UScctRejoinSubsystem* Rejoin)
{
	FScctRejoinChoice Choice;
	if (Rejoin == nullptr || !Rejoin->GetOffer(Choice))
	{
		return FString();
	}
	const FString Map = ScctRejoinMapTitle(Choice.Map);
	return Map.IsEmpty() ? FString(Choice.ServerName) : FString::Printf(TEXT("%s on %s"), *Map, Choice.ServerName);
}

void ScctRejoinPrompt::Show(const TSharedPtr<SScctDialogLayer>& Dialogs, UScctRejoinSubsystem* Rejoin)
{
	FScctRejoinChoice Choice;
	if (!Dialogs.IsValid() || Rejoin == nullptr || !Rejoin->GetOffer(Choice))
	{
		return;
	}

	const TWeakObjectPtr<UScctRejoinSubsystem> Weak = Rejoin;
	const FString Map = ScctRejoinMapTitle(Choice.Map);
	FScctDialog Dialog;
	Dialog.Title = TEXT("Rejoin the round");
	Dialog.Body = Choice.FromAccount != 0
		? FString::Printf(TEXT("The account dropped out of a round of %s on %s, and the server is still playing it. Rejoin it on the same side, with the score and lives kept?"), *Map, Choice.ServerName)
		: FString::Printf(TEXT("A round of %s on %s ended for this game before it was over (a crash, a lost connection or a quit), and the server is still playing it. Rejoin it on the same side, with the score and lives kept?"), *Map, Choice.ServerName);
	Dialog.Buttons.Add({ TEXT("FORGET IT"), EScctButtonLook::Secondary, [Weak](const FString&) { if (Weak.IsValid()) { Weak->Dismiss(); } } });
	Dialog.Buttons.Add({ TEXT("LATER"), EScctButtonLook::Secondary, nullptr });
	Dialog.Buttons.Add({ TEXT("REJOIN"), EScctButtonLook::Primary, [Weak](const FString&) { if (Weak.IsValid()) { Weak->Rejoin(); } } });
	Dialog.FocusButton = 2;
	// Escape only closes the question: the home page keeps its REJOIN entry.
	Dialog.CancelButton = 1;
	UE_LOG(LogOpenSCCT, Log, TEXT("Rejoin: the main menu asks whether to rejoin %s on '%s'"), *Map, Choice.ServerName);
	Dialogs->Show(Dialog);
}
