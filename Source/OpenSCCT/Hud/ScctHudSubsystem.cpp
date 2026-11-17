#include "Hud/ScctHudSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Hud/ScctChatSubsystem.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudWidget.h"
#include "Hud/ScctTeamPingSubsystem.h"
#include "Hud/ScctScreenEffectsSubsystem.h"
#include "Hud/ScctVisionSubsystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
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
	Collection.InitializeDependency<UScctScreenEffectsSubsystem>();
	if (Session == nullptr)
	{
		return;
	}
	Session->OnSessionChanged.AddUObject(this, &UScctHudSubsystem::HandleSessionChanged);
	Session->OnMatchStopped.AddUObject(this, &UScctHudSubsystem::HandleMatchStopped);
	Session->OnFrame.AddUObject(this, &UScctHudSubsystem::HandleFrame);

	FParse::Value(FCommandLine::Get(), TEXT("ScctHudLog="), LogEvery);

	InitNames();
	InitGadgetViews();
	InitIcons();
	InitRadar();
	InitBackpack();
	InitBinoculars();

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
	if (UScctSessionSubsystem* Session = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UScctSessionSubsystem>() : nullptr)
	{
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnMatchStopped.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	if (CommandSource.IsValid())
	{
		CommandSource->OnLocalCommand.Remove(CommandHandle);
	}
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
	bScoreboardKeyHeld = false;
	bScoreboardToggled = false;
	for (FTimedCommand& Command : TimedCommands)
	{
		Command.bDone = false;
	}

	// A status page of the match before goes with it, and the keys with it.
	if (StatusCloseKeys.Num() > 0)
	{
		SetStatusPage(false);
	}
	GadgetMenuCheck.Stage = 0;
	GadgetViewCheck.Stage = 0;
	GadgetMenuCheck.NextStep = 0;
	LastGadgetView = 0;
	bLastCamAlert = false;
	LastMenuSignature = 0;
	GadgetKeysAge = 0.0f;
	bTextureChecksDone = false;
	LastCoopIcon = 0;
	LastRespawnTile = FIntPoint(-1, -1);
	RespawnKeyAge = -100.0f;
	LastHeartCone = 0;
	LastBackpackSignature = 0;
	bRadarTextureMissingLogged = false;
	BinocularCheck.Stage = 0;
	BinocularCheck.NextZoom = 0;
	BinocularCheck.NextUse = 0;
	BinocularCheck.bUseHeld = false;
	BinocularAge = 0.0f;
	SpeakAge = -1.0f;
	BinocularKeysAge = -100.0f;
	LastBinocularMode = 0;
	LastZoomStep = 0;
	LastBinocularTargetCount = -1;
	LastLockCandidate.Reset();
	bLastLockMarked = false;
	bBinocularPageMissingLogged = false;
	LastMotionSignature = 0;
	bMotionTableMissingLogged = false;
	AimMateUntil = -1.0f;
	if (NewSessionId != 0 && Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("HUD: the bridge has no HUD table (its reason is in the log above); the match plays without a HUD and without goggles"));
	}
}

void UScctHudSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	// The bridge sends the release of a held command when the keys are taken away (a menu opens), so a held
	// scoreboard never sticks.
	if (Name == TEXT("HoldScoreboard"))
	{
		bScoreboardKeyHeld = bPressed;
	}
	else if (bPressed && Name == TEXT("ShowConnectedPlayers"))
	{
		bScoreboardToggled = !bScoreboardToggled;
	}
	else if (bPressed && Name == TEXT("SwitchEnhancedReality") && Api != nullptr)
	{
		Api->SetOption(EScctHudOption::SwitchEnhancedReality, 0);
	}
	else if (bPressed && Name == TEXT("DisplaySessionStatus"))
	{
		SetStatusPage(true);
	}
	else if (bPressed && Name == TEXT("ToggleTeammateNames"))
	{
		ToggleTeammateNames();
	}
	else if (bPressed && Name == TEXT("DisplayStrategicMap") && !bStrategicMapNoted)
	{
		bStrategicMapNoted = true;
		UE_LOG(LogOpenSCCT, Warning, TEXT("HUD: DisplayStrategicMap is not served: the bridge hands over no plan of the map (its picture, scale and the live positions on it)"));
	}
}

