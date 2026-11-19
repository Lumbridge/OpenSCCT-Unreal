#include "Play/ScctGameMode.h"
#include "Engine/GameInstance.h"
#include "Menu/ScctLoadingSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Demo/ScctDemoPlayback.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Hud/ScctChatSubsystem.h"
#include "Hud/ScctHudEdit.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/ScctMatchMenuWidget.h"
#include "Menu/ScctMenuNav.h"
#include "Hub/ScctHubSubsystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctViewPawn.h"

namespace
{
	/** A yes/no URL option: on when present, unless its value says otherwise. */
	bool ParseSwitch(const FString& Options, const TCHAR* Key)
	{
		if (!UGameplayStatics::HasOption(Options, Key))
		{
			return false;
		}
		const FString Value = UGameplayStatics::ParseOption(Options, Key);
		return !(Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("no"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("off"), ESearchCase::IgnoreCase));
	}

	/** ?Sidearm=: the simulation's number of a sidearm by its name (EScctSidearm), or a number; -1 for none. */
	int32 ParseSidearm(const FString& Value, int32 Default)
	{
		static const TCHAR* const Names[] = { TEXT("suppressed"), TEXT("automatic"), TEXT("magnum44"), TEXT("desert_eagle"), TEXT("unsuppressed") };
		if (Value.IsEmpty())
		{
			return Default;
		}
		if (Value.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			return -1;
		}
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			if (Value.Equals(Names[Index], ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return Value.IsNumeric() ? FMath::Clamp(FCString::Atoi(*Value), -1, static_cast<int32>(UE_ARRAY_COUNT(Names)) - 1) : Default;
	}

	/** The bots' skill as the bridge counts it: 0 default, 1 easy, 2 normal, 3 hard, 4 expert. */
	int32 ParseDifficulty(const FString& Value)
	{
		static const TCHAR* const Names[] = { TEXT("default"), TEXT("easy"), TEXT("normal"), TEXT("hard"), TEXT("expert") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			if (Value.Equals(Names[Index], ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return FMath::Clamp(FCString::Atoi(*Value), 0, 4);
	}
}

AScctGameMode::AScctGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AScctViewPawn::StaticClass();
	PlayerControllerClass = AScctPlayerController::StaticClass();
}

void AScctGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// A level is named after the OpenSCCT map it was imported from, unless the URL says otherwise.
	Match.Map = UGameplayStatics::ParseOption(Options, TEXT("ScctMap"));
	if (Match.Map.IsEmpty())
	{
		Match.Map = FPackageName::GetShortName(MapName);
		Match.Map.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
	}

	const FString Mode = UGameplayStatics::ParseOption(Options, TEXT("Mode"));
	if (!Mode.IsEmpty())
	{
		Match.Mode = Mode;
	}

	Match.bMerc = UGameplayStatics::ParseOption(Options, TEXT("Side")).Equals(TEXT("merc"), ESearchCase::IgnoreCase);
	Match.Bots = UGameplayStatics::GetIntOption(Options, TEXT("Bots"), Match.Bots);
	Match.Difficulty = ParseDifficulty(UGameplayStatics::ParseOption(Options, TEXT("Difficulty")));
	Match.bSidearms = ParseSwitch(Options, TEXT("Sidearms"));
	Match.Sidearm = ParseSidearm(UGameplayStatics::ParseOption(Options, TEXT("Sidearm")), Match.Sidearm);
	Match.bRifles = ParseSwitch(Options, TEXT("Rifles"));
	Match.bOfflineRules = ParseSwitch(Options, TEXT("Practice"));
	Match.bPlayLevel = ParseSwitch(Options, TEXT("PlayLevel"));
	Match.bUnlimitedGear = ParseSwitch(Options, TEXT("UnlimitedGear"));
	Match.bResponsiveSmoke = ParseSwitch(Options, TEXT("ResponsiveSmoke"));
	Match.MatchPlayers = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("MatchPlayers"), 0), 0, 24);
	Match.PlayerName = UGameplayStatics::ParseOption(Options, TEXT("Name"));
	Match.TimeLimit = UGameplayStatics::GetIntOption(Options, TEXT("TimeLimit"), -1);
	Match.SpyLives = UGameplayStatics::GetIntOption(Options, TEXT("SpyLives"), -1);
	Match.MercLives = UGameplayStatics::GetIntOption(Options, TEXT("MercLives"), -1);
	Match.RespawnSeconds = UGameplayStatics::GetIntOption(Options, TEXT("Respawn"), -1);
	bOnline = ParseSwitch(Options, UScctOnlineSubsystem::OnlineOption);
	FParse::Value(FCommandLine::Get(), TEXT("ScctMatchMenu="), OpenMenuIn);

	// -ScctLeaveMatchAfter counts for the first -ScctLeaveMatchTimes levels of the run (default 1), so a run that
	// goes menu, match, menu, match comes to an end.
	static int32 LeavesAsked = 0;
	int32 LeaveTimes = 1;
	FParse::Value(FCommandLine::Get(), TEXT("ScctLeaveMatchTimes="), LeaveTimes);
	if (LeavesAsked < LeaveTimes && FParse::Value(FCommandLine::Get(), TEXT("ScctLeaveMatchAfter="), LeaveMatchIn))
	{
		++LeavesAsked;
	}
}

void AScctGameMode::StartPlay()
{
	Super::StartPlay();

	UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(this);
	if (Session == nullptr)
	{
		return;
	}

	UScctDemoPlayback* Demo = UScctDemoPlayback::Get(this);
	if (Demo != nullptr && Demo->ClaimLevel(GetWorld(), Match.Map, OptionsString))
	{
		// A demo plays the recording in this level: no match starts.
		bDemo = true;
		Demo->PresentIn(Session, Match.Map);
		return;
	}

	UScctHubSubsystem* Hub = UScctHubSubsystem::Get(this);
	if (Hub != nullptr && Hub->ShouldPresent(GetWorld()))
	{
		// The hub is the hub server's: this level only presents the session the hub visit puts in the bridge's slot.
		UE_LOG(LogOpenSCCT, Log, TEXT("Level %s presents the hub visit (no local match)"), *Match.Map);
		Hub->PresentIn(Session);
		return;
	}

	if (bOnline && Online != nullptr)
	{
		// The round is the server's: this level only presents the session the visit puts in the bridge's slot.
		UE_LOG(LogOpenSCCT, Log, TEXT("Level %s presents the online visit's round (no local match)"), *Match.Map);
		Online->PresentIn(Session);
		return;
	}

	if (Online != nullptr && Online->GetState() != EScctOnlineState::Idle)
	{
		// Starting a local match ends a visit in the bridge anyway; leaving first tells the server.
		Online->Leave();
	}
	Session->StartMatch(Match);
}

void AScctGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDemo)
	{
		return;
	}

	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || !Player->IsLocalController())
	{
		return;
	}
	if (!IsValid(MatchMenu))
	{
		// Above the HUD (10), which it never covers while the menu is closed and nothing is waited for.
		MatchMenu = CreateWidget<UScctMatchMenuWidget>(Player, UScctMatchMenuWidget::StaticClass());
		if (!IsValid(MatchMenu))
		{
			return;
		}
		MatchMenu->OnResumed.BindUObject(this, &AScctGameMode::SetMenuOpen, false);
		MatchMenu->OnWantsOpen.BindUObject(this, &AScctGameMode::SetMenuOpen, true);
		MatchMenu->AddToViewport(30);
		GiveInputToGame(Player);

		// What stops play from now on is shown as it happens; what already did (the start in StartPlay) at once.
		if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
		{
			Session->OnPlayError.AddUObject(this, &AScctGameMode::HandlePlayError);
			if (Session->GetPlayError().Kind != EScctPlayErrorKind::None)
			{
				HandlePlayError(Session->GetPlayError());
			}
		}
	}

	if (MatchMenu->IsShowingPlayError())
	{
		TickPlayError(Player, DeltaSeconds);
		return;
	}
	MatchMenu->TickResults(DeltaSeconds);

	// The menu opens on the player's own binding (ShowMenu, Quit), which arrives as an interface command.
	AScctPlayerController* ScctPlayer = Cast<AScctPlayerController>(Player);
	if (ScctPlayer != nullptr && CommandSource.Get() != ScctPlayer)
	{
		CommandSource = ScctPlayer;
		ScctPlayer->OnLocalCommand.AddUObject(this, &AScctGameMode::HandleLocalCommand);
	}

	// While it is open the controls raise no commands (the player's keys are held back), so the keys that close
	// it are read here.
	if (MatchMenu->IsMenuOpen() && !MatchMenu->IsShowingLocalResults())
	{
		// The press that opened the menu must not close it again: whether this actor ticks before or after the
		// player controller is not fixed, so that press can still read as "just pressed" in the next frame. A
		// close key counts once every close key has been seen up since the menu opened.
		bool bAnyDown = false;
		bool bAnyPressed = false;
		for (const FKey& Key : CloseKeys)
		{
			bAnyDown |= Player->IsInputKeyDown(Key);
			bAnyPressed |= Player->WasInputKeyJustPressed(Key);
		}
		if (bCloseKeysArmed && bAnyPressed)
		{
			SetMenuOpen(false);
		}
		else if (!bAnyDown && !bAnyPressed)
		{
			bCloseKeysArmed = true;
		}
	}
	if (OpenMenuIn >= 0.0f)
	{
		OpenMenuIn -= DeltaSeconds;
		if (OpenMenuIn < 0.0f)
		{
			SetMenuOpen(true);
		}
	}
	if (LeaveMatchIn >= 0.0f)
	{
		LeaveMatchIn -= DeltaSeconds;
		if (LeaveMatchIn < 0.0f)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctLeaveMatchAfter: leaving the match as the in-match menu's last button does"));
			MatchMenu->LeaveMatch();
		}
	}
}

