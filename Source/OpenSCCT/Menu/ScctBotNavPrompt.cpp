#include "Menu/ScctBotNavPrompt.h"

#include "Containers/Ticker.h"
#include "Menu/ScctMenuKit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctContentDownloads.h"
#include "OpenSCCT.h"
#include "Runtime/ScctContentApi.h"

namespace
{
	enum class EBotNavPhase : uint8 { Checking, Asking, Downloading, Failed, Finished };

	struct FBotNavPrompt
	{
		TWeakPtr<SScctDialogLayer> Dialogs;
		FString Map;
		TFunction<bool()> StillValid;
		TFunction<void()> Launch;
		EBotNavPhase Phase = EBotNavPhase::Checking;
		double CheckUntil = 0.0;
		double AnswerAt = -1.0;
		bool bDialogUp = false;
		FString Question;
		FString LastLogged;
		FTSTicker::FDelegateHandle Ticker;
	};

	FString BotNavAnswerSwitch()
	{
		FString Answer;
		FParse::Value(FCommandLine::Get(), TEXT("ScctBotNavAnswer="), Answer);
		return Answer.ToLower();
	}

	FString BotNavKey(const FString& Map)
	{
		return TEXT("nav:") + Map;
	}

	void BotNavFinish(const TSharedRef<FBotNavPrompt>& Prompt, bool bLaunch)
	{
		if (Prompt->Phase == EBotNavPhase::Finished)
		{
			return;
		}
		const EBotNavPhase Was = Prompt->Phase;
		Prompt->Phase = EBotNavPhase::Finished;
		if (Was == EBotNavPhase::Downloading)
		{
			ScctContentDownloads::CancelDownload(BotNavKey(Prompt->Map));
		}
		// The dialog this prompt put up is closed (its buttons do nothing now that the prompt is finished).
		const TSharedPtr<SScctDialogLayer> Dialogs = Prompt->Dialogs.Pin();
		if (Prompt->bDialogUp && Dialogs.IsValid() && Dialogs->IsOpen())
		{
			Dialogs->Answer(-1);
		}
		Prompt->bDialogUp = false;
		// The ticker sees the prompt finished on its next call and goes (it is never removed from inside its own call).
		const bool bGo = bLaunch && (!Prompt->StillValid || Prompt->StillValid());
		UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: %s"), bGo ? TEXT("starting the match") : TEXT("the start was dropped"));
		if (bGo && Prompt->Launch)
		{
			Prompt->Launch();
		}
	}

	void BotNavStartDownload(const TSharedRef<FBotNavPrompt>& Prompt);

	void BotNavShow(const TSharedRef<FBotNavPrompt>& Prompt, FScctDialog Dialog)
	{
		const TSharedPtr<SScctDialogLayer> Dialogs = Prompt->Dialogs.Pin();
		if (!Dialogs.IsValid())
		{
			BotNavFinish(Prompt, true);
			return;
		}
		// Every button first says the dialog is gone; a finished prompt's buttons do nothing more.
		for (FScctDialogButton& Button : Dialog.Buttons)
		{
			TFunction<void(const FString&)> Then = MoveTemp(Button.OnPressed);
			Button.OnPressed = [Prompt, Then](const FString& Typed)
			{
				Prompt->bDialogUp = false;
				if (Prompt->Phase != EBotNavPhase::Finished && Then)
				{
					Then(Typed);
				}
			};
		}
		Prompt->bDialogUp = true;
		Dialogs->Show(Dialog);
		const FString Answer = BotNavAnswerSwitch();
		Prompt->AnswerAt = Answer.IsEmpty() ? -1.0 : FPlatformTime::Seconds() + 2.0;
	}

