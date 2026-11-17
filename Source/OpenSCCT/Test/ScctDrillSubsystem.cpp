#include "Test/ScctDrillSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Test/ScctGrabAreaSubsystem.h"

namespace
{
	/** Above the in-match menu (added at 30, which UMG puts at 40), so the coach's buttons can be clicked while it is open. */
	constexpr int32 CoachZOrder = 41;

	constexpr int32 MaxAttempts = 8;

	/** The level every drill plays on (Tools/Test/Import-Fixtures.ps1 imports it). */
	const TCHAR* const DrillMap = TEXT("TestRange");

	const FScctDrillInfo Drills[] =
	{
		{ TEXT("vision-cancelling"), TEXT("Vision cancelling"), TEXT("Walk off the tower and switch vision at touchdown while moving to cut the landing recovery short.") },
		{ TEXT("spy-grab"), TEXT("Mercenary grabbing"), TEXT("Walk up behind a mercenary who cannot fight back and grab him; the grab area shows where it works.") },
	};

	bool IsSet(float Value)
	{
		return !FMath::IsNaN(Value);
	}

	UWorld* DrillGameWorld(UWorld* World)
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

	void DrillCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (Args.Num() != 1 || !UScctDrillSubsystem::IsKind(Args[0]))
		{
			Output.Log(TEXT("usage: scct.Drill vision-cancelling|spy-grab"));
			return;
		}
		UWorld* Game = DrillGameWorld(World);
		if (Game == nullptr || !UScctDrillSubsystem::Launch(Game, Args[0]))
		{
			Output.Log(TEXT("drill: not in a game"));
			return;
		}
		Output.Logf(TEXT("drill: opening %s"), *UScctDrillSubsystem::LaunchURL(Args[0]));
	}

	void DrillActionCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		UWorld* Game = DrillGameWorld(World);
		UScctDrillSubsystem* Drill = Game != nullptr ? Game->GetSubsystem<UScctDrillSubsystem>() : nullptr;
		if (Args.Num() != 1)
		{
			Output.Log(TEXT("usage: scct.DrillAction retry|moving|grabarea|reset|forward|back|stop|night|thermal|grab|debug"));
			return;
		}
		Output.Log(Drill != nullptr ? Drill->Action(Args[0]) : FString(TEXT("drill: not in a game")));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice DrillConsoleCommand(
		TEXT("scct.Drill"),
		TEXT("scct.Drill vision-cancelling|spy-grab - open a TestRange drill with its coach (from the front end or a match)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DrillCommand));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice DrillActionConsoleCommand(
		TEXT("scct.DrillAction"),
		TEXT("scct.DrillAction retry|moving|grabarea|... - the drill coach's buttons: back to the ledge or start again, ")
		TEXT("the target walking or stopped, the grab area on or off, or one of the lesson's own actions."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DrillActionCommand));
}

UScctDrillSubsystem::UScctDrillSubsystem()
	: Model(MakeShared<FScctDrillCoachModel>())
{
}

TConstArrayView<FScctDrillInfo> UScctDrillSubsystem::GetDrills()
{
	return MakeArrayView(Drills);
}

bool UScctDrillSubsystem::IsKind(const FString& Kind)
{
	for (const FScctDrillInfo& Drill : Drills)
	{
		if (Kind == Drill.Kind)
		{
			return true;
		}
	}
	return false;
}

FString UScctDrillSubsystem::LaunchURL(const FString& Kind, const FString& PlayerName)
{
	// AScctGameMode reads Side, Bots and Name (the bridge makes the start the drill's in any case); this subsystem
	// reads Drill. A name with the URL's own separator would cut the options.
	FString Url = FString::Printf(TEXT("/Game/Imported/Maps/%s/%s?Drill=%s?Side=spy?Bots=%d"), DrillMap, DrillMap, *Kind,
		Kind == TEXT("spy-grab") ? 1 : 0);
	const FString Name = PlayerName.Replace(TEXT("?"), TEXT(""));
	if (!Name.IsEmpty())
	{
		Url += TEXT("?Name=") + Name;
	}
	return Url;
}

bool UScctDrillSubsystem::Launch(const UObject* WorldContext, const FString& Kind, const FString& PlayerName)
{
	if (!IsKind(Kind))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Drill: there is no drill '%s' (vision-cancelling, spy-grab)"), *Kind);
		return false;
	}
	UWorld* World = WorldContext != nullptr ? WorldContext->GetWorld() : nullptr;
	if (World == nullptr || GEngine == nullptr)
	{
		return false;
	}
	const FString Url = LaunchURL(Kind, PlayerName);
	UE_LOG(LogOpenSCCT, Log, TEXT("Drill: opening %s"), *Url);
	GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
	return true;
}

