#include "Hud/ScctTeamPingSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Settings/ScctSettings.h"

#define LOCTEXT_NAMESPACE "ScctTeamPing"

namespace
{
	/** The command of the player's bindings. */
	const FName TeamPingCommand(TEXT("TeamPing"));

	/** Mouse counts that push the wheel's choice all the way out (Godot's TeamPingInput.MouseRadius). */
	constexpr float MouseRadius = 72.0f;

	/** Below this share of the radius the wheel chooses nothing (TeamPingInput.Deadzone). */
	constexpr float Deadzone = 0.25f;

	constexpr int32 RowCapacity = 16;

	const TCHAR* KindName(int32 Kind)
	{
		switch (Kind)
		{
		case EScctTeamPingKind::Generic: return TEXT("generic");
		case EScctTeamPingKind::Watch: return TEXT("watch");
		case EScctTeamPingKind::Check: return TEXT("check");
		case EScctTeamPingKind::CoopBoost: return TEXT("boost");
		case EScctTeamPingKind::Mine: return TEXT("mine");
		default: return TEXT("none");
		}
	}

	/** A kind by its name; -1 for none. "enemy" is not a kind of the protocol and is taken as watch (watch here). */
	int32 ParseKind(const FString& Name)
	{
		if (Name.Equals(TEXT("generic"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("here"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("tap"), ESearchCase::IgnoreCase))
		{
			return EScctTeamPingKind::Generic;
		}
		if (Name.Equals(TEXT("watch"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("enemy"), ESearchCase::IgnoreCase))
		{
			return EScctTeamPingKind::Watch;
		}
		if (Name.Equals(TEXT("check"), ESearchCase::IgnoreCase))
		{
			return EScctTeamPingKind::Check;
		}
		if (Name.Equals(TEXT("boost"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("coop"), ESearchCase::IgnoreCase))
		{
			return EScctTeamPingKind::CoopBoost;
		}
		if (Name.Equals(TEXT("mine"), ESearchCase::IgnoreCase))
		{
			return EScctTeamPingKind::Mine;
		}
		return -1;
	}

	/** The camera Unreal draws, as the bridge takes it. */
	bool PlayerCamera(const APlayerController* Player, FVector& Location, FRotator& Rotation)
	{
		if (Player == nullptr)
		{
			return false;
		}
		Player->GetPlayerViewPoint(Location, Rotation);
		return true;
	}
}

bool UScctTeamPingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctTeamPingSubsystem::RegisterSettings()
{
	FScctSettingDesc Hold;
	Hold.Key = TEXT("Hud.TeamPingHoldSeconds");
	Hold.Page = TEXT("Hud");
	Hold.Group = LOCTEXT("Group", "Team ping");
	Hold.Label = LOCTEXT("HoldLabel", "Hold for the ping wheel");
	Hold.Tooltip = LOCTEXT("HoldTip", "Seconds the team ping key is held before the wheel of ping kinds opens; a shorter press marks the spot you aim at.");
	Hold.Type = EScctSettingType::Float;
	Hold.Default = TEXT("0.25");
	Hold.Min = 0.1f;
	Hold.Max = 1.0f;
	Hold.Step = 0.05f;
	Hold.Order = 20;
	FScctSettings::Register(Hold);

	FScctSettingDesc Show;
	Show.Key = TEXT("Hud.ShowTeamPings");
	Show.Page = TEXT("Hud");
	Show.Group = LOCTEXT("Group", "Team ping");
	Show.Label = LOCTEXT("ShowLabel", "Show team pings");
	Show.Tooltip = LOCTEXT("ShowTip", "Draw the spots your teammates (and you) mark with the team ping, with their distance, or at the screen's edge when they are out of view.");
	Show.Type = EScctSettingType::Bool;
	Show.Default = TEXT("true");
	Show.Order = 21;
	FScctSettings::Register(Show);
}

void UScctTeamPingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSettings();

	// After the HUD's, so its frame (the snapshot, the scoreboard) has run when this one fills its part of the model.
	Collection.InitializeDependency<UScctHudSubsystem>();
	if (UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>())
	{
		Session->OnSessionChanged.AddUObject(this, &UScctTeamPingSubsystem::HandleSessionChanged);
		Session->OnFrame.AddUObject(this, &UScctTeamPingSubsystem::HandleFrame);
	}

	FString Debug;
	// Not stopping at the comma: the value is kind[,At].
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctTeamPing="), Debug, /*bShouldStopOnSeparator*/ false))
	{
		FString KindText = Debug, AtText;
		Debug.Split(TEXT(","), &KindText, &AtText);
		DebugKind = ParseKind(KindText.TrimStartAndEnd());
		if (!AtText.IsEmpty())
		{
			DebugAt = FCString::Atof(*AtText);
		}
		if (DebugKind < 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctTeamPing: '%s' is not a kind of ping (generic, watch, check, boost, mine)"), *KindText);
		}
		else if (KindText.Equals(TEXT("enemy"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctTeamPing: the protocol has no enemy kind; sending watch (watch here) instead"));
		}
	}
}

void UScctTeamPingSubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Session = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UScctSessionSubsystem>() : nullptr)
	{
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Detach();
	Super::Deinitialize();
}

void UScctTeamPingSubsystem::Attach(AScctPlayerController* Player)
{
	if (CommandSource.Get() == Player)
	{
		return;
	}
	Detach();
	if (Player == nullptr)
	{
		return;
	}
	CommandSource = Player;
	CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctTeamPingSubsystem::HandleLocalCommand);
	Player->LookSink.BindUObject(this, &UScctTeamPingSubsystem::HandleLook);
}

void UScctTeamPingSubsystem::Detach()
{
	if (AScctPlayerController* Player = CommandSource.Get())
	{
		Player->OnLocalCommand.Remove(CommandHandle);
		if (Player->LookSink.IsBoundToObject(this))
		{
			Player->LookSink.Unbind();
		}
	}
	CommandSource.Reset();
	CommandHandle.Reset();
}

void UScctTeamPingSubsystem::HandleSessionChanged(int32 OldSessionId, int32 NewSessionId)
{
	// The bridge's counts and pings belong to one session.
	Gesture = FGestureState();
	State = FScctTeamPingState{};
	Context = 0;
	LastReceived = 0;
	LastBadgeSignature = 0;
	Clock = 0.0;
	bDebugDone = false;
	bDebugWaitLogged = false;
}

int32 UScctTeamPingSubsystem::CurrentContext(const FScctTeamPingState& InState) const
{
	// The blocks of Godot's TeamPingHud that are Unreal's: a menu or the chat has the keys, the scoreboard is up, the HUD
	// is hidden or the round is over, the camera is inside a wall.
	const AScctPlayerController* Player = CommandSource.Get();
	const UScctHudSubsystem* Hud = GetWorld()->GetSubsystem<UScctHudSubsystem>();
	if (InState.Context == 0 || Player == nullptr || Hud == nullptr || Player->IsMenuOpen() || Player->GetLookView().Occluded != 0)
	{
		return 0;
	}
	const FScctHudModel& M = Hud->GetModel();
	if (!M.bHasState || M.bScoreboardHeld || (M.State.Flags & (EScctHudFlags::HideHud | EScctHudFlags::GameEnded)) != 0)
	{
		return 0;
	}
	return InState.Context;
}

void UScctTeamPingSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	if (Name != TeamPingCommand)
	{
		return;
	}
	if (bPressed)
	{
		Press(FPlatformTime::Seconds());
	}
	else
	{
		Release(FPlatformTime::Seconds());
	}
}

bool UScctTeamPingSubsystem::HandleLook(float MouseX, float MouseY, float RightX, float RightY)
{
	if (!Gesture.bWheelOpen)
	{
		return false;
	}
	// Unreal's mouse Y and stick Y are positive away from the player, the screen's up; the wheel's Y is the screen's down.
	if (MouseX != 0.0f || MouseY != 0.0f)
	{
		FVector2f Direction = Gesture.MouseDirection + FVector2f(MouseX, -MouseY) / MouseRadius;
		if (Direction.SizeSquared() > 1.0f)
		{
			Direction.Normalize();
		}
		Choose(Direction);
	}
	// The stick chooses by where it points now (TeamPingInput.StickDirection), only when it moved.
	const FVector2f Stick(RightX, -RightY);
	if (Stick != Gesture.StickDirection)
	{
		Gesture.StickDirection = Stick;
		const FVector2f Mouse = Gesture.MouseDirection;
		Choose(Stick);
		Gesture.MouseDirection = Mouse;
	}
	return true;
}

void UScctTeamPingSubsystem::Choose(const FVector2f& Direction)
{
	Gesture.MouseDirection = Direction;
	int32 Selection = -1;
	if (Direction.SizeSquared() >= Deadzone * Deadzone)
	{
		if (FMath::Abs(Direction.X) > FMath::Abs(Direction.Y))
		{
			Selection = Direction.X > 0.0f ? EScctTeamPingKind::Check : (Gesture.bMerc ? -1 : EScctTeamPingKind::Mine);
		}
		else
		{
			Selection = Direction.Y < 0.0f ? EScctTeamPingKind::Watch : (Gesture.bMerc ? -1 : EScctTeamPingKind::CoopBoost);
		}
	}
	if (Selection != Gesture.Selection)
	{
		Gesture.Selection = Selection;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: the wheel points at %s"), KindName(Selection));
	}
}

void UScctTeamPingSubsystem::Press(double Now)
{
	if (Gesture.bDown || Context == 0)
	{
		if (Context == 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: the key went down while no ping can be placed (bridge context %d, supported %d)"), State.Context, State.Supported);
		}
		return;
	}
	Gesture.bDown = true;
	Gesture.Context = Context;
	Gesture.PressedAt = Now;
	Gesture.bMerc = State.Team == 1;
}

void UScctTeamPingSubsystem::Release(double Now)
{
	if (!Gesture.bDown)
	{
		return;
	}
	const float HoldSeconds = FMath::Clamp(FScctSettings::GetFloat(TEXT("Hud.TeamPingHoldSeconds")), 0.1f, 1.0f);
	int32 Choice = Gesture.Selection;
	if (!Gesture.bWheelOpen && Now - Gesture.PressedAt < HoldSeconds)
	{
		Choice = EScctTeamPingKind::Generic;
	}
	if (Choice >= 0 && !Gesture.bPending)
	{
		Gesture.bPending = true;
		Gesture.PendingContext = Gesture.Context;
		Gesture.PendingKind = Choice;
		Gesture.bPendingFromWheel = Gesture.bWheelOpen;
	}
	else if (Choice < 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: the wheel closed on its middle, nothing is sent"));
	}
	if (Gesture.bWheelOpen)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: the wheel closed on %s"), KindName(Choice));
	}
	CloseGesture();
}

void UScctTeamPingSubsystem::CloseGesture()
{
	Gesture.bDown = false;
	Gesture.Context = 0;
	Gesture.bWheelOpen = false;
	Gesture.Selection = -1;
	Gesture.MouseDirection = FVector2f::ZeroVector;
	Gesture.StickDirection = FVector2f::ZeroVector;
}

void UScctTeamPingSubsystem::CancelGesture(const TCHAR* Why)
{
	if (Gesture.bDown || Gesture.bPending)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: the gesture is dropped (%s)"), Why);
	}
	CloseGesture();
	Gesture.bPending = false;
}

