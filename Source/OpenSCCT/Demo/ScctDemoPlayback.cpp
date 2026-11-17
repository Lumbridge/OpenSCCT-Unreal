#include "Demo/ScctDemoPlayback.h"

#include "Demo/SScctDemoHud.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Widgets/SWeakWidget.h"

const TCHAR* const UScctDemoPlayback::DemoOption = TEXT("ScctDemo");

namespace
{
	/** A text of any length from a call that returns its full length. */
	FString ScctDemoPlayReadText(int32 (__cdecl* Get)(int32, TCHAR*, int32), int32 Kind)
	{
		TCHAR Short[512];
		const int32 Length = Get(Kind, Short, UE_ARRAY_COUNT(Short));
		if (Length <= 0)
		{
			return FString();
		}
		if (Length < UE_ARRAY_COUNT(Short))
		{
			return FString(Short);
		}
		TArray<TCHAR> Long;
		Long.SetNumZeroed(Length + 1);
		Get(Kind, Long.GetData(), Long.Num());
		return FString(Long.GetData());
	}

	/** "1:05" or "1:02:03" for a number of seconds. */
	FString ScctDemoPlayClock(int32 Seconds)
	{
		Seconds = FMath::Max(Seconds, 0);
		return Seconds >= 3600
			? FString::Printf(TEXT("%d:%02d:%02d"), Seconds / 3600, Seconds / 60 % 60, Seconds % 60)
			: FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}

	const TCHAR* ScctDemoCameraName(int32 Camera)
	{
		switch (static_cast<EScctDemoCamera>(Camera))
		{
		case EScctDemoCamera::FirstPerson: return TEXT("first person");
		case EScctDemoCamera::ThirdPerson: return TEXT("third person");
		case EScctDemoCamera::Free: return TEXT("free camera");
		default: return TEXT("as played");
		}
	}
}

UScctDemoPlayback* UScctDemoPlayback::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UScctDemoPlayback>() : nullptr;
}

const FScctDemoPlayApi* UScctDemoPlayback::Api()
{
	return ScctFeature<FScctDemoPlayApi>();
}

FString UScctDemoPlayback::PlayText(EScctDemoPlayText Kind)
{
	const FScctDemoPlayApi* Table = Api();
	return Table != nullptr ? ScctDemoPlayReadText(Table->GetPlayText, static_cast<int32>(Kind)) : FString();
}

void UScctDemoPlayback::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* CommandLine = FCommandLine::Get();
	if (FParse::Value(CommandLine, TEXT("ScctDemo="), CommandLineDemo))
	{
		CommandLineDemo = FPaths::ConvertRelativePathToFull(CommandLineDemo);
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: -ScctDemo, this run watches %s"), *CommandLineDemo);
	}
	FParse::Value(CommandLine, TEXT("ScctDemoSpeed="), StartSpeed);
	FString Camera;
	if (FParse::Value(CommandLine, TEXT("ScctDemoCamera="), Camera))
	{
		StartCamera = Camera.StartsWith(TEXT("first"), ESearchCase::IgnoreCase) ? static_cast<int32>(EScctDemoCamera::FirstPerson)
			: Camera.StartsWith(TEXT("free"), ESearchCase::IgnoreCase) ? static_cast<int32>(EScctDemoCamera::Free)
			: Camera.StartsWith(TEXT("as"), ESearchCase::IgnoreCase) ? static_cast<int32>(EScctDemoCamera::AsPlayed)
			: static_cast<int32>(EScctDemoCamera::ThirdPerson);
	}
	FParse::Value(CommandLine, TEXT("ScctDemoSeek="), StartSeek);
	FParse::Value(CommandLine, TEXT("ScctDemoPlayer="), StartPlayer);
	FParse::Value(CommandLine, TEXT("ScctDemoPause="), PauseAfter);
	FParse::Value(CommandLine, TEXT("ScctDemoLeaveAfter="), LeaveAfter);
	int32 XRayValue = 1;
	if (FParse::Value(CommandLine, TEXT("ScctDemoXRay="), XRayValue) || FParse::Param(CommandLine, TEXT("ScctDemoXRay")))
	{
		bXRay = XRayValue != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: -ScctDemoXRay, X-ray %s"), bXRay ? TEXT("on") : TEXT("off"));
	}
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UScctDemoPlayback::HandleWorldCleanup);
}