	/** The question (Godot's "Download and play", "Build locally", "Cancel"), or the failure with "Try again". */
	void BotNavAsk(const TSharedRef<FBotNavPrompt>& Prompt, const FString& Body, bool bFailed)
	{
		Prompt->Phase = bFailed ? EBotNavPhase::Failed : EBotNavPhase::Asking;
		UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: asks \"%s\""), *Body.Replace(TEXT("\n"), TEXT(" ")));
		FScctDialog Dialog;
		Dialog.Title = TEXT("Bot navigation");
		Dialog.Body = Body;
		Dialog.Buttons.Add({ bFailed ? TEXT("Try again") : TEXT("Download and play"), EScctButtonLook::Primary, [Prompt](const FString&) { BotNavStartDownload(Prompt); } });
		Dialog.Buttons.Add({ TEXT("Build locally"), EScctButtonLook::Secondary, [Prompt](const FString&)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: built locally (the bots prepare their routes on this PC)"));
			BotNavFinish(Prompt, true);
		} });
		Dialog.Buttons.Add({ TEXT("Cancel"), EScctButtonLook::Secondary, [Prompt](const FString&) { BotNavFinish(Prompt, false); } });
		Dialog.FocusButton = 0;
		Dialog.CancelButton = 2;
		BotNavShow(Prompt, Dialog);
	}

	void BotNavStartDownload(const TSharedRef<FBotNavPrompt>& Prompt)
	{
		if (!ScctContentDownloads::StartBotNavigation(Prompt->Map))
		{
			BotNavAsk(Prompt, TEXT("The bot navigation download failed. OpenSCCT-Content no longer has it.\nTry again or build it locally."), true);
			return;
		}
		Prompt->Phase = EBotNavPhase::Downloading;
		UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: downloading the prebuilt routes of %s"), *Prompt->Map);
		FScctDialog Dialog;
		Dialog.Title = TEXT("Bot navigation");
		const FString Key = BotNavKey(Prompt->Map);
		Dialog.LiveBody = [Key]
		{
			FString Line;
			float Fraction = 0.0f;
			ScctContentDownloads::GetDownload(Key, Line, Fraction);
			return Line.IsEmpty() ? FString(TEXT("Downloading bot navigation")) : Line;
		};
		Dialog.Buttons.Add({ TEXT("Cancel"), EScctButtonLook::Secondary, [Prompt](const FString&) { BotNavFinish(Prompt, false); } });
		Dialog.CancelButton = 0;
		BotNavShow(Prompt, Dialog);
		// Downloading is not answered unattended: it finishes by itself.
		Prompt->AnswerAt = -1.0;
	}

	bool BotNavTick(TWeakPtr<FBotNavPrompt> Weak)
	{
		const TSharedPtr<FBotNavPrompt> Pinned = Weak.Pin();
		if (!Pinned.IsValid() || Pinned->Phase == EBotNavPhase::Finished)
		{
			return false;
		}
		const TSharedRef<FBotNavPrompt> Prompt = Pinned.ToSharedRef();
		if (Prompt->StillValid && !Prompt->StillValid())
		{
			BotNavFinish(Prompt, false);
			return false;
		}

		switch (Prompt->Phase)
		{
		case EBotNavPhase::Checking:
		{
			FString Text;
			const int32 State = ScctContentDownloads::CheckBotNavigation(Prompt->Map, Text);
			if (State == static_cast<int32>(EScctBotNavigationState::Offer))
			{
				BotNavAsk(Prompt, Text, false);
			}
			else if (State == static_cast<int32>(EScctBotNavigationState::None) || FPlatformTime::Seconds() >= Prompt->CheckUntil)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: %s"), Text.IsEmpty() ? TEXT("the content index did not answer in time") : *Text);
				BotNavFinish(Prompt, true);
				return false;
			}
			break;
		}
		case EBotNavPhase::Downloading:
		{
			FString Line;
			float Fraction = 0.0f;
			const int32 State = ScctContentDownloads::GetDownload(BotNavKey(Prompt->Map), Line, Fraction);
			// Logged when the step changes and every quarter of the download.
			const int32 Colon = Line.Find(TEXT(":"));
			const FString Logged = FString::Printf(TEXT("%s %d"), *(Colon < 0 ? Line : Line.Left(Colon)), FMath::FloorToInt32(Fraction * 4.0f));
			if (Logged != Prompt->LastLogged)
			{
				Prompt->LastLogged = Logged;
				UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: %s"), *Line);
			}
			if (State == static_cast<int32>(EScctContentDownloadState::Done))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: fetched the prebuilt graph of %s: %s"), *Prompt->Map, *Line);
				BotNavFinish(Prompt, true);
				return false;
			}
			if (State == static_cast<int32>(EScctContentDownloadState::Failed) || State == static_cast<int32>(EScctContentDownloadState::Stopped))
			{
				const int32 ErrorAt = Line.Find(TEXT(": "));
				BotNavAsk(Prompt, FString::Printf(TEXT("The bot navigation download failed. %s\nTry again or build it locally."), ErrorAt >= 0 ? *Line.Mid(ErrorAt + 2) : *Line), true);
			}
			break;
		}
		default:
			break;
		}

		// -ScctBotNavAnswer presses a button of the question.
		if (Prompt->AnswerAt >= 0.0 && FPlatformTime::Seconds() >= Prompt->AnswerAt && (Prompt->Phase == EBotNavPhase::Asking || Prompt->Phase == EBotNavPhase::Failed))
		{
			Prompt->AnswerAt = -1.0;
			const FString Answer = BotNavAnswerSwitch();
			const int32 Index = Answer == TEXT("download") ? 0 : Answer == TEXT("local") ? 1 : 2;
			UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation: -ScctBotNavAnswer presses %s"), Index == 0 ? TEXT("Download and play") : Index == 1 ? TEXT("Build locally") : TEXT("Cancel"));
			if (const TSharedPtr<SScctDialogLayer> Dialogs = Prompt->Dialogs.Pin())
			{
				Dialogs->Answer(Index);
			}
		}
		return Prompt->Phase != EBotNavPhase::Finished;
	}
}