bool UScctDrillSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctDrillSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Session != nullptr)
	{
		Session->OnMatchStarted.AddUObject(this, &UScctDrillSubsystem::HandleMatchStarted);
		Session->OnMatchStopped.AddUObject(this, &UScctDrillSubsystem::HandleMatchStopped);
		Session->OnFrame.AddUObject(this, &UScctDrillSubsystem::HandleFrame);
	}

	// -ScctDrillActions="8 retry;9 moving"
	FString Script;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctDrillActions="), Script, false))
	{
		TArray<FString> Entries;
		Script.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			FString Seconds, Name;
			if (Entry.TrimStartAndEnd().Split(TEXT(" "), &Seconds, &Name))
			{
				ScriptedActions.Emplace(FCString::Atof(*Seconds), Name.TrimStartAndEnd());
			}
		}
		ScriptedActions.Sort([](const TPair<float, FString>& A, const TPair<float, FString>& B) { return A.Key < B.Key; });
	}
}

void UScctDrillSubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Session = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UScctSessionSubsystem>() : nullptr)
	{
		Session->OnMatchStarted.RemoveAll(this);
		Session->OnMatchStopped.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	HideCoach();
	Super::Deinitialize();
}

void UScctDrillSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// The world's subsystems begin play just before the game mode starts its match, so the bridge is armed in time.
	const AGameModeBase* GameMode = InWorld.GetAuthGameMode();
	const FString Wanted = GameMode != nullptr ? UGameplayStatics::ParseOption(GameMode->OptionsString, TEXT("Drill")) : FString();
	if (Wanted.IsEmpty())
	{
		return;
	}
	if (!IsKind(Wanted))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Drill: there is no drill '%s' (vision-cancelling, spy-grab); playing the level as it is"), *Wanted);
		return;
	}
	FString Level = FPackageName::GetShortName(InWorld.GetMapName());
	Level.RemoveFromStart(InWorld.StreamingLevelsPrefix);
	if (!Level.Equals(DrillMap, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Drill: %s runs on %s, not %s; playing the level as it is"), *Wanted, DrillMap, *Level);
		return;
	}

	FString Error;
	Api = ScctFeature<FScctLessonApi>(&Error);
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Drill: the bridge has no lesson table: %s"), *Error);
		return;
	}
	if (Api->Arm(*Wanted) == 0)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Drill: the bridge refused %s"), *Wanted);
		return;
	}
	Kind = Wanted;
	UE_LOG(LogOpenSCCT, Log, TEXT("Drill: %s armed for this level's match"), *Kind);
}

void UScctDrillSubsystem::HandleMatchStarted()
{
	if (Kind.IsEmpty() || Api == nullptr || bLessonStarted)
	{
		return;
	}

	if (Api->StartLesson(*Kind) == 0)
	{
		TCHAR Text[1024] = {};
		Api->LastError(Text, UE_ARRAY_COUNT(Text));
		UE_LOG(LogOpenSCCT, Error, TEXT("Drill: %s could not start: %s"), *Kind, Text);
		return;
	}

	bLessonStarted = true;
	bGrabAreaAsked = false;
	LoggedAttempt = 0;
	LoggedGrabs = 0;
	LoggedFeedback.Reset();
	ReadySeconds = -1.0f;
	UE_LOG(LogOpenSCCT, Log, TEXT("Drill: %s started"), *Kind);

	const bool bSpyGrab = Kind == TEXT("spy-grab");
	Model->Title = bSpyGrab ? TEXT("MERCENARY GRABBING") : TEXT("VISION CANCELLING");
	Model->bSpyGrab = bSpyGrab;
	Model->RetryLabel = bSpyGrab ? TEXT("Reset positions") : TEXT("Return to ledge");
	Model->Feedback = TEXT("Preparing practice...");
	TWeakObjectPtr<UScctDrillSubsystem> Self(this);
	Model->OnRetry = [Self]() { if (Self.IsValid()) { UE_LOG(LogOpenSCCT, Log, TEXT("Drill: button: %s"), *Self->Action(TEXT("retry"))); } };
	Model->OnMoving = [Self]() { if (Self.IsValid()) { UE_LOG(LogOpenSCCT, Log, TEXT("Drill: button: %s"), *Self->Action(TEXT("moving"))); } };
	Model->OnGrabArea = [Self]() { if (Self.IsValid()) { UE_LOG(LogOpenSCCT, Log, TEXT("Drill: button: %s"), *Self->Action(TEXT("grabarea"))); } };
	ShowCoach();
}

void UScctDrillSubsystem::HandleMatchStopped()
{
	// The lesson belongs to its match, as the Godot coach does to its play scene; the bridge ends it with the session.
	if (bLessonStarted)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Drill: %s ended with the match"), *Kind);
	}
	bLessonStarted = false;
	HideCoach();
}