void AScctGameMode::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	// Quit (F11 in the stock layout) does nothing, as in OpenSCCT's Godot client, which lists it as a legacy
	// shortcut with no handler ("use the game menu to quit"): the way out of a match is the menu's last button.
	if (bPressed && IsValid(MatchMenu) && !MatchMenu->IsMenuOpen() && Name == TEXT("ShowMenu"))
	{
		SetMenuOpen(true);
	}
}

void AScctGameMode::HandlePlayError(const FScctPlayError& Error)
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || !IsValid(MatchMenu))
	{
		return;
	}
	if (MatchMenu->IsMenuOpen())
	{
		SetMenuOpen(false);
	}
	MatchMenu->ShowPlayError(Error);

	// The page takes the mouse and the keys as the menu does; the controls hold everything back (no match is left).
	CloseKeys = TArray<FKey>();
	if (AScctPlayerController* ScctPlayer = Cast<AScctPlayerController>(Player))
	{
		CloseKeys = ScctPlayer->FindKeysBoundTo(TEXT("ShowMenu"));
		ScctPlayer->SetMenuOpen(true);
	}
	CloseKeys.AddUnique(EKeys::Escape);
	// The controller's Start, B and Back too: a player on a pad alone has no key bound to ShowMenu.
	CloseKeys.Append(ScctMenuNav::PadCloseKeys());
	bCloseKeysArmed = false;
	Player->SetShowMouseCursor(true);
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	Player->SetInputMode(Mode);

	PlayErrorLeaveIn = -1.0f;
	if (FApp::IsUnattended())
	{
		// Nobody is there to press Escape: an unattended run leaves on its own, after time for its screenshots.
		PlayErrorLeaveIn = 10.0f;
		FParse::Value(FCommandLine::Get(), TEXT("ScctPlayErrorLeaveAfter="), PlayErrorLeaveIn);
	}
}