void UScctHudSubsystem::SetStatusPage(bool bOpen)
{
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (bOpen)
	{
		if (Player == nullptr || Model->bStatusPage || Player->IsMenuOpen() || !Model->bHasRound)
		{
			return;
		}
		// The page closes on the key that opened it, on Escape and on Enter (the original's exit keys). The
		// controls raise no commands while the keys are held back, so those keys are read from Unreal directly.
		StatusCloseKeys = Player->FindKeysBoundTo(TEXT("DisplaySessionStatus"));
		StatusCloseKeys.AddUnique(EKeys::Escape);
		StatusCloseKeys.AddUnique(EKeys::Enter);
		bStatusKeysArmed = false;
		Model->bStatusPage = true;
		Player->SetMenuOpen(true);
		UE_LOG(LogOpenSCCT, Log, TEXT("HUD: mission status page opened"));
		return;
	}

	StatusCloseKeys.Reset();
	if (Model->bStatusPage)
	{
		Model->bStatusPage = false;
		UE_LOG(LogOpenSCCT, Log, TEXT("HUD: mission status page closed"));
		GiveInputToGame();
	}
}

void UScctHudSubsystem::GiveInputToGame()
{
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player == nullptr)
	{
		return;
	}
	Player->SetMenuOpen(false);
	if (!FApp::IsUnattended())
	{
		// The same as when the in-match menu closes (AScctGameMode::GiveInputToGame): the game has the keys and
		// captures the mouse again.
		Player->SetInputMode(FInputModeGameOnly());
	}
	Player->SetShowMouseCursor(false);
}

void UScctHudSubsystem::HandleMatchStopped()
{
	HideWidget();
	if (UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>())
	{
		Vision->Clear();
	}
	if (UScctScreenEffectsSubsystem* ScreenEffects = GetWorld()->GetSubsystem<UScctScreenEffectsSubsystem>())
	{
		ScreenEffects->Clear();
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
		// Nothing of the HUD takes the mouse or the keyboard (the chat draws its own composer).
		Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
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
	BuildMotion();
	ProjectNames();
	RunTimedCommands();
	RunGadgetChecks(DeltaSeconds);
	RunBinocularCheck();

	// The scoreboard follows the player's bindings, which arrive as interface commands.
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player != nullptr && CommandSource.Get() != Player)
	{
		CommandSource = Player;
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctHudSubsystem::HandleLocalCommand);
	}
	Model->bScoreboardHeld = bScoreboardForced || bScoreboardKeyHeld || bScoreboardToggled;
	// The chat's composer (UScctChatSubsystem) open: the log above it stays readable.
	const UScctChatSubsystem* Chat = GetWorld()->GetSubsystem<UScctChatSubsystem>();
	Model->bChatOpen = Chat != nullptr && Chat->IsOpen();

	// The status page's exit keys. The press that opened the page must not close it: a key counts once every
	// exit key has been up since it opened.
	if (Model->bStatusPage && Player != nullptr)
	{
		bool bAnyDown = false;
		bool bAnyPressed = false;
		for (const FKey& Key : StatusCloseKeys)
		{
			bAnyDown |= Player->IsInputKeyDown(Key);
			bAnyPressed |= Player->WasInputKeyJustPressed(Key);
		}
		if (bStatusKeysArmed && bAnyPressed)
		{
			SetStatusPage(false);
		}
		else if (!bAnyDown && !bAnyPressed)
		{
			bStatusKeysArmed = true;
		}
	}
	Model->Advance(DeltaSeconds);
	ReadIcons(DeltaSeconds);
	ReadBinoculars(DeltaSeconds);

	if (UScctVisionSubsystem* Vision = GetWorld()->GetSubsystem<UScctVisionSubsystem>())
	{
		Vision->Present(Model->bHasState ? Model->State.VisionMode : 0, DeltaSeconds);
	}
	if (UScctScreenEffectsSubsystem* ScreenEffects = GetWorld()->GetSubsystem<UScctScreenEffectsSubsystem>())
	{
		ScreenEffects->Present(*Model, DeltaSeconds);
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
	ReadRows(M.MenuLines, 8, Api->GetMenu);
	ReadRows(M.Objectives, 48, Api->GetObjectives);
	ReadRows(M.ObjectiveLines, 16, Api->GetObjectiveLines);
	ReadRows(M.Scores, 32, Api->GetScoreboard);
	ReadRows(M.Radar, 64, Api->GetRadar);
	ReadRows(M.BinocularTargets, 16, Api->GetBinocularTargets);
	ReadGadgetViews();
	ReadRadar();
	ReadBackpack();

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
			OnMessage.Broadcast(Message);
			M.AddMessage(Message);
		}
	} while (Count == UE_ARRAY_COUNT(Messages));
}