FString UScctDrillSubsystem::Action(const FString& Name)
{
	if (!bLessonStarted || Api == nullptr)
	{
		return TEXT("drill: no drill runs here (scct.Drill vision-cancelling|spy-grab starts one)");
	}

	const bool bSpyGrab = Kind == TEXT("spy-grab");
	if (Name.Equals(TEXT("retry"), ESearchCase::IgnoreCase))
	{
		if (bSpyGrab)
		{
			// As the Godot coach: the whole drill again, both players back where they started.
			return Launch(this, Kind) ? TEXT("drill: starting spy-grab again") : TEXT("drill: could not restart");
		}
		return Api->Action(TEXT("reset")) != 0 ? TEXT("drill: back to the ledge") : TEXT("drill: not ready yet");
	}
	if (Name.Equals(TEXT("grabarea"), ESearchCase::IgnoreCase))
	{
		UScctGrabAreaSubsystem* GrabArea = GetWorld()->GetSubsystem<UScctGrabAreaSubsystem>();
		return GrabArea != nullptr ? GrabArea->Request(-1) : FString(TEXT("drill: no grab-area overlay"));
	}

	const FString Lower = Name.ToLower();
	if (Api->Action(*Lower) == 0)
	{
		return FString::Printf(TEXT("drill: the lesson did not take '%s'"), *Name);
	}
	return FString::Printf(TEXT("drill: %s"), *Lower);
}

bool UScctDrillSubsystem::IsGrabAreaShown() const
{
	const UScctGrabAreaSubsystem* GrabArea = GetWorld()->GetSubsystem<UScctGrabAreaSubsystem>();
	return GrabArea != nullptr && GrabArea->IsShown();
}

void UScctDrillSubsystem::HandleFrame(float DeltaSeconds)
{
	if (!bLessonStarted || Api == nullptr)
	{
		return;
	}

	FScctLessonState State;
	if (Api->GetState(&State) == 0)
	{
		return;
	}

	if (State.Ready != 0)
	{
		if (ReadySeconds < 0.0f)
		{
			ReadySeconds = 0.0f;
			UE_LOG(LogOpenSCCT, Log, TEXT("Drill: %s ready: %s"), *Kind, State.Feedback);
		}
		else
		{
			ReadySeconds += DeltaSeconds;
		}

		if (State.Kind == 1)
		{
			// The Godot coach shows the grab area as soon as the players are placed, and keeps the lesson's debug
			// flag with it.
			if (!bGrabAreaAsked)
			{
				bGrabAreaAsked = true;
				if (UScctGrabAreaSubsystem* GrabArea = GetWorld()->GetSubsystem<UScctGrabAreaSubsystem>())
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Drill: spy-grab: %s"), *GrabArea->Request(1));
				}
			}
			if ((State.Debug != 0) != IsGrabAreaShown())
			{
				Api->Action(TEXT("debug"));
			}
		}
		RunScriptedActions(DeltaSeconds);
	}

	LogProgress(State);
	UpdateModel(State);
	ProjectPortal(State);
}

void UScctDrillSubsystem::RunScriptedActions(float DeltaSeconds)
{
	while (ScriptedActions.Num() > 0 && ReadySeconds >= ScriptedActions[0].Key)
	{
		const FString Name = ScriptedActions[0].Value;
		ScriptedActions.RemoveAt(0);
		UE_LOG(LogOpenSCCT, Log, TEXT("Drill: -ScctDrillActions %s: %s"), *Name, *Action(Name));
		if (!bLessonStarted)
		{
			return;
		}
	}
}

void UScctDrillSubsystem::LogProgress(const FScctLessonState& State)
{
	const FString Feedback(State.Feedback);
	if (Feedback != LoggedFeedback)
	{
		LoggedFeedback = Feedback;
		UE_LOG(LogOpenSCCT, Log, TEXT("Drill coach: %s"), *Feedback);
	}

	if (State.Kind == 1)
	{
		if (State.Grabs > LoggedGrabs)
		{
			LoggedGrabs = State.Grabs;
			UE_LOG(LogOpenSCCT, Log, TEXT("Drill spy-grab: grab %d (distance %.0f cm, target %s)"), State.Grabs,
				IsSet(State.Distance) ? State.Distance : -1.0f, State.Moving != 0 ? TEXT("walking") : TEXT("stopped"));
		}
		return;
	}

	FScctLessonAttempt Attempts[MaxAttempts];
	const int32 Count = FMath::Clamp(Api->GetAttempts(Attempts, MaxAttempts), 0, MaxAttempts);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctLessonAttempt& Attempt = Attempts[Index];
		if (Attempt.Number <= LoggedAttempt)
		{
			continue;
		}
		LoggedAttempt = Attempt.Number;
		const FString Switch = IsSet(Attempt.SwitchMs) ? FString::Printf(TEXT("switch %+.0f ms after touchdown"), Attempt.SwitchMs) : FString(TEXT("no switch"));
		const FString Saved = IsSet(Attempt.SavedMs) ? FString::Printf(TEXT(", saved %.0f ms"), Attempt.SavedMs) : FString();
		UE_LOG(LogOpenSCCT, Log, TEXT("Drill vision-cancelling attempt: recovery %.3f s (#%d %s, %s%s, %s)"),
			Attempt.RecoveryMs / 1000.0f, Attempt.Number, Attempt.Result, *Switch, *Saved,
			Attempt.Moving != 0 ? TEXT("kept moving") : TEXT("stopped on landing"));
	}
}