void AScctGameMode::TickPlayError(APlayerController* Player, float DeltaSeconds)
{
	// Escape (and the keys bound to ShowMenu) leave, as the page says; the press that was down when it came up does not.
	bool bAnyDown = false;
	bool bAnyPressed = false;
	for (const FKey& Key : CloseKeys)
	{
		bAnyDown |= Player->IsInputKeyDown(Key);
		bAnyPressed |= Player->WasInputKeyJustPressed(Key);
	}
	if (bCloseKeysArmed && bAnyPressed)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPlayError: Escape pressed, leaving"));
		MatchMenu->LeaveFromPlayError();
	}
	else if (!bAnyDown && !bAnyPressed)
	{
		bCloseKeysArmed = true;
	}

	if (PlayErrorLeaveIn >= 0.0f)
	{
		PlayErrorLeaveIn -= DeltaSeconds;
		if (PlayErrorLeaveIn < 0.0f)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPlayError: unattended run, leaving as the page's LEAVE button does"));
			MatchMenu->LeaveFromPlayError();
		}
	}
}

void AScctGameMode::GiveInputToGame(APlayerController* Player)
{
	// The input mode belongs to the game's viewport, which outlives the level: after the front end (whose menu
	// takes all input) the viewport would go on ignoring the keyboard and the mouse in the match.
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	const bool bWasIgnored = Viewport != nullptr && Viewport->IgnoreInput();
	if (FApp::IsUnattended())
	{
		// Nobody is at the keyboard and the window is off-screen: the game takes input again, but the mouse is
		// neither captured nor locked to a window the person at this machine cannot see.
		if (Viewport != nullptr)
		{
			Viewport->SetIgnoreInput(false);
		}
	}
	else
	{
		Player->SetInputMode(FInputModeGameOnly());
	}
	Player->SetShowMouseCursor(false);
	if (bWasIgnored)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Level %s: the keyboard and mouse are the game's again (a menu had them)"), *Match.Map);
	}
}