void UScctDemoPlayback::Deinitialize()
{
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	XRay.Release();
	HideHud();
	if (PresentingWorld.IsValid())
	{
		if (const FScctDemoPlayApi* Table = Api())
		{
			Table->Stop();
		}
	}
	Super::Deinitialize();
}

void UScctDemoPlayback::Fail(const FString& Reason)
{
	Error = Reason;
	UE_LOG(LogOpenSCCT, Warning, TEXT("Demo: %s"), *Reason);
}

bool UScctDemoPlayback::Watch(const FString& Path)
{
	Error.Reset();
	const FScctDemoPlayApi* Table = Api();
	if (Table == nullptr)
	{
		Fail(TEXT("Demos cannot be watched: the simulation did not start (see the log)."));
		return false;
	}

	TCHAR Map[128] = {};
	if (Table->Open(*Path, 0, Map, UE_ARRAY_COUNT(Map)) == 0)
	{
		Fail(PlayText(EScctDemoPlayText::Error));
		return false;
	}

	OpenedMap = Map;
	// LevelOf can be a URL (the runtime level with ?ScctMap=), so whether the map can be drawn is IsPlayable's answer.
	const FString Level = UScctOnlineSubsystem::LevelOf(OpenedMap);
	if (!UScctOnlineSubsystem::IsPlayable(OpenedMap))
	{
		Table->Stop();
		Fail(FString::Printf(TEXT("The demo is on %s, which this game has neither imported nor can build from render files."), *OpenedMap));
		return false;
	}

	// A visit would lose its round to the demo (the bridge leaves it too); leaving here tells the server.
	if (UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(this))
	{
		if (Online->GetState() != EScctOnlineState::Idle)
		{
			Online->Leave();
		}
	}

	UWorld* World = GetGameInstance()->GetWorld();
	const FString Url = FString::Printf(TEXT("%s?%s=1"), *Level, DemoOption);
	UE_LOG(LogOpenSCCT, Log, TEXT("Demo: watching %s (map %s), opening %s"), *Path, *OpenedMap, *Url);
	if (World != nullptr && GEngine != nullptr)
	{
		GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
	}
	return true;
}

bool UScctDemoPlayback::ClaimLevel(UWorld* World, const FString& LevelMap, const FString& Options)
{
	if (UGameplayStatics::HasOption(Options, DemoOption))
	{
		return true;
	}
	if (CommandLineDemo.IsEmpty() || bCommandLineUsed || World == nullptr)
	{
		return false;
	}

	// -ScctDemo: the run's first level plays it (or opens the level it was recorded on).
	bCommandLineUsed = true;
	const FScctDemoPlayApi* Table = Api();
	TCHAR Map[128] = {};
	if (Table == nullptr || Table->Open(*CommandLineDemo, 0, Map, UE_ARRAY_COUNT(Map)) == 0)
	{
		Fail(Table == nullptr ? FString(TEXT("the simulation did not start")) : PlayText(EScctDemoPlayText::Error));
		return false;
	}
	OpenedMap = Map;
	if (!OpenedMap.Equals(LevelMap, ESearchCase::IgnoreCase))
	{
		const FString Url = FString::Printf(TEXT("%s?%s=1"), *UScctOnlineSubsystem::LevelOf(OpenedMap), DemoOption);
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: -ScctDemo was recorded on %s, opening %s"), *OpenedMap, *Url);
		GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
	}
	return true;
}

