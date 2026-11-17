#include "Hud/ScctHudSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudWidget.h"
#include "Hud/ScctVisionSubsystem.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** Reads one of the bridge's lists: up to Capacity rows, the array sized to what was written. */
	template <typename TRow>
	void ReadRows(TArray<TRow>& Rows, int32 Capacity, int32 (__cdecl* Getter)(TRow*, int32))
	{
		Rows.SetNumUninitialized(Capacity, EAllowShrinking::No);
		const int32 Count = Getter(Rows.GetData(), Capacity);
		Rows.SetNum(FMath::Clamp(Count, 0, Capacity), EAllowShrinking::No);
	}
}

UScctHudSubsystem::UScctHudSubsystem()
	: Model(MakeShared<FScctHudModel>())
{
}

bool UScctHudSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctHudSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	Collection.InitializeDependency<UScctVisionSubsystem>();
	if (Session == nullptr)
	{
		return;
	}
	Session->OnSessionChanged.AddUObject(this, &UScctHudSubsystem::HandleSessionChanged);
	Session->OnMatchStopped.AddUObject(this, &UScctHudSubsystem::HandleMatchStopped);
	Session->OnFrame.AddUObject(this, &UScctHudSubsystem::HandleFrame);
	Session->OnBeforeTicks.AddUObject(this, &UScctHudSubsystem::HandleBeforeTicks);

	FParse::Value(FCommandLine::Get(), TEXT("ScctHudLog="), LogEvery);

	// -ScctHudCommands="3 SwitchHeadset 1;9 Scoreboard 1"
	FString Commands;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctHudCommands="), Commands, false))
	{
		TArray<FString> Entries;
		Commands.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			TArray<FString> Parts;
			Entry.ParseIntoArrayWS(Parts);
			if (Parts.Num() >= 2)
			{
				FTimedCommand& Command = TimedCommands.AddDefaulted_GetRef();
				Command.Time = FCString::Atof(*Parts[0]);
				Command.Name = Parts[1];
				Command.Argument = Parts.Num() >= 3 ? FCString::Atoi(*Parts[2]) : 0;
			}
		}
	}
}

void UScctHudSubsystem::Deinitialize()
{
	HideWidget();
	Super::Deinitialize();
}

void UScctHudSubsystem::HandleSessionChanged(int32 OldSessionId, int32 NewSessionId)
{
	// Text ids, messages and everything in motion belong to one session.
	Api = NewSessionId != 0 ? ScctFeature<FScctHudApi>() : nullptr;
	Model->Reset(Api);
	bSnapshotRead = false;
	ViewportSize = FIntPoint::ZeroValue;
	LogTimer = 0.0f;
	bScoreboardForced = false;
	bFireForced = false;
	for (FTimedCommand& Command : TimedCommands)
	{
		Command.bDone = false;
	}
	if (NewSessionId != 0 && Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("HUD: the bridge has no HUD table; nothing will be drawn"));
	}
}

void UScctHudSubsystem::HandleBeforeTicks(float DeltaSeconds)
{
	// -ScctHudCommands "Fire 1": the trigger is held for the simulation in place of the player's own input.
	if (bFireForced)
	{
		if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
		{
			FScctBridgeInput Input = {};
			Input.Buttons[static_cast<int32>(EScctButton::Fire)] = 1;
			Session->SetInput(Input);
		}
	}
}

void UScctHudSubsystem::HandleMatchStopped()
{
	HideWidget();
	if (UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>())
	{
		Vision->Clear();
	}
}

void UScctHudSubsystem::ShowWidget()
{
	if (IsValid(Widget))
	{
		return;
	}
	// The widget needs the player's viewport, which is there by the first frame of a match.
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || !Player->IsLocalController())
	{
		return;
	}
	Widget = CreateWidget<UScctHudWidget>(Player, UScctHudWidget::StaticClass());
	if (IsValid(Widget))
	{
		Widget->SetModel(Model);
		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		Widget->AddToViewport(10);
		UE_LOG(LogOpenSCCT, Log, TEXT("HUD: widget added to the viewport"));
	}
}