void ScctBotNavPrompt::OfferThenLaunch(const TSharedPtr<SScctDialogLayer>& Dialogs, const FString& Map, TFunction<bool()> StillValid, TFunction<void()> Launch)
{
	FString Text;
	const int32 State = ScctContentDownloads::CheckBotNavigation(Map, Text);
	if (State == static_cast<int32>(EScctBotNavigationState::None) || !Dialogs.IsValid())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation for %s: nothing offered (%s)"), *Map, Text.IsEmpty() ? TEXT("no content table") : *Text);
		if (Launch)
		{
			Launch();
		}
		return;
	}

	const TSharedRef<FBotNavPrompt> Prompt = MakeShared<FBotNavPrompt>();
	Prompt->Dialogs = Dialogs;
	Prompt->Map = Map;
	Prompt->StillValid = MoveTemp(StillValid);
	Prompt->Launch = MoveTemp(Launch);
	Prompt->CheckUntil = FPlatformTime::Seconds() + 3.0;
	if (State == static_cast<int32>(EScctBotNavigationState::Offer))
	{
		BotNavAsk(Prompt, Text, false);
	}
	else
	{
		// The index is still being read: the question waits up to three seconds for it, as in the Godot client.
		UE_LOG(LogOpenSCCT, Log, TEXT("Bot navigation for %s: checking the content index"), *Map);
		FScctDialog Dialog;
		Dialog.Title = TEXT("Bot navigation");
		Dialog.Body = TEXT("Checking for prebuilt bot navigation...");
		Dialog.Buttons.Add({ TEXT("Build locally"), EScctButtonLook::Secondary, [Prompt](const FString&) { BotNavFinish(Prompt, true); } });
		Dialog.Buttons.Add({ TEXT("Cancel"), EScctButtonLook::Secondary, [Prompt](const FString&) { BotNavFinish(Prompt, false); } });
		Dialog.CancelButton = 1;
		BotNavShow(Prompt, Dialog);
		Prompt->AnswerAt = -1.0;
	}
	// The ticker holds the prompt: it lives until it finishes.
	Prompt->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Prompt](float) { return BotNavTick(Prompt); }), 0.1f);
}