void UScctTeamPingSubsystem::Advance(double Now, bool bInputAvailable)
{
	if (!Gesture.bDown && !Gesture.bPending)
	{
		return;
	}
	if (!bInputAvailable || Context == 0)
	{
		CancelGesture(!bInputAvailable ? TEXT("the game lost the keys") : TEXT("no ping can be placed now"));
		return;
	}

	// The release of the frame before: sent if nothing changed since, aimed where the camera points now.
	if (Gesture.bPending)
	{
		Gesture.bPending = false;
		if (Gesture.PendingContext == Context)
		{
			Send(Gesture.PendingKind, Gesture.bPendingFromWheel ? TEXT("the wheel") : TEXT("a tap"));
		}
	}

	if (!Gesture.bDown)
	{
		return;
	}
	if (Gesture.Context != Context)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: the pawn, view or team changed under the gesture; it is dropped"));
		CloseGesture();
		return;
	}
	const float HoldSeconds = FMath::Clamp(FScctSettings::GetFloat(TEXT("Hud.TeamPingHoldSeconds")), 0.1f, 1.0f);
	if (Gesture.bWheelOpen || Now - Gesture.PressedAt < HoldSeconds)
	{
		return;
	}
	Gesture.bWheelOpen = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: the wheel opened for the %s"), Gesture.bMerc ? TEXT("mercs (watch, check)") : TEXT("spies (watch, check, boost, mine)"));
}