void UScctHudSubsystem::HideWidget()
{
	if (IsValid(Widget))
	{
		Widget->RemoveFromParent();
	}
	Widget = nullptr;
}

void UScctHudSubsystem::HandleFrame(float DeltaSeconds)
{
	if (Api == nullptr)
	{
		return;
	}
	ShowWidget();

	const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();

	// The merc's aim point depends on the picture's shape.
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		FVector2D Size;
		Viewport->GetViewportSize(Size);
		const FIntPoint NewSize(FMath::RoundToInt32(Size.X), FMath::RoundToInt32(Size.Y));
		if (NewSize != ViewportSize && NewSize.X > 0 && NewSize.Y > 0)
		{
			ViewportSize = NewSize;
			Api->SetViewport(NewSize.X, NewSize.Y);
		}
	}

	// The HUD data changes only on a simulation tick.
	if (!bSnapshotRead || (Session != nullptr && Session->GetTicksThisFrame() > 0))
	{
		ReadSnapshot();
		bSnapshotRead = true;
	}
	ProjectMarkers();
	RunTimedCommands();

	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	Model->bScoreboardHeld = bScoreboardForced || (Player != nullptr && Player->IsInputKeyDown(EKeys::Tab));
	Model->Advance(DeltaSeconds);

	if (UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>())
	{
		Vision->Present(Model->bHasState ? Model->State.VisionMode : 0, DeltaSeconds);
	}
	LogSnapshot(DeltaSeconds);
}

void UScctHudSubsystem::ReadSnapshot()
{
	FScctHudModel& M = *Model;
	M.bHasState = Api->GetState(&M.State) != 0 && M.State.Valid != 0;
	M.bHasRound = Api->GetRound(&M.Round) != 0 && M.Round.Valid != 0;

	ReadRows(M.Gadgets, 16, Api->GetGadgets);
	ReadRows(M.Interactions, 8, Api->GetInteractions);
	ReadRows(M.Objectives, 48, Api->GetObjectives);
	ReadRows(M.ObjectiveLines, 16, Api->GetObjectiveLines);
	ReadRows(M.Scores, 32, Api->GetScoreboard);

	M.LocalTeam = M.State.Side;
	for (const FScctHudScoreRow& Row : M.Scores)
	{
		if ((Row.Flags & EScctHudScoreFlags::Local) != 0 && Row.Team >= 0 && Row.Team <= 1)
		{
			M.LocalTeam = Row.Team;
		}
	}

	// Everything new since the last read; a full buffer means there may be more.
	FScctHudMessage Messages[8];
	int32 Count = 0;
	do
	{
		Count = Api->DrainMessages(Messages, UE_ARRAY_COUNT(Messages));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FScctHudMessage& Message = Messages[Index];
			UE_LOG(LogOpenSCCT, Log, TEXT("HUD message t=%.1f category %d kind %d cause %d flags %d for %.1f s: \"%s\""),
				Message.Time, Message.Category, Message.Kind, Message.Cause, Message.Flags, Message.Duration, Message.Text);
			M.AddMessage(Message);
		}
	} while (Count == UE_ARRAY_COUNT(Messages));
}

void UScctHudSubsystem::ProjectMarkers()
{
	FScctHudModel& M = *Model;
	M.Markers.Reset();
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || !M.bHasState)
	{
		return;
	}

	// The widget's units are viewport pixels divided by the DPI scale.
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
	const FVector Eye(M.State.X, M.State.Y, M.State.Z);
	for (const FScctHudObjective& Objective : M.Objectives)
	{
		FScctHudMarker& Marker = M.Markers.AddDefaulted_GetRef();
		Marker.Objective = Objective;
		const FVector Location(Objective.X, Objective.Y, Objective.Z);
		Marker.Distance = static_cast<float>(FVector::Distance(Eye, Location));
		FVector2D Screen;
		Marker.bInFront = Player->ProjectWorldLocationToScreen(Location, Screen, false);
		Marker.Screen = FVector2f(Screen) / Scale;
	}
}

