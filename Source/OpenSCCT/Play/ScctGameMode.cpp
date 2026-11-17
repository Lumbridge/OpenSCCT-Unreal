#include "Play/ScctGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/ScctMatchMenuWidget.h"
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
	Match.bRifles = ParseSwitch(Options, TEXT("Rifles"));
	Match.bOfflineRules = ParseSwitch(Options, TEXT("Practice"));
	Match.PlayerName = UGameplayStatics::ParseOption(Options, TEXT("Name"));
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
		MatchMenu->AddToViewport(30);
		GiveInputToGame(Player);
	}

	if (Player->WasInputKeyJustPressed(EKeys::Escape))
	{
		SetMenuOpen(!MatchMenu->IsMenuOpen());
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

	// The match goes on underneath (it is the server's, or the bots'): the menu only takes the mouse.
	MatchMenu->SetMenuOpen(bOpen);
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