bool UScctTeamPingSubsystem::Send(int32 Kind, const TCHAR* Why)
{
	const FScctTeamPingApi* Api = ScctFeature<FScctTeamPingApi>();
	FVector Location;
	FRotator Rotation;
	if (Api == nullptr || !PlayerCamera(CommandSource.Get(), Location, Rotation))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctTeamPing: not sent (%s): no team ping table or no camera"), Why);
		return false;
	}
	FScctTeamPingCamera Camera;
	Camera.X = static_cast<float>(Location.X);
	Camera.Y = static_cast<float>(Location.Y);
	Camera.Z = static_cast<float>(Location.Z);
	Camera.Pitch = static_cast<float>(Rotation.Pitch);
	Camera.Yaw = static_cast<float>(Rotation.Yaw);
	Camera.Roll = static_cast<float>(Rotation.Roll);
	float Target[3] = {};
	const int32 Result = Api->Send(Kind, &Camera, Target);
	if (Result == 1)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: sent %s (%s) aimed from (%.0f,%.0f,%.0f) pitch %.1f yaw %.1f, the spot at (%.0f,%.0f,%.0f), %.0f units away"),
			KindName(Kind), Why, Camera.X, Camera.Y, Camera.Z, Camera.Pitch, Camera.Yaw, Target[0], Target[1], Target[2],
			FVector::Distance(Location, FVector(Target[0], Target[1], Target[2])));
		return true;
	}
	UE_LOG(LogOpenSCCT, Warning, TEXT("ScctTeamPing: %s (%s) not sent: %s"), KindName(Kind), Why,
		Result == -1 ? TEXT("the kind is not one of the team's") : Result == -2 ? TEXT("the aim found no spot") : TEXT("no ping can be placed now"));
	return false;
}