void UScctHudSubsystem::RunTimedCommands()
{
	for (FTimedCommand& Command : TimedCommands)
	{
		if (Command.bDone || Model->Clock < Command.Time)
		{
			continue;
		}
		Command.bDone = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: %s %d at %.1f s"), *Command.Name, Command.Argument, Model->Clock);
		if (Command.Name.Equals(TEXT("Scoreboard"), ESearchCase::IgnoreCase))
		{
			bScoreboardForced = Command.Argument != 0;
		}
		else if (Command.Name.Equals(TEXT("Fire"), ESearchCase::IgnoreCase))
		{
			// A shot is the simulation's Fire command with the button held after it.
			bFireForced = Command.Argument != 0;
			if (UScctSessionSubsystem* Session = bFireForced ? GetWorld()->GetSubsystem<UScctSessionSubsystem>() : nullptr)
			{
				Session->SendCommand(TEXT("Fire"), 0);
			}
		}
		else if (Command.Name.Equals(TEXT("EnhancedReality"), ESearchCase::IgnoreCase))
		{
			Api->SetOption(EScctHudOption::SwitchEnhancedReality, 0);
		}
		else if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
		{
			Session->SendCommand(*Command.Name, Command.Argument);
		}
	}
}

void UScctHudSubsystem::LogSnapshot(float DeltaSeconds)
{
	if (LogEvery <= 0.0f)
	{
		return;
	}
	LogTimer -= DeltaSeconds;
	if (LogTimer > 0.0f)
	{
		return;
	}
	LogTimer = LogEvery;

	// One line a second of what the HUD is drawing from, to check screenshots against.
	const FScctHudModel& M = *Model;
	const FScctHudState& S = M.State;
	const FScctHudRound& R = M.Round;
	const FString Selected = M.Gadgets.IsValidIndex(S.SelectedGadget) ? M.Text(M.Gadgets[S.SelectedGadget].NameId) : FString();
	UE_LOG(LogOpenSCCT, Log,
		TEXT("HUD t=%.1f valid %d side %d flags 0x%x fx 0x%x health %d lives %d energy %.0f/%d vision %d | weapon '%s' clip %d ammo %d mode '%s' spread %.2f | gadgets %d selected %d '%s' | prompt '%s' location '%s' | round valid %d mode %d left %d s team lives %d:%d score %d:%d objectives left %d of %d | %d rows, %d objective lines, %d markers, feed %d headlines %d log %d"),
		S.TimeSeconds, M.bHasState ? 1 : 0, S.Side, S.Flags, S.EffectFlags, S.Health, S.Lives, S.EnergyLevel, S.EnergyLevelMax, S.VisionMode,
		*M.Text(S.WeaponNameId), S.ClipAmmo, S.Ammo, *M.Text(S.FireModeNameId), S.Accuracy,
		M.Gadgets.Num(), S.SelectedGadget, *Selected,
		*M.Text(S.UsePromptId), *M.Text(S.LocationNameId),
		M.bHasRound ? 1 : 0, R.GameMode, R.RemainingSeconds, R.TeamLives[0], R.TeamLives[1], R.TeamScore[0], R.TeamScore[1], R.ObjectivesLeft, R.NumObjectives,
		M.Scores.Num(), M.ObjectiveLines.Num(), M.Markers.Num(), M.Feed.Num(), M.Headlines.Num(), M.Log.Num());

	// The scoreboard's rows while it is up (and once near the start), for the same check.
	constexpr float FirstRows = 2.0f;
	if (M.bScoreboardHeld || (M.Clock >= FirstRows && M.Clock < FirstRows + LogEvery))
	{
		for (const FScctHudScoreRow& Row : M.Scores)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("HUD score row: '%s' team %d flags 0x%x obj/kills %d kills %d deaths %d lives %d ping %d"),
				*M.Text(Row.NameId), Row.Team, Row.Flags, Row.ObjOrKills, Row.Kills, Row.Deaths, Row.Lives, Row.Ping);
		}
	}
}
