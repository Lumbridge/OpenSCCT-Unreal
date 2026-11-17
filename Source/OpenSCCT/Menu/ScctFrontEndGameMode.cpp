#include "Menu/ScctFrontEndGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Menu/ScctFrontEndWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctViewPawn.h"

namespace
{
	/** A place in an imported map that makes a good picture behind the menu. */
	struct FBackdrop
	{
		const TCHAR* Map;
		FVector Location;
		float Yaw;
		float Pitch;
		/** Degrees the camera turns to either side of Yaw. */
		float Sway;
	};

	// The first of these whose level has been imported is used.
	const FBackdrop Backdrops[] = {
		{ TEXT("AquaE"), FVector(2640.0, 329.0, 260.0), 90.0f, 0.0f, 9.0f },
		{ TEXT("ClubE"), FVector(3952.0, 1616.0, -270.0), 200.0f, 15.0f, 8.0f },
		{ TEXT("KrauE"), FVector(7000.0, 0.0, -380.0), 90.0f, -20.0f, 8.0f },
	};

	const FName FallbackTag(TEXT("ScctFallback"));

	/** Seconds one sway of the camera takes, there and back. */
	constexpr float SwayPeriod = 46.0f;
}

AScctFrontEndGameMode::AScctFrontEndGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AScctViewPawn::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

void AScctFrontEndGameMode::StartPlay()
{
	Super::StartPlay();

	for (const FBackdrop& Backdrop : Backdrops)
	{
		const FString Level = UScctOnlineSubsystem::LevelOf(Backdrop.Map);
		if (!FPackageName::DoesPackageExist(Level))
		{
			continue;
		}

		bool bStarted = false;
		ULevelStreamingDynamic::LoadLevelInstance(this, Level, FVector::ZeroVector, FRotator::ZeroRotator, bStarted);
		if (!bStarted)
		{
			continue;
		}

		// The map brings its own light; the level's stand-in scene would shine into it.
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(FallbackTag))
			{
				It->Destroy();
			}
		}
		CameraLocation = Backdrop.Location;
		CameraRotation = FRotator(Backdrop.Pitch, Backdrop.Yaw, 0.0f);
		CameraSway = Backdrop.Sway;
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: backdrop is %s"), Backdrop.Map);
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: no imported map to show behind the menu; using the level's own scene"));
}

void AScctFrontEndGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Seconds += DeltaSeconds;
	ShowMenu();
	MoveCamera();
}

void AScctFrontEndGameMode::ShowMenu()
{
	if (IsValid(Menu))
	{
		return;
	}
	// The widget needs the player's viewport, which is there by the first frame.
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || !Player->IsLocalController())
	{
		return;
	}

	Menu = CreateWidget<UScctFrontEndWidget>(Player, UScctFrontEndWidget::StaticClass());
	if (!IsValid(Menu))
	{
		return;
	}
	Menu->AddToViewport(20);

	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(Mode);
	Player->SetShowMouseCursor(true);

	FString Page;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctMenuPage="), Page) || FParse::Value(FCommandLine::Get(), TEXT("ScctOpenPage="), Page))
	{
		Menu->ShowPageNamed(Page);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: front end shown"));
}

void AScctFrontEndGameMode::MoveCamera()
{
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	AScctViewPawn* ViewPawn = Player != nullptr ? Cast<AScctViewPawn>(Player->GetPawn()) : nullptr;
	if (ViewPawn == nullptr)
	{
		return;
	}

	// A slow look from side to side, with a breath of height: enough to feel alive behind the menu.
	const float Phase = Seconds * UE_TWO_PI / SwayPeriod;
	FScctBridgeView View = {};
	View.X = CameraLocation.X;
	View.Y = CameraLocation.Y;
	View.Z = CameraLocation.Z + 6.0f * FMath::Sin(Phase * 0.7f);
	View.Pitch = CameraRotation.Pitch;
	View.Yaw = CameraRotation.Yaw + CameraSway * FMath::Sin(Phase);
	View.Fov = 70.0f;
	View.Valid = 1;
	ViewPawn->ApplyView(View);
}