void UScctDrillSubsystem::UpdateModel(const FScctLessonState& State)
{
	Model->Feedback = State.Feedback;
	const AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	Model->bButtonsLive = Player != nullptr && Player->IsMenuOpen();

	if (State.Kind == 1)
	{
		Model->MovingLabel = State.Moving != 0 ? TEXT("Target: walking (click to stop)") : TEXT("Target: stopped (click to walk)");
		Model->GrabAreaLabel = IsGrabAreaShown() ? TEXT("Hide grab area") : TEXT("Show grab area");
		FString Results = FString::Printf(TEXT("Successful grabs: %d"), State.Grabs);
		if (IsSet(State.Distance))
		{
			Results += FString::Printf(TEXT("  -  Distance: %.0f units"), State.Distance);
		}
		Model->Results = Results + TEXT("\nThe target cannot attack or kill you. Use Interact or your Grab only binding from behind.");
		return;
	}

	FString Results = IsSet(State.BaselineMs) ? FString::Printf(TEXT("Baseline %.0f ms"), State.BaselineMs)
		: FString(TEXT("First drop: measure a baseline without switching"));
	if (IsSet(State.BestMs))
	{
		Results += FString::Printf(TEXT("  -  Best %.0f ms"), State.BestMs);
	}
	FScctLessonAttempt Attempts[MaxAttempts];
	const int32 Count = FMath::Clamp(Api->GetAttempts(Attempts, MaxAttempts), 0, MaxAttempts);
	for (int32 Index = FMath::Max(0, Count - 3); Index < Count; ++Index)
	{
		const FScctLessonAttempt& Attempt = Attempts[Index];
		Results += FString::Printf(TEXT("\n#%d %s: %.0f ms"), Attempt.Number, Attempt.Result, Attempt.RecoveryMs);
		if (IsSet(Attempt.SavedMs))
		{
			Results += FString::Printf(TEXT("  -  saved %.0f ms"), Attempt.SavedMs);
		}
	}
	Model->Results = Results;
}

void UScctDrillSubsystem::ProjectPortal(const FScctLessonState& State)
{
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	Model->bPortal = Player != nullptr && State.Kind == 0 && State.Ready != 0;
	if (!Model->bPortal)
	{
		return;
	}

	// The Godot portal: a ring standing across the walkway, facing along X, a metre across, so the spy walks
	// through it; its label floats above.
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
	const FVector Centre(State.PortalX, State.PortalY, State.PortalZ);
	const auto Project = [Player, Scale](const FVector& World, FVector2f& Out)
	{
		FVector2D Screen;
		const bool bInFront = Player->ProjectWorldLocationToScreen(World, Screen, false);
		Out = FVector2f(Screen) / Scale;
		return bInFront;
	};
	for (int32 Index = 0; Index < FScctDrillCoachModel::RingPoints; ++Index)
	{
		const float Angle = 2.0f * PI * Index / (FScctDrillCoachModel::RingPoints - 1);
		const FVector Point = Centre + FVector(0.0f, FMath::Cos(Angle), FMath::Sin(Angle)) * State.PortalRadius;
		Model->bRingInFront[Index] = Project(Point, Model->Ring[Index]);
	}
	Model->bLabel = Project(Centre + FVector(0.0f, 0.0f, State.PortalRadius + 30.0f), Model->Label);
}

void UScctDrillSubsystem::ShowCoach()
{
	if (Coach.IsValid())
	{
		return;
	}
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (Viewport == nullptr)
	{
		return;
	}
	Coach = SNew(SScctDrillCoach).Model(Model);
	ViewportContent = Coach;
	Viewport->AddViewportWidgetContent(ViewportContent.ToSharedRef(), CoachZOrder);
}

void UScctDrillSubsystem::HideCoach()
{
	if (ViewportContent.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(ViewportContent.ToSharedRef());
		}
	}
	ViewportContent.Reset();
	Coach.Reset();
}