bool UScctTeamPingSubsystem::RunCommand(const FString& Name, int32 Argument)
{
	if (Name.Equals(TEXT("PingKey"), ESearchCase::IgnoreCase))
	{
		AScctPlayerController* Player = CommandSource.Get();
		const bool bDone = Player != nullptr && Player->HoldBoundCommand(TEXT("TeamPing"), Argument != 0);
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: PingKey %s%s"), Argument != 0 ? TEXT("down") : TEXT("up"),
			bDone ? TEXT("") : TEXT(" refused (no player, or no key is bound to TeamPing: bind one, e.g. the console's ScctBind merc J TeamPing)"));
		return true;
	}
	if (Name.Equals(TEXT("PingWheel"), ESearchCase::IgnoreCase))
	{
		static const FVector2f Directions[] = { FVector2f(0.0f, 0.0f), FVector2f(0.0f, -1.0f), FVector2f(1.0f, 0.0f), FVector2f(0.0f, 1.0f), FVector2f(-1.0f, 0.0f) };
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: PingWheel %d%s"), Argument, Gesture.bWheelOpen ? TEXT("") : TEXT(" (the wheel is not open)"));
		if (Gesture.bWheelOpen)
		{
			Choose(Directions[FMath::Clamp(Argument, 0, 4)]);
		}
		return true;
	}
	return false;
}