void UScctHudSubsystem::ProjectMarkers()
{
	FScctHudModel& M = *Model;
	M.Markers.Reset();
	M.BinocularMarkers.Reset();
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
	ProjectBinocularTargets();
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
			// The fire key goes down or comes up, as under a finger.
			if (AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController()))
			{
				Player->HoldScriptedAction(TEXT("fire"), Command.Argument != 0);
			}
		}
		else if (Command.Name.Equals(TEXT("PreviewDamage"), ESearchCase::IgnoreCase))
		{
			Model->PreviewDamage();
		}
		else if (Command.Name.Equals(TEXT("PreviewEnd"), ESearchCase::IgnoreCase))
		{
			Model->PreviewWinner = FMath::Clamp(Command.Argument, -1, 1);
		}
		else if (Command.Name.Equals(TEXT("PreviewCamAlert"), ESearchCase::IgnoreCase))
		{
			Model->bPreviewCamAlert = Command.Argument != 0;
		}
		else if (Command.Name.Equals(TEXT("EnhancedReality"), ESearchCase::IgnoreCase))
		{
			Api->SetOption(EScctHudOption::SwitchEnhancedReality, 0);
		}
		else if (Command.Name.Equals(TEXT("StatusPage"), ESearchCase::IgnoreCase))
		{
			// The page as the key opens it (1) and closes it (0).
			SetStatusPage(Command.Argument != 0);
		}
		else if (Command.Name.Equals(TEXT("TeammateNames"), ESearchCase::IgnoreCase))
		{
			// For this run only: the user's own setting is not written.
			TeammateNamesForced[FMath::Clamp(Model->LocalTeam, 0, 1)] = Command.Argument != 0 ? 1 : 0;
		}
		else if (RunIconCommand(Command.Name, Command.Argument) || RunRadarCommand(Command.Name, Command.Argument)
			|| RunBackpackCommand(Command.Name, Command.Argument) || RunMotionCommand(Command.Name, Command.Argument)
			|| (GetWorld()->GetSubsystem<UScctTeamPingSubsystem>() != nullptr && GetWorld()->GetSubsystem<UScctTeamPingSubsystem>()->RunCommand(Command.Name, Command.Argument)))
		{
		}
		else if (AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			// Anything else is a command of the bindings: its key is pressed. A binding that carries a number
			// ("SwitchHeadset 1") is looked for with it first, then by its name alone ("ShowMenu").
			if (Player->FindKeysBoundTo(FString::Printf(TEXT("%s %d"), *Command.Name, Command.Argument)).Num() > 0)
			{
				Player->TapBoundCommand(FString::Printf(TEXT("%s %d"), *Command.Name, Command.Argument));
			}
			else
			{
				Player->TapBoundCommand(Command.Name);
			}
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
		TEXT("HUD t=%.1f valid %d side %d flags 0x%x fx 0x%x health %d lives %d energy %.0f/%d vision %d | weapon '%s' clip %d ammo %d mode '%s' spread %.2f | gadgets %d selected %d '%s' | prompt '%s' location '%s' | round valid %d mode %d left %d s team lives %d:%d score %d:%d objectives left %d of %d | %d rows, %d objective lines, %d markers, feed %d headlines %d log %d | hits %d | watching '%s' respawn in %d none left %d"),
		S.TimeSeconds, M.bHasState ? 1 : 0, S.Side, S.Flags, S.EffectFlags, S.Health, S.Lives, S.EnergyLevel, S.EnergyLevelMax, S.VisionMode,
		*M.Text(S.WeaponNameId), S.ClipAmmo, S.Ammo, *M.Text(S.FireModeNameId), S.Accuracy,
		M.Gadgets.Num(), S.SelectedGadget, *Selected,
		*M.Text(S.UsePromptId), *M.Text(S.LocationNameId),
		M.bHasRound ? 1 : 0, R.GameMode, R.RemainingSeconds, R.TeamLives[0], R.TeamLives[1], R.TeamScore[0], R.TeamScore[1], R.ObjectivesLeft, R.NumObjectives,
		M.Scores.Num(), M.ObjectiveLines.Num(), M.Markers.Num(), M.Feed.Num(), M.Headlines.Num(), M.Log.Num(),
		S.DamageCount, *M.Text(R.WatchedNameId), R.RespawnIn, R.NoRespawn);
	LogRadar();
	LogBackpack();
	LogBinoculars(false);
	LogMotion(false);

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
