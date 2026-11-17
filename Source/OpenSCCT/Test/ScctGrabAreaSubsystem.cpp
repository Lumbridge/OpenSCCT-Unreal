#include "Test/ScctGrabAreaSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Test/SScctGrabAreaOverlay.h"

namespace
{
	/** Just below the HUD: UMG puts a widget added at z-order N at N + 10, and the HUD is added at 10. */
	constexpr int32 OverlayZOrder = 19;

	/** How long a refusal stays on the screen. */
	constexpr double NoticeSeconds = 4.0;

	constexpr int32 MaxAreas = 64;

	/** The world a console command without one means: the first game world that plays. */
	UWorld* GameWorld(UWorld* World)
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

	void GrabAreaCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		UWorld* Game = GameWorld(World);
		UScctGrabAreaSubsystem* Subsystem = Game != nullptr ? Game->GetSubsystem<UScctGrabAreaSubsystem>() : nullptr;
		if (Subsystem == nullptr)
		{
			Output.Log(TEXT("grabarea: not in a game"));
			return;
		}

		int32 Mode = -1;
		if (Args.Num() > 0)
		{
			const FString& Arg = Args[0];
			if (Arg == TEXT("1") || Arg.Equals(TEXT("on"), ESearchCase::IgnoreCase))
			{
				Mode = 1;
			}
			else if (Arg == TEXT("0") || Arg.Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				Mode = 0;
			}
			else
			{
				Output.Log(TEXT("usage: scct.GrabArea [0|1]"));
				return;
			}
		}
		Output.Log(Subsystem->Request(Mode));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GrabAreaConsoleCommand(
		TEXT("scct.GrabArea"),
		TEXT("scct.GrabArea [0|1] - Practice only: draw each mercenary's grab window and whether Use would grab him now. ")
		TEXT("Without an argument it toggles."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&GrabAreaCommand));

	/** Projects a world point for the player's camera into the overlay's units. False when it is behind the camera. */
	bool ProjectPoint(const APlayerController& Player, const FVector& World, float Scale, FVector2f& Out)
	{
		FVector2D Screen;
		const bool bInFront = Player.ProjectWorldLocationToScreen(World, Screen, false);
		Out = FVector2f(Screen) / Scale;
		return bInFront;
	}

	/** A sector of a circle on the floor around Centre, HalfDegrees either side of YawDegrees. */
	void ProjectSector(const APlayerController& Player, float Scale, const FVector& Centre, float YawDegrees, float HalfDegrees, float Radius, FScctGrabAreaSector& Sector)
	{
		Sector.bInFront[0] = ProjectPoint(Player, Centre, Scale, Sector.Points[0]);
		for (int32 Index = 0; Index <= FScctGrabAreaSector::Segments; ++Index)
		{
			const float Angle = -HalfDegrees + 2.0f * HalfDegrees * Index / FScctGrabAreaSector::Segments;
			const FVector Rim = Centre + FRotator(0.0f, YawDegrees + Angle, 0.0f).Vector() * Radius;
			Sector.bInFront[Index + 1] = ProjectPoint(Player, Rim, Scale, Sector.Points[Index + 1]);
		}
	}
}

UScctGrabAreaSubsystem::UScctGrabAreaSubsystem()
	: Model(MakeShared<FScctGrabAreaModel>())
{
}

bool UScctGrabAreaSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctGrabAreaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>();
	if (Session == nullptr)
	{
		return;
	}
	Session->OnSessionChanged.AddUObject(this, &UScctGrabAreaSubsystem::HandleSessionChanged);
	Session->OnMatchStopped.AddUObject(this, &UScctGrabAreaSubsystem::HandleMatchStopped);
	Session->OnFrame.AddUObject(this, &UScctGrabAreaSubsystem::HandleFrame);
	bAskAtStart = FParse::Param(FCommandLine::Get(), TEXT("ScctGrabArea"));
}

void UScctGrabAreaSubsystem::Deinitialize()
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

bool UScctGrabAreaSubsystem::IsShown() const
{
	return bWanted && Overlay.IsValid();
}

FString UScctGrabAreaSubsystem::Request(int32 Mode)
{
	const bool bOn = Mode < 0 ? !bWanted : Mode != 0;
	if (!bOn)
	{
		if (bWanted)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("GrabArea: hidden"));
		}
		bWanted = false;
		Model->bShown = false;
		Model->Areas.Reset();
		return TEXT("grabarea: off");
	}

	const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	if (Session == nullptr || !Session->IsPlaying())
	{
		return TEXT("grabarea: not in a game");
	}
	Api = ScctFeature<FScctGrabAreaApi>();
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("GrabArea: the bridge has no grab-area table (its reason is in the log above)"));
		return TEXT("grabarea: the bridge has no grab-area table");
	}
	if (Api->IsPracticeRound() == 0)
	{
		const FString Refusal = TEXT("grabarea works in practice games only (?Practice=1).");
		UE_LOG(LogOpenSCCT, Warning, TEXT("GrabArea: refused, not a practice round: %s"), *Refusal);
		Notify(Refusal);
		return Refusal;
	}

	if (!bWanted)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("GrabArea: shown"));
	}
	bWanted = true;
	LogTimer = 0.0f;
	ShowWidget();
	return TEXT("grabarea: on");
}