void UScctDemoPlayback::PresentIn(UScctSessionSubsystem* Session, const FString& LevelMap)
{
	const FScctDemoPlayApi* Table = Api();
	if (Session == nullptr || Table == nullptr)
	{
		return;
	}
	UWorld* World = Session->GetWorld();
	FScctDemoPlayInfo Info = {};
	Table->GetPlayInfo(&Info);
	if (Info.State == static_cast<int32>(EScctDemoPlayState::None))
	{
		// A level opened for a demo with none open (a level change of its own): back to the menus.
		Fail(TEXT("No demo is open."));
		Leave();
		return;
	}
	// A -ScctDemo level of another map has opened the demo's level: that one plays it.
	if (!PlayText(EScctDemoPlayText::Map).Equals(LevelMap, ESearchCase::IgnoreCase))
	{
		return;
	}

	PresentingWorld = World;
	PresentingSession = Session;
	bLeaving = false;
	PlayingSeconds = 0.0f;
	LastLoggedSecond = -1;
	if (Session->FollowBridgeSessions(true))
	{
		BeforeTicksHandle = Session->OnBeforeTicks.AddUObject(this, &UScctDemoPlayback::HandleBeforeTicks);
	}

	const int32 SessionId = Table->Start();
	if (SessionId == 0)
	{
		Fail(PlayText(EScctDemoPlayText::Error));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: playing %s on %s (%s, server '%s') as session %d"), *PlayText(EScctDemoPlayText::Path),
			*PlayText(EScctDemoPlayText::Map), *PlayText(EScctDemoPlayText::Mode), *PlayText(EScctDemoPlayText::Server), SessionId);
	}
	ShowHud(World);
}

void UScctDemoPlayback::HandleBeforeTicks(float DeltaSeconds)
{
	const FScctDemoPlayApi* Table = Api();
	if (Table == nullptr || bLeaving)
	{
		return;
	}

	// The playback bar goes up once the viewport is there (it is not yet during StartPlay).
	if (!Hud.IsValid())
	{
		ShowHud(PresentingWorld.Get());
	}
	if (Hud.IsValid())
	{
		Hud->DriveFrame(DeltaSeconds);
	}

	// Paused or seeking too: the outlines follow the bodies as drawn.
	XRay.Update(PresentingSession.Get(), bXRay);

	Table->Frame(DeltaSeconds);

	FScctDemoPlayInfo Info = {};
	Table->GetPlayInfo(&Info);
	if (Info.State != static_cast<int32>(EScctDemoPlayState::Playing))
	{
		return;
	}
	ApplyStartOptions(Info);

	PlayingSeconds += DeltaSeconds;
	const int32 Second = FMath::FloorToInt(PlayingSeconds);
	if (Second % 5 == 0 && Second != LastLoggedSecond)
	{
		LastLoggedSecond = Second;
		const int32 Rate = FMath::Max(Info.TickRate, 1);
		const UScctSessionSubsystem* Session = PresentingSession.Get();
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: %s %s / %s at %gx, camera %s, watching %s (%d of %d players), %d pawns drawn"),
			*PlayText(EScctDemoPlayText::State), *ScctDemoPlayClock((Info.CurrentTick - Info.FirstTick) / Rate),
			*ScctDemoPlayClock((Info.LastTick - Info.FirstTick) / Rate), Info.SpeedIndex >= 0 ? (Info.SpeedIndex == 0 ? 0.25 : Info.SpeedIndex == 1 ? 0.5 : Info.SpeedIndex == 2 ? 1.0 : Info.SpeedIndex == 3 ? 2.0 : 4.0) : 1.0,
			ScctDemoCameraName(Info.Camera), *PlayText(EScctDemoPlayText::Watching), Info.TargetIndex + 1, Info.TargetCount,
			Session != nullptr ? Session->GetPawns().Num() : 0);
	}

	if (PauseAfter >= 0.0f && PlayingSeconds >= PauseAfter)
	{
		PauseAfter = -1.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: -ScctDemoPause, pausing"));
		Table->Control(static_cast<int32>(EScctDemoCommand::SetPaused), 1);
	}
	if (LeaveAfter >= 0.0f && PlayingSeconds >= LeaveAfter)
	{
		LeaveAfter = -1.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: -ScctDemoLeaveAfter, back to the demos"));
		Leave();
	}
}