void UScctTeamPingSubsystem::HandleFrame(float DeltaSeconds)
{
	Clock += DeltaSeconds;
	Attach(Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController()));
	const FScctTeamPingApi* Api = ScctFeature<FScctTeamPingApi>();
	if (Api == nullptr)
	{
		if (!bTableMissingLogged)
		{
			bTableMissingLogged = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctTeamPing: the bridge has no team ping table; the TeamPing key does nothing"));
		}
		Context = 0;
		Present(false);
		return;
	}

	Api->GetState(&State);
	Context = CurrentContext(State);
	if (State.Received != LastReceived)
	{
		LogReceived();
		LastReceived = State.Received;
	}

	const AScctPlayerController* Player = CommandSource.Get();
	Advance(FPlatformTime::Seconds(), Player != nullptr && Player->HasPlayInput());

	// -ScctTeamPing=kind[,At]
	if (DebugKind >= 0 && !bDebugDone && Clock >= DebugAt)
	{
		if (Context != 0)
		{
			bDebugDone = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctTeamPing: sending %s at %.1f s"), KindName(DebugKind), Clock);
			Send(DebugKind, TEXT("-ScctTeamPing"));
		}
		else if (!bDebugWaitLogged)
		{
			bDebugWaitLogged = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctTeamPing: waiting for a ping to be possible (bridge context %d, supported %d, team %d)"), State.Context, State.Supported, State.Team);
		}
	}

	Present(Context != 0 && FScctSettings::GetBool(TEXT("Hud.ShowTeamPings")));
}

void UScctTeamPingSubsystem::LogReceived()
{
	const FScctTeamPingApi* Api = ScctFeature<FScctTeamPingApi>();
	FScctTeamPingRow Rows[RowCapacity];
	const int32 Count = Api != nullptr ? FMath::Clamp(Api->GetPings(Rows, RowCapacity), 0, RowCapacity) : 0;
	const FScctTeamPingRow* Newest = nullptr;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Newest == nullptr || Rows[Index].Age < Newest->Age)
		{
			Newest = &Rows[Index];
		}
	}
	if (Newest == nullptr)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: received %d in all (none is live now)"), State.Received);
		return;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: received %s from slot %d%s for team %d at (%.0f,%.0f,%.0f), %.2f s old (%d received, %d live)"),
		KindName(Newest->Kind), Newest->SenderId, Newest->Own ? TEXT(" (the player's own, echoed to the team)") : TEXT(""), Newest->Team,
		Newest->X, Newest->Y, Newest->Z, Newest->Age, State.Received, Count);
}