void UScctGrabAreaSubsystem::Notify(const FString& Text)
{
	Model->Notice = Text;
	Model->NoticeUntil = FPlatformTime::Seconds() + NoticeSeconds;
	ShowWidget();
}

void UScctGrabAreaSubsystem::HandleSessionChanged(int32 OldSessionId, int32 NewSessionId)
{
	// The overlay belongs to one match, as it does to the Godot client's play scene.
	bWanted = false;
	Model->bShown = false;
	Model->Areas.Reset();
	Model->Notice.Reset();
	Api = NewSessionId != 0 ? ScctFeature<FScctGrabAreaApi>() : nullptr;
	// The practice flag is there once the first frame's ticks have run.
	bAskPending = bAskAtStart && NewSessionId != 0;
	HideWidget();
}

void UScctGrabAreaSubsystem::HandleMatchStopped()
{
	bWanted = false;
	Model->bShown = false;
	HideWidget();
}

void UScctGrabAreaSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	if (bPressed && Name == TEXT("GrabArea"))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("GrabArea: binding: %s"), *Request(-1));
	}
}

void UScctGrabAreaSubsystem::HandleFrame(float DeltaSeconds)
{
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player != nullptr && CommandSource.Get() != Player)
	{
		if (CommandSource.IsValid())
		{
			CommandSource->OnLocalCommand.Remove(CommandHandle);
		}
		CommandSource = Player;
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctGrabAreaSubsystem::HandleLocalCommand);
	}

	if (bAskPending)
	{
		bAskPending = false;
		UE_LOG(LogOpenSCCT, Log, TEXT("GrabArea: -ScctGrabArea: %s"), *Request(1));
	}

	// A practice server can switch practice off between rounds.
	if (bWanted && (Api == nullptr || Api->IsPracticeRound() == 0))
	{
		bWanted = false;
		Notify(TEXT("grabarea works in practice games only (?Practice=1)."));
		UE_LOG(LogOpenSCCT, Warning, TEXT("GrabArea: hidden, the round is no longer a practice round"));
	}

	Model->bShown = bWanted;
	if (bWanted)
	{
		Project();
	}
	else
	{
		Model->Areas.Reset();
	}

	const bool bNotice = !Model->Notice.IsEmpty() && FPlatformTime::Seconds() < Model->NoticeUntil;
	if (!bWanted && !bNotice)
	{
		HideWidget();
	}
}

void UScctGrabAreaSubsystem::Project()
{
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || Api == nullptr)
	{
		Model->Areas.Reset();
		return;
	}

	FScctGrabArea Areas[MaxAreas];
	const int32 Count = FMath::Clamp(Api->GetGrabAreas(Areas, MaxAreas), 0, MaxAreas);
	// The overlay's units are viewport pixels divided by the DPI scale, as the HUD's.
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
	int32 States[3] = {};
	Model->Areas.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctGrabArea& Area = Areas[Index];
		FScctGrabAreaDrawn& Drawn = Model->Areas[Index];
		Drawn.State = FMath::Clamp(Area.State, 0, 2);
		++States[Drawn.State];
		const FVector Feet(Area.X, Area.Y, Area.Z);
		ProjectSector(*Player, Scale, Feet, Area.SectorYaw, Area.TurnedHalfAngle, Area.Radius, Drawn.Turned);
		ProjectSector(*Player, Scale, Feet, Area.SectorYaw, Area.HalfAngle, Area.Radius, Drawn.Facing);
		const FVector Ahead = Feet + FRotator(0.0f, Area.FacingYaw, 0.0f).Vector() * (Area.Radius * 0.5f);
		Drawn.bLine = ProjectPoint(*Player, Feet, Scale, Drawn.LineFrom) & ProjectPoint(*Player, Ahead, Scale, Drawn.LineTo);
	}

	LogTimer -= GetWorld()->GetDeltaSeconds();
	if (LogTimer <= 0.0f)
	{
		LogTimer = 2.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("GrabArea: %d mercs (%d/%d/%d)"), Count, States[0], States[1], States[2]);
	}
}

void UScctGrabAreaSubsystem::ShowWidget()
{
	if (Overlay.IsValid())
	{
		return;
	}
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (Viewport == nullptr)
	{
		return;
	}
	Overlay = SNew(SScctGrabAreaOverlay).Model(Model);
	ViewportContent = Overlay;
	Viewport->AddViewportWidgetContent(ViewportContent.ToSharedRef(), OverlayZOrder);
}

void UScctGrabAreaSubsystem::HideWidget()
{
	if (ViewportContent.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(ViewportContent.ToSharedRef());
		}
	}
	ViewportContent.Reset();
	Overlay.Reset();
}
