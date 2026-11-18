#include "Play/ScctConsoleCommands.h"

#include "Containers/Ticker.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Runtime/ScctConsoleApi.h"
#include "Test/ScctGrabAreaSubsystem.h"
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

	/** The game world a command without one means. */
	UWorld* ScctConsoleGameWorld(UWorld* World)
	{
		if (World != nullptr && World->IsGameWorld())
		{
			return World;
		}
		if (GEngine != nullptr)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() != nullptr && Context.World()->IsGameWorld())
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	/** A server's answer, which arrives after its command returned: the log, and the console's own lines. */
	void ScctConsoleSayLater(const FString& Line)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Console: %s"), *Line);
		if (GEngine != nullptr && GEngine->GameViewport != nullptr && GEngine->GameViewport->ViewportConsole != nullptr)
		{
			GEngine->GameViewport->ViewportConsole->OutputText(Line);
		}
	}

	/** Prints the answers the servers sent since the last frame, from the first typed line on. */
	void ScctConsoleWatchAnswers()
	{
		static bool bWatching = false;
		if (bWatching)
		{
			return;
		}
		bWatching = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			if (IsEngineExitRequested())
			{
				return false;
			}
			const FScctConsoleApi* Api = ScctFeature<FScctConsoleApi>();
			TCHAR Buffer[1024];
			for (int32 Taken = 0; Api != nullptr && Taken < 32 && Api->TakeLine(Buffer, UE_ARRAY_COUNT(Buffer)) != 0; ++Taken)
			{
				Buffer[UE_ARRAY_COUNT(Buffer) - 1] = TEXT('\0');
				ScctConsoleSayLater(Buffer);
			}
			return true;
		}));
	}

	/** grabarea [on|off], in the Godot console's words (DevConsole's GrabArea). */
	void ScctConsoleGrabArea(const FString& Arg, UWorld* World, FOutputDevice& Ar)
	{
		int32 Mode = -1;
		if (Arg.Equals(TEXT("on"), ESearchCase::IgnoreCase))
		{
			Mode = 1;
		}
		else if (Arg.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Mode = 0;
		}
		else if (!Arg.IsEmpty())
		{
			ScctConsoleSay(Ar, TEXT("usage: grabarea [on|off]"));
			return;
		}
		UWorld* Game = ScctConsoleGameWorld(World);
		UScctGrabAreaSubsystem* GrabArea = Game != nullptr ? Game->GetSubsystem<UScctGrabAreaSubsystem>() : nullptr;
		if (GrabArea == nullptr)
		{
			ScctConsoleSay(Ar, TEXT("grabarea: not in a game"));
			return;
		}
		const FString Answer = GrabArea->Request(Mode);
		ScctConsoleSay(Ar, Answer == TEXT("grabarea: on") ? FString(TEXT("Grab area on."))
			: Answer == TEXT("grabarea: off") ? FString(TEXT("Grab area off."))
			: Answer.StartsWith(TEXT("grabarea works in practice")) ? FString(TEXT("grabarea works in practice / Visit Map games only."))
			: Answer);
	}

	/** Scct.After <seconds> <command>: the line is run as if typed, later. */
	void ScctConsoleAfter(const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
	{
		if (Args.Num() < 2 || !FCString::IsNumeric(*Args[0]))
		{
			ScctConsoleSay(Ar, TEXT("usage: Scct.After <seconds> <command>"));
			return;
		}
		TArray<FString> Rest(Args);
		Rest.RemoveAt(0);
		const FString Line = FString::Join(Rest, TEXT(" "));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Line](float)
		{
			UWorld* Game = ScctConsoleGameWorld(nullptr);
			ULocalPlayer* Player = Game != nullptr && GEngine != nullptr ? GEngine->GetFirstGamePlayer(Game) : nullptr;
			if (Player != nullptr && !IsEngineExitRequested())
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Console: > %s"), *Line);
				Player->Exec(Game, *Line, *GLog);
			}
			return false;
		}), FMath::Max(0.0f, FCString::Atof(*Args[0])));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScctAfterConsoleCommand(
		TEXT("Scct.After"), TEXT("Scct.After <seconds> <command> - run a console line later, as if typed then."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ScctConsoleAfter));

	/**
	 * The Godot console's words by name, for the console manager: its list is what the console's auto-complete offers,
	 * and a line that does not come through the local player still finds its command. The help of each is the Godot
	 * console's own (help <command> prints it from the bridge); a word typed that is not here still works.
	 */
	struct FScctConsoleWords
	{
		TArray<IConsoleObject*> Registered;

		FScctConsoleWords()
		{
			static const TCHAR* const Words[][2] =
			{
				{ TEXT("current"), TEXT("current - List all settings and their current values.") },
				{ TEXT("login"), TEXT("login <password> - Log in as the server's admin.") },
				{ TEXT("cmd"), TEXT("cmd <server command> - Run a host/admin command. Use cmd help for the server's list.") },
				{ TEXT("say"), TEXT("say <text> - Send a chat message to the server.") },
				{ TEXT("resign"), TEXT("resign - Give up the current round by spending your remaining lives.") },
				{ TEXT("pings"), TEXT("pings - Show players' ping, jitter and packet loss.") },
				{ TEXT("map"), TEXT("map <name> [mode] - Select a map and optional game mode; requires session-control permission.") },
				{ TEXT("rmap_next"), TEXT("rmap_next - Select the next map; requires session-control permission.") },
				{ TEXT("rmap_prev"), TEXT("rmap_prev - Select the previous map; requires session-control permission.") },
				{ TEXT("practice"), TEXT("practice - Show practice information. practice on|off changes the mode (host/admin).") },
				{ TEXT("heal"), TEXT("heal - Restore your health and cure poison in practice.") },
				{ TEXT("regen"), TEXT("regen - Toggle your health regeneration in practice.") },
				{ TEXT("tank"), TEXT("tank - Toggle immunity to damage in practice (suicide still works).") },
				{ TEXT("kill"), TEXT("kill - Kill your current pawn in practice.") },
				{ TEXT("ammo"), TEXT("ammo - Refill your ammunition in practice.") },
				{ TEXT("grabarea"), TEXT("grabarea [on|off] - Practice only: draw each mercenary's grab window and the local player's grab eligibility.") },
				{ TEXT("infinite"), TEXT("infinite [on|off|toggle] - Show or change infinite ammo, gadget charges and mask air for the practice round.") },
				{ TEXT("resources"), TEXT("resources [on|off|toggle] - The same as infinite.") },
				{ TEXT("addbot"), TEXT("addbot - Add a practice bot (practice / Visit Map). help addbot shows the arguments.") },
				{ TEXT("kickbot"), TEXT("kickbot - Remove a practice bot. help kickbot shows the arguments.") },
				{ TEXT("placebot"), TEXT("placebot - Put a practice bot where you stand. help placebot shows the arguments.") },
				{ TEXT("botstop"), TEXT("botstop - Stop the practice bots.") },
				{ TEXT("botresume"), TEXT("botresume - Let the practice bots go on.") },
				{ TEXT("bothealth"), TEXT("bothealth - A practice bot's health. help bothealth shows the arguments.") },
				{ TEXT("botweapon"), TEXT("botweapon - A practice merc bot's weapon. help botweapon shows the arguments.") },
				{ TEXT("botteam"), TEXT("botteam - A practice bot's side. help botteam shows the arguments.") },
				{ TEXT("whereami"), TEXT("whereami [copy] - Show map, position and view details; copy also copies them to the clipboard.") },
			};
			IConsoleManager& Manager = IConsoleManager::Get();
			for (const auto& Word : Words)
			{
				// A name the engine already has (its own help) keeps its entry; typed, the word still comes here first.
				if (Manager.FindConsoleObject(Word[0]) != nullptr)
				{
					continue;
				}
				const FString Name = Word[0];
				Registered.Add(Manager.RegisterConsoleCommand(Word[0], Word[1],
					FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([Name](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
					{
						const FString Line = Args.IsEmpty() ? Name : Name + TEXT(" ") + FString::Join(Args, TEXT(" "));
						if (!ScctConsoleCommands::ExecGameCommand(*Line, World, Ar))
						{
							ScctConsoleSay(Ar, Name + TEXT(": the console's commands are not there (the bridge is not up; its reason is in the log)"));
						}
					}), ECVF_Default));
			}
		}

		~FScctConsoleWords()
		{
			for (IConsoleObject* Object : Registered)
			{
				IConsoleManager::Get().UnregisterConsoleObject(Object);
			}
		}
	} GScctConsoleWords;

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
	// path, a class name has neither. A line too short to be the engine's is the console's, answered as the Godot
	// console answers it.
	if (Args.IsEmpty() || (!FScctSettingsConsole::Has(Args[0]) && (!Args[0].Contains(TEXT(".")) || Args[0].StartsWith(TEXT("/")))))
	{
		if (Args.Num() > (bGet ? 1 : 2))
		{
			return false;
		}
		ScctConsoleSay(Ar, !bGet && Args.Num() < 2 ? FString(TEXT("usage: set <key> <value>"))
			: FString::Printf(TEXT("no setting '%s' (try: current)"), Args.IsEmpty() ? TEXT("") : *Args[0]));
		return true;
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

bool ScctConsoleCommands::ExecGameCommand(const TCHAR* Cmd, UWorld* World, FOutputDevice& Ar)
{
	const FString Line = FString(Cmd).TrimStartAndEnd();
	FString Verb = Line, Arg;
	int32 Space = INDEX_NONE;
	if (Line.FindChar(TEXT(' '), Space))
	{
		Verb = Line.Left(Space);
		Arg = Line.Mid(Space + 1).TrimStartAndEnd();
	}
	// Admin stays the word it was here first (an admin command, the Godot console's cmd), not that console's alias of
	// login; quit and exit are the engine's.
	if (Verb.IsEmpty() || Verb.Equals(TEXT("admin"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	// The settings words: the Unreal client's own settings, by its own keys.
	if (Verb.Equals(TEXT("current"), ESearchCase::IgnoreCase))
	{
		for (const FString& Setting : FScctSettingsConsole::Describe(FString()))
		{
			ScctConsoleSay(Ar, Setting);
		}
		return true;
	}
	if (FScctSettingsConsole::Has(Verb))
	{
		TArray<FString> Args;
		Args.Add(Verb);
		if (Arg.IsEmpty())
		{
			ScctConsoleGet(Args, Ar);
		}
		else
		{
			Args.Add(Arg);
			ScctConsoleSet(Args, Ar);
		}
		return true;
	}
	if (Verb.Equals(TEXT("grabarea"), ESearchCase::IgnoreCase))
	{
		ScctConsoleGrabArea(Arg, World, Ar);
		return true;
	}

	// Everything else is asked of the Godot console's own parser.
	const FScctConsoleApi* Api = ScctFeature<FScctConsoleApi>();
	if (Api == nullptr)
	{
		return false;
	}
	ScctConsoleWatchAnswers();
	int32 Result = EScctConsoleResult::Unknown;
	TArray<TCHAR> Buffer;
	Buffer.SetNumZeroed(4096);
	const int32 Length = Api->Run(*Line, Buffer.GetData(), Buffer.Num(), &Result);
	if (Result != EScctConsoleResult::Done)
	{
		return false;
	}
	Buffer.Last() = TEXT('\0');
	TArray<FString> Printed;
	FString(Buffer.GetData()).ParseIntoArray(Printed, TEXT("\n"), false);
	if (Length > 0)
	{
		for (const FString& PrintedLine : Printed)
		{
			ScctConsoleSay(Ar, PrintedLine);
		}
	}

	// whereami copy: the report goes on the clipboard here, where the window is.
	TArray<TCHAR> Copy;
	Copy.SetNumZeroed(4096);
	if (Api->TakeCopy(Copy.GetData(), Copy.Num()) > 0)
	{
		Copy.Last() = TEXT('\0');
		FPlatformApplicationMisc::ClipboardCopy(Copy.GetData());
	}
	return true;
}

void ScctConsoleCommands::SayUnknown(const TCHAR* Cmd, FOutputDevice& Ar)
{
	FString Verb = FString(Cmd).TrimStartAndEnd();
	int32 Space = INDEX_NONE;
	if (Verb.FindChar(TEXT(' '), Space))
	{
		Verb.LeftInline(Space);
	}
	ScctConsoleSay(Ar, FString::Printf(TEXT("unknown command '%s' (type help)"), *Verb.ToLower()));
}