void UScctTeamPingSubsystem::Present(bool bShow)
{
	UScctHudSubsystem* Hud = GetWorld()->GetSubsystem<UScctHudSubsystem>();
	if (Hud == nullptr)
	{
		return;
	}
	FScctHudModel& M = Hud->GetMutableModel();
	FScctTeamPingView& View = M.TeamPing;
	View.Badges.Reset();
	View.bWheelOpen = Gesture.bWheelOpen && Context != 0;
	View.WheelTeam = Gesture.bMerc ? 1 : 0;
	View.Selection = Gesture.Selection;

	const FScctTeamPingApi* Api = ScctFeature<FScctTeamPingApi>();
	const APlayerController* Player = CommandSource.Get();
	const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	FVector CameraLocation;
	FRotator CameraRotation;
	if (!bShow || Api == nullptr || Viewport == nullptr || !PlayerCamera(Player, CameraLocation, CameraRotation))
	{
		return;
	}
	FScctTeamPingRow Rows[RowCapacity];
	const int32 Count = FMath::Clamp(Api->GetPings(Rows, RowCapacity), 0, RowCapacity);
	if (Count == 0)
	{
		if (LastBadgeSignature != 0)
		{
			LastBadgeSignature = 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: no badge on screen"));
		}
		return;
	}

	// The full-screen layer's units: viewport pixels over the DPI scale. A badge's centre stays Margin in from the edge
	// (Godot's 31 at 720 lines, scaled with the screen's height).
	FVector2D Pixels;
	Viewport->GetViewportSize(Pixels);
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
	const FVector2f Size = FVector2f(Pixels) / Scale;
	const float K = FMath::Clamp(Size.Y / 720.0f, 0.75f, 2.25f);
	View.Margin = 31.0f * K;
	const FVector2f Centre = Size * 0.5f;
	const FVector2f Extent = Centre - FVector2f(View.Margin);
	if (Extent.X <= 0.0f || Extent.Y <= 0.0f)
	{
		return;
	}

	const FVector Eye(M.State.X, M.State.Y, M.State.Z);
	uint32 Signature = GetTypeHash(Count);
	FString Line;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctTeamPingRow& Row = Rows[Index];
		const FVector World(Row.X, Row.Y, Row.Z);
		FScctTeamPingBadge Badge;
		Badge.Kind = Row.Kind;
		Badge.Team = Row.Team;
		Badge.Opacity = Row.Opacity;
		Badge.Distance = static_cast<float>(FVector::Distance(Eye, World));
		Badge.Sender = M.Text(Row.SenderNameId);
		Badge.bOwn = Row.Own != 0;

		// In front: Unreal's own projection; inside the margin it is drawn there. Behind, or outside: the way to it from
		// the screen's centre (behind, the camera-space right and up), clamped to the margin's rectangle.
		const FVector Local = CameraRotation.UnrotateVector(World - CameraLocation);
		FVector2f Direction;
		FVector2D Screen;
		if (Local.X > 1.0 && Player->ProjectWorldLocationToScreen(World, Screen, false))
		{
			const FVector2f Point = FVector2f(Screen) / Scale;
			Direction = Point - Centre;
			if (FMath::Abs(Direction.X) <= Extent.X && FMath::Abs(Direction.Y) <= Extent.Y)
			{
				Badge.Screen = Point;
				View.Badges.Add(Badge);
				Signature = HashCombine(Signature, GetTypeHash(Row.Kind * 2));
				Line += FString::Printf(TEXT(" %s at (%.0f,%.0f) %.0f units opacity %.2f%s;"), KindName(Row.Kind), Point.X, Point.Y, Badge.Distance, Row.Opacity, Badge.bOwn ? TEXT(" own") : TEXT(""));
				continue;
			}
		}
		else
		{
			Direction = FVector2f(static_cast<float>(Local.Y), static_cast<float>(-Local.Z));
		}
		Direction = Direction.IsNearlyZero() ? FVector2f(0.0f, 1.0f) : Direction.GetSafeNormal();
		const float Edge = 1.0f / FMath::Max(FMath::Abs(Direction.X) / Extent.X, FMath::Abs(Direction.Y) / Extent.Y);
		Badge.Screen = Centre + Direction * Edge;
		Badge.Direction = Direction;
		Badge.bAtEdge = true;
		View.Badges.Add(Badge);
		Signature = HashCombine(Signature, GetTypeHash(Row.Kind * 2 + 1));
		Line += FString::Printf(TEXT(" %s at the edge (%.0f,%.0f) pointing (%.2f,%.2f) %.0f units opacity %.2f%s;"), KindName(Row.Kind), Badge.Screen.X, Badge.Screen.Y,
			Direction.X, Direction.Y, Badge.Distance, Row.Opacity, Badge.bOwn ? TEXT(" own") : TEXT(""));
	}
	if (Signature != LastBadgeSignature)
	{
		LastBadgeSignature = Signature;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctTeamPing: %d badge(s) on a %.0f x %.0f screen:%s"), View.Badges.Num(), Size.X, Size.Y, *Line);
	}
}

#undef LOCTEXT_NAMESPACE
