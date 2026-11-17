#include "Menu/ScctMenuDriver.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Menu/ScctMenuKit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "UnrealClient.h"

namespace
{
	struct FScript
	{
		TArray<FString> Commands;
		int32 Next = 0;
		float Wait = 0.0f;
		bool bRead = false;
		TWeakPtr<SWidget> Hovered;
		IScctDriven* HoveredDriven = nullptr;
	};

	FScript& Script()
	{
		static FScript Made;
		if (!Made.bRead)
		{
			Made.bRead = true;
			FString Text;
			if (FParse::Value(FCommandLine::Get(), TEXT("ScctMenuDo="), Text, false))
			{
				Text.ParseIntoArray(Made.Commands, TEXT(";"));
				for (FString& Command : Made.Commands)
				{
					Command.TrimStartAndEndInline();
				}
			}
		}
		return Made;
	}

	void PressKey(const FKey& Key)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		Slate.ProcessKeyDownEvent(Event);
		Slate.ProcessKeyUpEvent(Event);
	}
}

bool FScctMenuDriver::IsScripted()
{
	return Script().Commands.Num() > 0;
}

void FScctMenuDriver::Tick(float DeltaSeconds, const FHost& Host)
{
	FScript& Run = Script();
	if (Run.Next >= Run.Commands.Num())
	{
		return;
	}
	if (Run.Wait > 0.0f)
	{
		Run.Wait -= DeltaSeconds;
		return;
	}

	const FString Command = Run.Commands[Run.Next++];
	FString Verb, Rest;
	if (!Command.Split(TEXT(" "), &Verb, &Rest))
	{
		Verb = Command;
	}
	Rest.TrimStartAndEndInline();
	// A frame between commands lets a page that was just built lay itself out before it is used.
	Run.Wait = 0.05f;

	if (Verb.Equals(TEXT("wait"), ESearchCase::IgnoreCase))
	{
		Run.Wait = FCString::Atof(*Rest);
	}
	else if (Verb.Equals(TEXT("page"), ESearchCase::IgnoreCase))
	{
		if (Host.ShowPage)
		{
			Host.ShowPage(Rest);
		}
		Run.Wait = 0.3f;
	}
	else if (Verb.Equals(TEXT("key"), ESearchCase::IgnoreCase))
	{
		FString Name = Rest, Count;
		Rest.Split(TEXT(" "), &Name, &Count);
		const FKey Key(*Name);
		if (!Key.IsValid())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Menu script: '%s' is not a key"), *Name);
		}
		for (int32 Index = 0; Key.IsValid() && Index < FMath::Max(FCString::Atoi(*Count), 1); ++Index)
		{
			PressKey(Key);
		}
		Run.Wait = 0.12f;
	}
	else if (Verb.Equals(TEXT("press"), ESearchCase::IgnoreCase) || Verb.Equals(TEXT("focus"), ESearchCase::IgnoreCase) || Verb.Equals(TEXT("hover"), ESearchCase::IgnoreCase))
	{
		if (Verb.Equals(TEXT("hover"), ESearchCase::IgnoreCase) && Run.Hovered.IsValid() && Run.HoveredDriven != nullptr)
		{
			Run.HoveredDriven->SetDrivenHover(false);
		}
		Run.Hovered.Reset();
		Run.HoveredDriven = nullptr;

		IScctDriven* Found = Rest.IsEmpty() ? nullptr : IScctDriven::Find(Rest);
		if (Found == nullptr)
		{
			if (!Rest.IsEmpty())
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Menu script: nothing on screen is called '%s'"), *Rest);
			}
		}
		else if (Verb.Equals(TEXT("press"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu script: press '%s'"), *Found->GetDrivenLabel());
			Found->DrivenPress();
			Run.Wait = 0.3f;
		}
		else if (Verb.Equals(TEXT("focus"), ESearchCase::IgnoreCase))
		{
			FSlateApplication::Get().SetKeyboardFocus(Found->GetDrivenWidget(), EFocusCause::Navigation);
		}
		else
		{
			Found->SetDrivenHover(true);
			Run.Hovered = Found->GetDrivenWidget();
			Run.HoveredDriven = Found;
		}
	}
	else if (Verb.Equals(TEXT("type"), ESearchCase::IgnoreCase))
	{
		for (const TCHAR Character : Rest)
		{
			FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0, false));
		}
	}
	else if (Verb.Equals(TEXT("shot"), ESearchCase::IgnoreCase))
	{
		FString Directory = FPaths::ProjectSavedDir() / TEXT("ScctShots");
		FParse::Value(FCommandLine::Get(), TEXT("ScctShotDir="), Directory);
		const FString File = Directory / (Rest + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu script: screenshot %s"), *File);
		Run.Wait = 0.5f;
	}
	else if (Verb.Equals(TEXT("log"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu script: %s"), *Rest);
	}
	else if (Verb.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu script: done"));
		FPlatformMisc::RequestExit(false);
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu script: unknown command '%s'"), *Command);
	}
}