void AScctGameMode::SetMenuOpen(bool bOpen)
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || !IsValid(MatchMenu))
	{
		return;
	}
	if (bOpen && MatchMenu->IsShowingLocalResults())
	{
		// The results take input from any fullscreen controls still active when the round ended.
		if (UScctHudEditSubsystem* HudEdit = GetWorld()->GetSubsystem<UScctHudEditSubsystem>())
		{
			HudEdit->Stop();
		}
		if (UScctChatSubsystem* Chat = GetWorld()->GetSubsystem<UScctChatSubsystem>())
		{
			Chat->Close();
		}
	}

	// The match goes on underneath (it is the server's, or the bots'): the menu takes the mouse and the keys, and
	// the controls let go of everything the player held.
	AScctPlayerController* ScctPlayer = Cast<AScctPlayerController>(Player);
	if (bOpen && !MatchMenu->IsMenuOpen())
	{
		CloseKeys = ScctPlayer != nullptr ? ScctPlayer->FindKeysBoundTo(TEXT("ShowMenu")) : TArray<FKey>();
		CloseKeys.AddUnique(EKeys::Escape);
		// The controller's Start, B and Back too: a player on a pad alone has no key bound to ShowMenu.
		CloseKeys.Append(ScctMenuNav::PadCloseKeys());
		bCloseKeysArmed = false;
	}
	MatchMenu->SetMenuOpen(bOpen);
	if (ScctPlayer != nullptr)
	{
		ScctPlayer->SetMenuOpen(bOpen);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("In-match menu %s"), bOpen ? TEXT("opened") : TEXT("closed"));
	if (bOpen)
	{
		Player->SetShowMouseCursor(true);
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		Player->SetInputMode(Mode);
	}
	else
	{
		GiveInputToGame(Player);
	}
}

void AScctGameMode::StartToLeaveMap()
{
	// The loading page reads the map, the mode and the side from the travel's URL, which the engine empties next.
	if (const UGameInstance* Instance = GetGameInstance())
	{
		if (UScctLoadingSubsystem* Loading = Instance->GetSubsystem<UScctLoadingSubsystem>())
		{
			Loading->NoteTravel();
		}
	}
	Super::StartToLeaveMap();
}
