#include "Menu/ScctFrontEndGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Menu/ScctLoadingSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Demo/ScctDemoPlayback.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Menu/SScctGearPage.h"
#include "Menu/SScctStrategicMapPage.h"
#include "Menu/ScctFrontEndWidget.h"
#include "Menu/ScctMenuBackdrop.h"
#include "Menu/ScctTutorialPlay.h"
#include "Misc/CommandLine.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Online/ScctMapFolders.h"
#include "Social/ScctLaunchOptions.h"
#include "OpenSCCT.h"
#include "Play/ScctViewPawn.h"
#include "ShaderCompiler.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SNullWidget.h"

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

	/** The longest the backdrop waits for its shaders (wall clock seconds) before it is shown anyway. */
	constexpr double BackdropShaderWait = 12.0;

	bool bTookEditorMap = false;

	bool IsEditorOptionValue(const FString& Value)
	{
		for (const TCHAR Ch : Value)
		{
			if (!FChar::IsAlnum(Ch) && Ch != '_' && Ch != '-')
			{
				return false;
			}
		}
		return !Value.IsEmpty() && Value.Len() < 32; // Map and Mode in FScctBridgeStartOptions
	}

	FString EditorPlayUrl(const FString& Folder)
	{
		const FString Id = FPaths::GetCleanFilename(Folder);
		const FString Resolved = ScctMapFolders::FolderOf(Id);
		if (!IsEditorOptionValue(Id) || Resolved.IsEmpty() || !FPaths::IsSamePath(Folder, Resolved)
			|| !FPaths::FileExists(Folder / TEXT("manifest.json")) || !FPaths::FileExists(Folder / TEXT("render/world.glb"))
			|| !FPackageName::DoesPackageExist(UScctOnlineSubsystem::RuntimeLevel))
		{
			return {};
		}

		FString Url = FString::Printf(TEXT("%s?ScctMap=%s"), UScctOnlineSubsystem::RuntimeLevel, *Id);
		for (const TCHAR* Key : { TEXT("Side"), TEXT("Mode"), TEXT("Bots"), TEXT("Difficulty"), TEXT("MatchPlayers"), TEXT("PlayLevel"), TEXT("ResponsiveSmoke") })
		{
			FString Value;
			if (FParse::Value(FCommandLine::Get(), *FString::Printf(TEXT("-ScctEditor%s="), Key), Value) && IsEditorOptionValue(Value))
			{
				Url += FString::Printf(TEXT("?%s=%s"), Key, *Value);
			}
		}
		return Url;
	}

	// Shipping ignores the positional map URL. Travel after startup so both kinds of build use the same path.
	bool OpenEditorMap(UWorld& World)
	{
		const FString& Folder = ScctLaunchOptions::EditorMap();
		if (bTookEditorMap || Folder.IsEmpty())
		{
			return false;
		}
		bTookEditorMap = true; // Quitting the preview returns to the normal menu.
		ScctMapFolders::SetEditorRoot(FPaths::GetPath(Folder));
		const FString Url = EditorPlayUrl(Folder);
		if (Url.IsEmpty())
		{
			ScctMapFolders::SetEditorRoot(FString());
			const FText Error = FText::Format(NSLOCTEXT("OpenSCCT", "EditorMapUnavailable",
				"Could not open the editor build at {0}. Check the compiled map, the shared content folder and the Unreal installation."), FText::FromString(Folder));
			UE_LOG(LogOpenSCCT, Error, TEXT("%s"), *Error.ToString());
			FMessageDialog::Open(EAppMsgType::Ok, Error);
			return false;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Editor play: %s from %s"), *Url, *Folder);
		GEngine->SetClientTravel(&World, *Url, TRAVEL_Absolute);
		return true;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScctEditorMapPrecedence, "OpenSCCT.Editor.MapPreview.Source",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FScctEditorMapPrecedence::RunTest(const FString&)
{
	// Exercise the production parser and bridge catalog with a real editor build, without starting a match.
	const FString Folder = ScctLaunchOptions::EditorMap().IsEmpty()
		? ScctMapFolders::FolderOf(TEXT("AquaE")) : ScctLaunchOptions::EditorMap();
	if (Folder.IsEmpty())
	{
		AddInfo(TEXT("No AquaE installed. Supply --scct-editor-map=<compiled SVMap directory> to exercise editor map resolution."));
		return true;
	}
	const FString Id = FPaths::GetCleanFilename(Folder);
	const FString NormalLevel = UScctOnlineSubsystem::LevelOf(Id);
	ScctMapFolders::SetEditorRoot(FPaths::GetPath(Folder));
	ON_SCOPE_EXIT { ScctMapFolders::SetEditorRoot(FString()); };
	TestTrue(TEXT("Editor build is the simulation catalog's first match"), FPaths::IsSamePath(Folder, ScctMapFolders::FolderOf(Id)));
	const FString Url = EditorPlayUrl(Folder);
	TestTrue(TEXT("Editor build uses the runtime map"), Url.StartsWith(FString::Printf(TEXT("%s?ScctMap=%s"), UScctOnlineSubsystem::RuntimeLevel, *Id)));
	TestTrue(TEXT("A different build directory with the same ID is rejected"), EditorPlayUrl(Folder / TEXT("other") / Id).IsEmpty());
	AddInfo(FString::Printf(TEXT("Normal selection: %s. Editor selection: %s from %s"), *NormalLevel, *Url, *Folder));
	return true;
}
#endif

AScctFrontEndGameMode::AScctFrontEndGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AScctViewPawn::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

void AScctFrontEndGameMode::StartPlay()
{
	Super::StartPlay();

	if (UGameInstance* Instance = GetGameInstance())
	{
		UGameViewportClient* Viewport = Instance->GetGameViewportClient();
		UScctLoadingSubsystem* Loading = Instance->GetSubsystem<UScctLoadingSubsystem>();
		if (Viewport != nullptr && Loading != nullptr)
		{
			// Under the menu so it can be used as soon as its widget has been built.
			LoadingCover = Loading->MakeFrontEndCover();
			Viewport->AddViewportWidgetContent(LoadingCover.ToSharedRef(), 0);
		}
	}

	if (!bTookEditorMap && !ScctLaunchOptions::EditorMap().IsEmpty())
	{
		return;
	}
	ScctMapFolders::SetEditorRoot(FString());

	for (const FBackdrop& Backdrop : Backdrops)
	{
		// A backdrop is streamed in as a level instance, which takes no URL options, so only an imported level can be one
		// (the runtime level reads its map from the world's ?ScctMap=).
		const FString Level = UScctOnlineSubsystem::ImportedLevelOf(Backdrop.Map);
		if (!FPackageName::DoesPackageExist(Level))
		{
			continue;
		}

		bool bStarted = false;
		ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstance(this, Level, FVector::ZeroVector, FRotator::ZeroRotator, bStarted);
		if (!bStarted || Streaming == nullptr)
		{
			continue;
		}

		// Loaded hidden: the cover stays behind the menu until the map can be drawn (RevealBackdrop).
		Streaming->SetShouldBeVisible(false);
		BackdropLevel = Streaming;
		BackdropMap = Backdrop.Map;
		BackdropLocation = Backdrop.Location;
		BackdropRotation = FRotator(Backdrop.Pitch, Backdrop.Yaw, 0.0f);
		BackdropSway = Backdrop.Sway;
		BackdropStarted = FPlatformTime::Seconds();
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: backdrop %s is loading behind the cover"), Backdrop.Map);
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: no imported map to show behind the menu; using the level's own scene"));
}

void AScctFrontEndGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (OpenEditorMap(*GetWorld()))
	{
		return;
	}
	Seconds += DeltaSeconds;
	ShowMenu();
	RevealBackdrop();
	MoveCamera();
	if (IsValid(Menu) && !IsValid(BackdropLevel))
	{
		RemoveLoadingCover();
	}

	// The live backdrop, once the menu and the still are up (the still is settled when nothing is waited for any more).
	if (IsValid(Menu))
	{
		if (!IsValid(LiveBackdrop))
		{
			LiveBackdrop = NewObject<UScctMenuBackdrop>(this);
		}
		LiveBackdrop->Tick(*this, DeltaSeconds, !IsValid(BackdropLevel), StillMap, StillLevel);
	}
}

void AScctFrontEndGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveLoadingCover();
	if (IsValid(LiveBackdrop))
	{
		LiveBackdrop->Shutdown(GetWorld());
	}
	Super::EndPlay(EndPlayReason);
}

void AScctFrontEndGameMode::RemoveLoadingCover()
{
	if (LoadingCover.IsValid())
	{
		const UGameInstance* Instance = GetGameInstance();
		if (UGameViewportClient* Viewport = Instance != nullptr ? Instance->GetGameViewportClient() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(LoadingCover.ToSharedRef());
		}
		LoadingCover.Reset();
	}
}

void AScctFrontEndGameMode::SetStillCamera(const FVector& Location, const FRotator& Rotation, float Sway)
{
	CameraLocation = Location;
	CameraRotation = Rotation;
	CameraSway = Sway;
}

void AScctFrontEndGameMode::RevealBackdrop()
{
	if (!IsValid(BackdropLevel))
	{
		return;
	}
	const double Waited = FPlatformTime::Seconds() - BackdropStarted;

	if (!BackdropLevel->ShouldBeVisible())
	{
		if (BackdropLevel->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: backdrop %s failed to load; the level's own scene stays"), BackdropMap);
			BackdropLevel = nullptr;
			return;
		}
		if (!BackdropLevel->IsLevelLoaded())
		{
			return;
		}
		// Uncooked runs compile the map's materials on first sight: until then it draws grey or black.
		const int32 Jobs = GShaderCompilingManager != nullptr ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
		if (Jobs > 0 && Waited < BackdropShaderWait)
		{
			return;
		}
		BackdropLevel->SetShouldBeVisible(true);
		return;
	}
	if (!BackdropLevel->IsLevelVisible())
	{
		return;
	}

	// The map brings its own light; the level's stand-in scene would shine into it.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(FallbackTag))
		{
			It->Destroy();
		}
	}
	CameraLocation = BackdropLocation;
	CameraRotation = BackdropRotation;
	CameraSway = BackdropSway;
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: backdrop is %s (shown after %.1f s)"), BackdropMap, Waited);
	StillLevel = BackdropLevel;
	StillMap = BackdropMap;
	BackdropLevel = nullptr;
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
	if (LoadingCover.IsValid())
	{
		// Keep the temporary scene hidden, and leave the menu itself unobstructed.
		LoadingCover->SetContent(SNullWidget::NullWidget);
	}

	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(Mode);
	Player->SetShowMouseCursor(true);

	// Back from watching a demo: the Demos page again, where the demo was picked.
	if (UScctDemoPlayback* Demo = UScctDemoPlayback::Get(this); Demo != nullptr && Demo->TakeReturnToDemos())
	{
		Menu->ShowPageNamed(TEXT("demos"));
	}

	// Back from a tutorial or a visit: the Tutorials page again, where it was started.
	UScctTutorialSubsystem* Tutorials = UScctTutorialSubsystem::Get(this);
	if (Tutorials != nullptr && Tutorials->TakeReturnToTutorials())
	{
		Menu->ShowPageNamed(TEXT("tutorials"));
	}

	FString Page;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctMenuPage="), Page) || FParse::Value(FCommandLine::Get(), TEXT("ScctOpenPage="), Page))
	{
		Menu->ShowPageNamed(Page);
	}
	FString OptionsPage;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctOptions="), OptionsPage))
	{
		Menu->ShowOptions(FName(*OptionsPage));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ScctConfirmTest")) || FParse::Param(FCommandLine::Get(), TEXT("ScctMenuQuitConfirm")))
	{
		Menu->AskQuit();
	}
	// -ScctGear[=spy|merc] opens the Equipment page over the offline page; -ScctGearApply also picks and applies.
	FString GearSide, GearScript;
	const bool bGearApply = FParse::Value(FCommandLine::Get(), TEXT("ScctGearApply="), GearScript, false);
	if (bGearApply || FParse::Param(FCommandLine::Get(), TEXT("ScctGear")) || FParse::Value(FCommandLine::Get(), TEXT("ScctGear="), GearSide))
	{
		if (bGearApply)
		{
			GearScript.Split(TEXT(","), &GearSide, nullptr);
		}
		Menu->ShowPageNamed(TEXT("offline"));
		const int32 Side = GearSide.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : 0;
		if (const TSharedPtr<SScctGearPage> Gear = Menu->ShowGear(Side); Gear.IsValid() && bGearApply)
		{
			Gear->RunScript(GearScript);
		}
	}
	// -ScctStrategicMap=<Map> opens a map's strategic map as the lobby's STRATEGIC MAP does (the lobby's own map
	// needs a server); with -ScctStrategicMapUp its UP is pressed at once.
	FString StrategicMap;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctStrategicMap="), StrategicMap))
	{
		if (const TSharedPtr<SScctStrategicMapPage> MapPage = Menu->ShowStrategicMap(StrategicMap);
			MapPage.IsValid() && FParse::Param(FCommandLine::Get(), TEXT("ScctStrategicMapUp")))
		{
			MapPage->FloorUp();
		}
	}
	// -ScctTutorial / -ScctVisit: a tutorial or a visit started from its page, once a run (Menu/ScctTutorialPlay.h).
	if (Tutorials != nullptr)
	{
		Tutorials->RunCommandLine(Menu);
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

void AScctFrontEndGameMode::StartToLeaveMap()
{
	// The match behind the menu stops before the level that was asked for starts to load.
	if (IsValid(LiveBackdrop))
	{
		LiveBackdrop->Shutdown(GetWorld());
	}

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
