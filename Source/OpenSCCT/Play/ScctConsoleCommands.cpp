#include "Play/ScctConsoleCommands.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Menu/ScctEnhancedImport.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettingsConsole.h"

namespace
{
	/** A line for whoever typed the command: the log always, the console too when it is not the log itself. */
	void ScctConsoleSay(FOutputDevice& Ar, const FString& Line)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Console: %s"), *Line);
		if (&Ar != static_cast<FOutputDevice*>(GLog))
		{
			Ar.Log(Line);
		}
	}

	void ScctConsoleGet(const TArray<FString>& Args, FOutputDevice& Ar)
	{
		if (Args.Num() != 1)
		{
			ScctConsoleSay(Ar, TEXT("usage: Get <key>   (Settings lists the keys)"));
			return;
		}
		FString Value, Error;
		if (FScctSettingsConsole::Get(Args[0], Value, Error))
		{
			ScctConsoleSay(Ar, FString::Printf(TEXT("%s = %s"), *FScctSettingsConsole::CanonicalKey(Args[0]), *Value));
		}
		else
		{
			ScctConsoleSay(Ar, Error);
		}
	}

	void ScctConsoleSet(const TArray<FString>& Args, FOutputDevice& Ar)
	{
		if (Args.Num() < 2)
		{
			ScctConsoleSay(Ar, TEXT("usage: Set <key> <value>   (Settings lists the keys and what each takes; \"default\" resets a registry option)"));
			return;
		}
		// The value is the rest of the line: a resolution or a choice's label may have spaces.
		TArray<FString> Rest(Args);
		Rest.RemoveAt(0);
		const FString Wanted = FString::Join(Rest, TEXT(" "));
		FString Before, Now, Error;
		FScctSettingsConsole::Get(Args[0], Before, Error);
		if (!FScctSettingsConsole::Set(Args[0], Wanted, Now, Error))
		{
			ScctConsoleSay(Ar, FString::Printf(TEXT("%s not set: %s"), *Args[0], *Error));
			return;
		}
		FScctSettingsConsole::Save();
		ScctConsoleSay(Ar, FString::Printf(TEXT("%s = %s (was %s)"), *FScctSettingsConsole::CanonicalKey(Args[0]), *Now, *Before));
	}

	void ScctConsoleSettings(const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
	{
		if (Args.Num() == 0)
		{
			TArray<FString> Pages;
			for (const TPair<FString, int32>& Page : FScctSettingsConsole::Pages())
			{
				Pages.Add(FString::Printf(TEXT("%s (%d)"), *Page.Key, Page.Value));
			}
			ScctConsoleSay(Ar, TEXT("Settings pages: ") + FString::Join(Pages, TEXT(", ")));
			ScctConsoleSay(Ar, TEXT("Settings <page> lists a page's keys; Get <key> and Set <key> <value> read and change one."));
			return;
		}
		const TArray<FString> Lines = FScctSettingsConsole::Describe(Args[0]);
		if (Lines.IsEmpty())
		{
			ScctConsoleSay(Ar, FString::Printf(TEXT("no settings page called '%s' (Settings lists the pages)"), *Args[0]));
			return;
		}
		ScctConsoleSay(Ar, FString::Printf(TEXT("Settings %s: %d keys"), *Args[0], Lines.Num()));
		for (const FString& Line : Lines)
		{
			ScctConsoleSay(Ar, TEXT("  ") + Line);
		}
	}

	UScctOnlineSubsystem* ScctConsoleOnline(UWorld* World)
	{
		if (World == nullptr && GEngine != nullptr)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
				{
					World = Context.World();
					break;
				}
			}
		}
		return World != nullptr ? UScctOnlineSubsystem::Get(World) : nullptr;
	}

	void ScctConsoleAdminLogin(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UScctOnlineSubsystem* Online = ScctConsoleOnline(World);
		if (Args.Num() == 0)
		{
			ScctConsoleSay(Ar, TEXT("usage: AdminLogin <password>"));
			return;
		}
		if (Online == nullptr || Online->GetState() == EScctOnlineState::Idle)
		{
			ScctConsoleSay(Ar, TEXT("AdminLogin: not on a server"));
			return;
		}
		if (Online->IsHosting())
		{
			ScctConsoleSay(Ar, TEXT("AdminLogin: you host this server; Admin <command> runs its console without a login"));
			return;
		}
		// The password is never written to the log; the server's answer arrives as a system line in the chat.
		Online->AdminLogin(FString::Join(Args, TEXT(" ")));
		ScctConsoleSay(Ar, TEXT("AdminLogin: sent; the server answers in the chat"));
	}

	void ScctConsoleAdmin(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UScctOnlineSubsystem* Online = ScctConsoleOnline(World);
		if (Args.Num() == 0)
		{
			ScctConsoleSay(Ar, TEXT("usage: Admin <command>   (AdminLogin <password> first on a dedicated server)"));
			return;
		}
		const FString Command = FString::Join(Args, TEXT(" "));
		if (Online == nullptr || !Online->AdminCommand(Command))
		{
			ScctConsoleSay(Ar, TEXT("Admin: not on a server"));
			return;
		}
		ScctConsoleSay(Ar, Online->IsHosting() ? FString::Printf(TEXT("Admin: '%s' run by your server's console"), *Command)
			: Online->IsLocalAdmin() ? FString::Printf(TEXT("Admin: '%s' sent"), *Command)
			: FString::Printf(TEXT("Admin: '%s' sent (not logged in as an admin: the server will say why it refuses)"), *Command));
	}

	void ScctConsoleImportEnhanced(const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
	{
		const FString Folder = FString::Join(Args, TEXT(" ")).TrimQuotes();
		const bool bProfile = Folder.IsEmpty() || Folder.Equals(TEXT("profile"), ESearchCase::IgnoreCase);
		const FScctEnhancedImportResult Result = ScctEnhancedImport::Import(bProfile ? FString() : Folder, bProfile);
		TArray<FString> Lines;
		ScctEnhancedImport::Summary(Result).ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			ScctConsoleSay(Ar, Line);
		}
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScctGetConsoleCommand(
		TEXT("Scct.Get"), TEXT("Get <key> - a setting's value (Settings lists the keys). Plain Get works too."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar) { ScctConsoleGet(Args, Ar); }));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScctSetConsoleCommand(
		TEXT("Scct.Set"), TEXT("Set <key> <value> - sets a setting, puts it in force and saves it. Plain Set works too."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar) { ScctConsoleSet(Args, Ar); }));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScctSettingsConsoleCommand(
		TEXT("Settings"), TEXT("Settings [page] - the settings pages, or every key of a page with its value and what it takes."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ScctConsoleSettings));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScctAdminLoginConsoleCommand(
		TEXT("AdminLogin"), TEXT("AdminLogin <password> - log in as an admin of the dedicated server played on."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ScctConsoleAdminLogin));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScctAdminConsoleCommand(
		TEXT("Admin"), TEXT("Admin <command> - an admin command to the server played on (the console of your own server when hosting)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ScctConsoleAdmin));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScctImportEnhancedConsoleCommand(
		TEXT("Scct.ImportEnhanced"), TEXT("Scct.ImportEnhanced [folder] - import an Enhanced SCCT Versus install's settings (no folder: this PC's profile)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ScctConsoleImportEnhanced));
}

bool ScctConsoleCommands::ExecSettingCommand(const TCHAR* Cmd, UWorld* World, FOutputDevice& Ar)
{
	const TCHAR* Rest = Cmd;
	const bool bGet = FParse::Command(&Rest, TEXT("Get"));
	if (!bGet && !FParse::Command(&Rest, TEXT("Set")))
	{
		return false;
	}
	TArray<FString> Args;
	FString(Rest).ParseIntoArrayWS(Args);
	// "GET <class> <property>" and "SET <class> <property> <value>" stay the engine's: a settings key has a dot and no
	// path, a class name has neither.
	if (Args.IsEmpty() || (!FScctSettingsConsole::Has(Args[0]) && (!Args[0].Contains(TEXT(".")) || Args[0].StartsWith(TEXT("/")))))
	{
		return false;
	}
	if (bGet)
	{
		ScctConsoleGet(Args, Ar);
	}
	else
	{
		ScctConsoleSet(Args, Ar);
	}
	return true;
}