void UScctDemoPlayback::ApplyStartOptions(const FScctDemoPlayInfo& Info)
{
	const FScctDemoPlayApi* Table = Api();
	if (!bStartApplied && Info.LastTick > Info.FirstTick)
	{
		bStartApplied = true;
		if (StartSpeed > 0.0f)
		{
			const int32 Index = StartSpeed <= 0.3f ? 0 : StartSpeed <= 0.6f ? 1 : StartSpeed <= 1.5f ? 2 : StartSpeed <= 3.0f ? 3 : 4;
			Table->Control(static_cast<int32>(EScctDemoCommand::SetSpeed), Index);
		}
		if (StartCamera >= 0)
		{
			Table->Control(static_cast<int32>(EScctDemoCommand::SetCamera), StartCamera);
		}
		if (StartSeek > 0.0f)
		{
			const int32 Tick = Info.FirstTick + FMath::RoundToInt(StartSeek * FMath::Max(Info.TickRate, 1));
			UE_LOG(LogOpenSCCT, Log, TEXT("Demo: -ScctDemoSeek, seeking to %s (tick %d)"), *ScctDemoPlayClock(FMath::RoundToInt(StartSeek)), Tick);
			Table->Control(static_cast<int32>(EScctDemoCommand::Seek), Tick);
		}
	}
	if (StartPlayer > 0 && Info.TargetCount >= StartPlayer)
	{
		Table->Control(static_cast<int32>(EScctDemoCommand::WatchIndex), StartPlayer - 1);
		StartPlayer = 0;
	}
}

void UScctDemoPlayback::ShowHud(UWorld* World)
{
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	APlayerController* Player = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Hud.IsValid() || Viewport == nullptr || Player == nullptr)
	{
		return;
	}

	TWeakObjectPtr<UScctDemoPlayback> Weak(this);
	SAssignNew(Hud, SScctDemoHud)
		.OnLeave(FSimpleDelegate::CreateLambda([Weak] { if (Weak.IsValid()) { Weak->Leave(); } }))
		.XRayOn_Lambda([Weak] { return Weak.IsValid() && Weak->IsXRay(); })
		.OnToggleXRay(FSimpleDelegate::CreateLambda([Weak] { if (Weak.IsValid()) { Weak->SetXRay(!Weak->IsXRay()); } }));
	HudRoot = SNew(SWeakWidget).PossiblyNullContent(Hud);
	Viewport->AddViewportWidgetContent(HudRoot.ToSharedRef(), 40);

	// The playback bar takes the keys and the mouse; the spectator the recorder was takes no input (as Godot's
	// Controls.Enabled = false), so neither the game's bindings nor the in-match menu see them.
	Player->SetShowMouseCursor(true);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Hud);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(Mode);
	Viewport->SetIgnoreInput(false);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Hud, EFocusCause::SetDirectly);
	}
}

void UScctDemoPlayback::HideHud()
{
	if (HudRoot.IsValid() && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HudRoot.ToSharedRef());
	}
	HudRoot.Reset();
	Hud.Reset();
}

void UScctDemoPlayback::Leave()
{
	if (bLeaving)
	{
		return;
	}
	bLeaving = true;
	if (const FScctDemoPlayApi* Table = Api())
	{
		Table->Stop();
	}
	HideHud();
	bReturnToDemos = true;
	UWorld* World = GetGameInstance()->GetWorld();
	UE_LOG(LogOpenSCCT, Log, TEXT("Demo: stopped watching, back to the Demos page"));
	if (World != nullptr && GEngine != nullptr)
	{
		GEngine->SetClientTravel(World, UScctOnlineSubsystem::FrontEndLevel, TRAVEL_Absolute);
	}
}

void UScctDemoPlayback::SetXRay(bool bOn)
{
	if (bXRay != bOn)
	{
		bXRay = bOn;
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: X-ray turned %s"), bOn ? TEXT("on") : TEXT("off"));
	}
}

bool UScctDemoPlayback::TakeReturnToDemos()
{
	const bool bReturn = bReturnToDemos;
	bReturnToDemos = false;
	return bReturn;
}

void UScctDemoPlayback::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World == nullptr || World != PresentingWorld.Get())
	{
		return;
	}

	// The level that presented the playback is going (its own travel, or the game's end): the playback goes with it.
	if (UScctSessionSubsystem* Session = PresentingSession.Get())
	{
		Session->OnBeforeTicks.Remove(BeforeTicksHandle);
	}
	XRay.Release();
	if (!bLeaving)
	{
		if (const FScctDemoPlayApi* Table = Api())
		{
			Table->Stop();
		}
	}
	HideHud();
	PresentingWorld.Reset();
	PresentingSession.Reset();
	BeforeTicksHandle.Reset();
	bLeaving = false;
}
