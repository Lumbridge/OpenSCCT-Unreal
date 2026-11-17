#include "Play/ScctPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace
{
	/** The simulation turns in units of 65536 to the circle, 3 of them per mouse count at a multiplier of 1. */
	constexpr float RotationUnitsPerCount = 3.0f;
	constexpr float RotationUnitsPerDegree = 65536.0f / 360.0f;

	/**
	 * A camera that moved further than this in one frame, and faster than anything in the game travels, did not
	 * continue the last picture. The distance is the bridge's own cut distance between two ticks. A frame longer
	 * than JumpFrameSeconds (a hitch) is allowed no more travel than one of that length.
	 */
	constexpr float JumpDistance = 128.0f;
	constexpr float JumpSpeed = 6000.0f;
	constexpr float JumpFrameSeconds = 0.1f;

	/**
	 * The horizontal angle of a 4:3 picture that is VerticalFov high, degrees. The view pawn's camera takes its
	 * field of view in that form (the simulation's own) and keeps the vertical angle on any screen.
	 */
	float FourByThreeFov(float VerticalFov)
	{
		const float HalfVertical = FMath::DegreesToRadians(VerticalFov * 0.5f);
		return FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfVertical) * 4.0f / 3.0f));
	}
}

AScctPlayerController::AScctPlayerController()
{
	bShowMouseCursor = false;
}

void AScctPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	// The game has the keys and captures the mouse (hidden cursor, high precision deltas) without a first click
	// on anything.
	SetInputMode(FInputModeGameOnly());

	Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
	if (Session.IsValid())
	{
		BeforeTicksHandle = Session->OnBeforeTicks.AddUObject(this, &AScctPlayerController::FeedControls);
		SessionChangedHandle = Session->OnSessionChanged.AddUObject(this, &AScctPlayerController::ForgetView);
		Session->ViewOverride.BindUObject(this, &AScctPlayerController::OverrideView);
	}

	FString Script;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctAutoplay="), Script, false))
	{
		FString Error;
		if (Autoplay.Parse(Script, Error))
		{
			Autoplay.OnReport = [this](const FString& Label) { ReportAutoplay(Label); };
			UE_LOG(LogOpenSCCT, Log, TEXT("Autoplay: script \"%s\" waits for the local player's body"), *Script);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Error, TEXT("Autoplay: %s; nothing will be played"), *Error);
		}
	}
}

void AScctPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Session.IsValid())
	{
		Session->OnBeforeTicks.Remove(BeforeTicksHandle);
		Session->OnSessionChanged.Remove(SessionChangedHandle);
		Session->ViewOverride.Unbind();
	}
	Super::EndPlay(EndPlayReason);
}

void AScctPlayerController::PlayerTick(float DeltaTime)
{
	// The autoplay's events go in before Unreal works through the frame's input (inside Super), as events from
	// the devices have by now.
	if (Autoplay.HasSteps() && !Autoplay.IsFinished() && Session.IsValid())
	{
		if (!bAutoplayStarted && Session->IsPlaying() && Session->FindLocalProxy() != nullptr)
		{
			bAutoplayStarted = true;
		}
		if (bAutoplayStarted)
		{
			Autoplay.Tick(DeltaTime, *this, GetMouseCountsPerDegree());
		}
	}

	// A tapped key was down for the whole of the frame before; it comes up now.
	for (const FKey& Key : TappedKeys)
	{
		FScctControlAutoplay::SendKey(*this, Key, false);
	}
	TappedKeys.Reset();

	Super::PlayerTick(DeltaTime);
}

void AScctPlayerController::SetMenuOpen(bool bOpen)
{
	if (bMenuOpen != bOpen)
	{
		bMenuOpen = bOpen;
		UE_LOG(LogOpenSCCT, Log, TEXT("Controls: %s"), bOpen ? TEXT("a menu is open, the player's keys and mouse are held back") : TEXT("the menu closed, the keys and the mouse are the player's again"));
	}
}

TArray<FKey> AScctPlayerController::FindKeysBoundTo(const FString& Command) const
{
	TArray<FKey> Found;
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
	if (Controls == nullptr)
	{
		return Found;
	}

	// "SwitchHeadset 1" is asked for with its number, "ShowMenu" by its name whatever number follows it.
	const FString Wanted = Command.TrimStartAndEnd();
	const bool bWithNumber = Wanted.Contains(TEXT(" "));
	const int32 bMerc = Controls->GetSide() == 1 ? 1 : 0;
	const TArray<TPair<FKey, FKey>>& Table = FScctControls::Keys();
	for (int32 Code = 0; Code < Table.Num(); ++Code)
	{
		TCHAR KeyName[64] = {};
		TCHAR Bound[512] = {};
		if (!Table[Code].Key.IsValid() || Controls->GetKeyName(Code, KeyName, UE_ARRAY_COUNT(KeyName) - 1) <= 0
			|| Controls->GetBinding(bMerc, 0, KeyName, Bound, UE_ARRAY_COUNT(Bound) - 1) <= 0)
		{
			continue;
		}

		// A binding is a list of actions: "Button bFire | Function Fire | GadgetQuickChoice 0".
		TArray<FString> Actions;
		FString(Bound).ParseIntoArray(Actions, TEXT("|"));
		for (FString& Action : Actions)
		{
			Action.TrimStartAndEndInline();
			Action.RemoveFromStart(TEXT("Function "), ESearchCase::IgnoreCase);
			Action.TrimStartAndEndInline();
			int32 Space = INDEX_NONE;
			if (!bWithNumber && Action.FindChar(TEXT(' '), Space))
			{
				Action.LeftInline(Space);
			}
			if (Action.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				Found.Add(Table[Code].Key);
				break;
			}
		}
	}
	return Found;
}

bool AScctPlayerController::HoldScriptedAction(const FString& Action, bool bHeld)
{
	const FKey Key = FScctControlAutoplay::FindActionKey(Action);
	if (!Key.IsValid())
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Controls: '%s' is not an action a script can hold"), *Action);
		return false;
	}
	bScripted = true;
	FScctControlAutoplay::SendKey(*this, Key, bHeld);
	return true;
}

bool AScctPlayerController::TapBoundCommand(const FString& Command)
{
	const TArray<FKey> Keys = FindKeysBoundTo(Command);
	if (Keys.Num() == 0)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Controls: no key of the current side is bound to '%s', so a script cannot press it"), *Command);
		return false;
	}
	bScripted = true;
	FScctControlAutoplay::SendKey(*this, Keys[0], true);
	TappedKeys.Add(Keys[0]);
	return true;
}

bool AScctPlayerController::HasPlayInput() const
{
	// A menu over the match has the keys and the mouse, whoever plays.
	if (bMenuOpen)
	{
		return false;
	}

	// Nobody is at the keyboard of an autoplay run and its window never has focus: the script is the player.
	if (Autoplay.HasSteps() || bScripted)
	{
		return true;
	}

	// A menu that shows the cursor has the mouse, and a text field or the console takes the keyboard focus from
	// the viewport: in both cases the keys are not the pawn's.
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameViewportClient* Client = LocalPlayer != nullptr ? LocalPlayer->ViewportClient.Get() : nullptr;
	const FViewport* Viewport = Client != nullptr ? Client->Viewport : nullptr;
	// HasFocus is the keyboard focus inside the game's own windows, which stays where it was while another
	// program is in front, so the window is asked as well.
	return Viewport != nullptr && Viewport->HasFocus() && Viewport->IsForegroundWindow() && !bShowMouseCursor;
}

bool AScctPlayerController::IsPadActive() const
{
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
	return Controls != nullptr && Controls->IsPadActive() != 0;
}

void AScctPlayerController::FeedControls(float DeltaSeconds)
{
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
	if (Controls == nullptr)
	{
		if (!bReportedNoControls)
		{
			bReportedNoControls = true;
			FString Error;
			ScctFeature<FScctControlsApi>(&Error);
			UE_LOG(LogOpenSCCT, Error, TEXT("The bridge's controls are missing, so keys, mouse and controller do nothing and the camera has no look-ahead: %s"), *Error);
		}
		return;
	}

	// Before the first frame of input, so the first tick already plays with the player's settings and keys.
	UScctControlSettings::Get()->EnsureLoaded();

	// Without the Play flag the bridge releases everything held and ignores keys still down when it returns, so
	// a frame is sent either way.
	FScctRawInput Raw = {};
	Raw.DeltaSeconds = DeltaSeconds;
	if (HasPlayInput())
	{
		ReadDevices(Raw);
		Raw.Flags = EScctRawInputFlags::Play;
	}
	FrameMouseX = Raw.MouseX;
	Controls->Frame(&Raw);

	TakeLocalCommands(*Controls);
}

void AScctPlayerController::ReadDevices(FScctRawInput& Raw) const
{
	const UPlayerInput* Input = PlayerInput;
	if (Input == nullptr)
	{
		return;
	}

	const TArray<TPair<FKey, FKey>>& Table = FScctControls::Keys();
	for (int32 Code = 0; Code < Table.Num(); ++Code)
	{
		uint8 State = 0;
		int32 Presses = 0;
		for (const FKey* Key : { &Table[Code].Key, &Table[Code].Value })
		{
			const FKeyState* KeyState = Key->IsValid() ? Input->GetKeyState(*Key) : nullptr;
			if (KeyState == nullptr)
			{
				continue;
			}
			if (KeyState->bDown)
			{
				State |= EScctKeyState::Down;
			}
			// Every press since the last frame is counted, so a key that went down and up again inside one
			// frame is still seen, and so is each notch of the wheel.
			Presses += KeyState->EventCounts[IE_Pressed].Num();
		}
		if (Presses > 0)
		{
			State |= EScctKeyState::Pressed;
		}

		// The wheel's two codes hold the notches turned this frame, not a key state: a wheel has no "down".
		const bool bWheel = Code == static_cast<int32>(EScctKey::MouseWheelUp) || Code == static_cast<int32>(EScctKey::MouseWheelDown);
		Raw.Keys[Code] = bWheel ? static_cast<uint8>(FMath::Min(Presses, 255)) : State;
	}

	// The raw values: what the devices reported, before Unreal's axis sensitivity, smoothing, field of view
	// scaling and dead zones (see RAW MOUSE in the header).
	Raw.MouseX = Input->GetRawKeyValue(EKeys::MouseX);
	Raw.MouseY = Input->GetRawKeyValue(EKeys::MouseY);
	// Unreal adds up an axis's samples when several arrive inside one frame, which is right for mouse counts and
	// wrong for a stick's position: a stick cannot be further out than its edge.
	const auto Stick = [Input](const FKey& Key) { return FMath::Clamp(Input->GetRawKeyValue(Key), -1.0f, 1.0f); };
	Raw.LeftX = Stick(EKeys::Gamepad_LeftX);
	Raw.LeftY = Stick(EKeys::Gamepad_LeftY);
	Raw.RightX = Stick(EKeys::Gamepad_RightX);
	Raw.RightY = Stick(EKeys::Gamepad_RightY);
	Raw.LeftTrigger = FMath::Max(Stick(EKeys::Gamepad_LeftTriggerAxis), 0.0f);
	Raw.RightTrigger = FMath::Max(Stick(EKeys::Gamepad_RightTriggerAxis), 0.0f);
}

void AScctPlayerController::TakeLocalCommands(const FScctControlsApi& Controls)
{
	FScctLocalCommand Commands[16];
	int32 Count;
	do
	{
		Count = FMath::Clamp(Controls.TakeLocalCommands(Commands, UE_ARRAY_COUNT(Commands)), 0, static_cast<int32>(UE_ARRAY_COUNT(Commands)));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FScctLocalCommand& Command = Commands[Index];
			FName& Name = CommandNames.FindOrAdd(Command.NameId);
			if (Name.IsNone())
			{
				TCHAR Buffer[64] = {};
				Controls.GetName(Command.NameId, Buffer, UE_ARRAY_COUNT(Buffer));
				Buffer[UE_ARRAY_COUNT(Buffer) - 1] = TEXT('\0');
				Name = FName(Buffer);
			}

			UE_LOG(LogOpenSCCT, Log, TEXT("Interface command %s %d %s"), *Name.ToString(), Command.Arg, Command.Pressed != 0 ? TEXT("pressed") : TEXT("released"));
			if (Command.Pressed != 0 && Name == TEXT("ToggleAutoHack"))
			{
				// The bridge has already switched the setting, in memory only: keep it.
				UScctControlSettings* Settings = UScctControlSettings::Get();
				Settings->Refresh();
				Settings->Save();
			}
			OnLocalCommand.Broadcast(Name, Command.Arg, Command.Pressed != 0);
		}
	}
	while (Count == UE_ARRAY_COUNT(Commands));
}

void AScctPlayerController::OverrideView(FScctBridgeView& View)
{
	const FScctLookView Before = LookView;
	LookView = {};

	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
	int32 SizeX = 0, SizeY = 0;
	GetViewportSize(SizeX, SizeY);
	const float Aspect = SizeY > 0 ? static_cast<float>(SizeX) / static_cast<float>(SizeY) : 0.0f;
	if (Controls == nullptr || Controls->GetView(&LookView, Aspect, 0.0f) == 0 || LookView.Valid == 0)
	{
		// No view of the controls' this frame (no pawn or camera yet): the bridge's own stays as it is.
		LookView = {};
		bHadLookView = false;
		return;
	}

	View.X = LookView.X;
	View.Y = LookView.Y;
	View.Z = LookView.Z;
	View.Pitch = LookView.Pitch;
	View.Yaw = LookView.Yaw;
	View.Roll = LookView.Roll;
	View.SpyFirstPerson = LookView.SpyFirstPerson;
	View.Valid = 1;
	if (LookView.VerticalFov > 1.0f)
	{
		// VerticalFov has the zoom, scope and aim blends, the widescreen setting and the field of view cap in
		// it. Past 16:9 it is the 16:9 angle, so an ultrawide screen sees more at the sides than the original.
		View.Fov = FourByThreeFov(LookView.VerticalFov);
	}

	// The bridge compares the last two ticks for its Cut, so a jump is not flagged when two or more ticks ran in
	// the frame it happened in (the first frame after a load, any frame at a low frame rate). The picture is
	// compared with the last one drawn here as well.
	const float Move = bHadLookView
		? FVector::Dist(FVector(Before.X, Before.Y, Before.Z), FVector(LookView.X, LookView.Y, LookView.Z))
		: 0.0f;
	const UWorld* World = GetWorld();
	const float FrameSeconds = World != nullptr ? World->GetDeltaSeconds() : 0.0f;
	const bool bJumped = bHadLookView
		&& (Before.SpyFirstPerson != LookView.SpyFirstPerson || Move > FMath::Max(JumpDistance, JumpSpeed * FMath::Min(FrameSeconds, JumpFrameSeconds)));

	if (PlayerCameraManager != nullptr)
	{
		// Cut stays set for every frame drawn until the next tick, and all through a cinematic camera: only its
		// first frame is the cut, the rest continue it and keep their motion vectors and history.
		if ((LookView.Cut != 0 && !bWasCut) || !bHadLookView || bJumped)
		{
			PlayerCameraManager->SetGameCameraCutThisFrame();
			++CutsRaised;
		}

		// No place for the spy's eye that is clear of the walls: the original shows nothing that frame.
		const bool bOccluded = LookView.Occluded != 0;
		if (bOccluded != bWasOccluded)
		{
			PlayerCameraManager->SetManualCameraFade(bOccluded ? 1.0f : 0.0f, FLinearColor::Black, false);
			bWasOccluded = bOccluded;
		}
	}
	bWasCut = LookView.Cut != 0;

	if (bHadLookView && LookView.Cut == 0 && !bJumped)
	{
		const float YawTurn = FMath::FindDeltaAngleDegrees(Before.Yaw, LookView.Yaw);
		const float Turn = FMath::Max(FMath::Abs(YawTurn), FMath::Abs(FMath::FindDeltaAngleDegrees(Before.Pitch, LookView.Pitch)));
		LargestViewTurn = FMath::Max(LargestViewTurn, Turn);
		LargestViewMove = FMath::Max(LargestViewMove, Move);
		if (Autoplay.HasSteps() && LookView.LookAhead != 0)
		{
			// With no frame of delay the picture has turned by exactly the counts this frame handed over.
			LargestTurnError = FMath::Max(LargestTurnError, FMath::Abs(YawTurn - FrameMouseX / GetMouseCountsPerDegree()));
		}
	}
	bHadLookView = true;
}

void AScctPlayerController::ForgetView(int32 OldSessionId, int32 NewSessionId)
{
	// The view belonged to the match that is over: nobody is handed its aim any more, the next match's first
	// picture continues nothing, and a black screen it left behind is lifted.
	LookView = {};
	bHadLookView = false;
	bWasCut = false;
	if (bWasOccluded && PlayerCameraManager != nullptr)
	{
		PlayerCameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, false);
	}
	bWasOccluded = false;
}

float AScctPlayerController::GetMouseCountsPerDegree() const
{
	const UScctControlSettings* Settings = UScctControlSettings::Get();
	const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
	const bool bMerc = Controls != nullptr && Controls->GetSide() == 1;
	const float Multiplier = (bMerc ? Settings->MercMouse.Look : Settings->SpyMouse.FreeLook) * Settings->MouseScale;
	return RotationUnitsPerDegree / (RotationUnitsPerCount * FMath::Max(Multiplier, 0.001f));
}

void AScctPlayerController::ReportAutoplay(const FString& Label)
{
	FString Body = TEXT("no local pawn");
	if (Session.IsValid())
	{
		for (const FScctBridgePawn& Each : Session->GetPawns())
		{
			if ((Each.Flags & EScctPawnFlags::Local) != 0)
			{
				Body = FString::Printf(TEXT("pawn at (%.0f %.0f %.0f) yaw %.1f height %.0f health %d"),
					Each.X, Each.Y, Each.Z, Each.Yaw, Each.CollisionHeight, Each.Health);
				break;
			}
		}
	}

	FString Hud;
	if (const FScctHudApi* HudApi = ScctFeature<FScctHudApi>())
	{
		FScctHudState State = {};
		if (HudApi->GetState(&State) != 0)
		{
			TCHAR SimState[64] = {};
			HudApi->GetText(State.StateNameId, SimState, UE_ARRAY_COUNT(SimState));
			SimState[UE_ARRAY_COUNT(SimState) - 1] = TEXT('\0');
			Hud = FString::Printf(TEXT(" | state %s clip %d ammo %d fire mode %d gadget %d vision %d"),
				SimState, State.ClipAmmo, State.Ammo, State.FireMode, State.SelectedGadget, State.VisionMode);
		}
	}

	// What Unreal really draws, from the projection it makes for the camera as it stands: the check that the
	// field of view arrives on the screen as the bridge meant it.
	FString Drawn = TEXT("unknown");
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	FViewport* Viewport = LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr ? LocalPlayer->ViewportClient->Viewport : nullptr;
	FSceneViewProjectionData Projection;
	if (Viewport != nullptr && LocalPlayer->GetProjectionData(Viewport, Projection))
	{
		const FMatrix& Matrix = Projection.ProjectionMatrix;
		if (Matrix.M[0][0] > 0.0 && Matrix.M[1][1] > 0.0)
		{
			Drawn = FString::Printf(TEXT("%.1f wide, %.1f high"),
				FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Matrix.M[0][0])), FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Matrix.M[1][1])));
		}
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Autoplay: %s | %s | view at (%.0f %.0f %.0f) yaw %.2f pitch %.2f fov %.1f (wanted %.1f wide, %.1f high; Unreal draws %s) aim blend %.2f%s | since the last line: the view turned at most %.3f deg and moved at most %.2f cm between two frames, was at most %.3f deg off the mouse counts of its frame, camera cuts %d"),
		*Label, *Body, LookView.X, LookView.Y, LookView.Z, LookView.Yaw, LookView.Pitch, LookView.Fov,
		LookView.HorizontalFov, LookView.VerticalFov, *Drawn, LookView.RifleAimBlend, *Hud, LargestViewTurn, LargestViewMove, LargestTurnError, CutsRaised);
	LargestViewTurn = 0.0f;
	LargestViewMove = 0.0f;
	LargestTurnError = 0.0f;
	CutsRaised = 0;
}

void AScctPlayerController::ScctInvertY(int32 bInvert)
{
	UScctControlSettings* Settings = UScctControlSettings::Get();
	Settings->bInvertY = bInvert != 0;
	Settings->Apply();
	UE_LOG(LogOpenSCCT, Log, TEXT("Control settings: invert Y is now %d (not saved until ScctControlsSave)"), Settings->bInvertY ? 1 : 0);
}

void AScctPlayerController::ScctMouseScale(float Scale)
{
	UScctControlSettings* Settings = UScctControlSettings::Get();
	Settings->MouseScale = Scale;
	Settings->Apply();
	UE_LOG(LogOpenSCCT, Log, TEXT("Control settings: mouse scale is now %.3f (not saved until ScctControlsSave)"), Settings->MouseScale);
}

void AScctPlayerController::ScctBind(const FString& Side, const FString& Key, const FString& Command)
{
	const bool bMerc = Side.Equals(TEXT("merc"), ESearchCase::IgnoreCase);
	if (!bMerc && !Side.Equals(TEXT("spy"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctBind: the side is spy or merc, not '%s'"), *Side);
		return;
	}

	UScctControlSettings* Settings = UScctControlSettings::Get();
	if (Settings->SetBinding(bMerc, false, Key, Command))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Bindings: the %s's %s is now \"%s\" (not saved until ScctControlsSave)"),
			bMerc ? TEXT("merc") : TEXT("spy"), *Key, *Settings->GetBinding(bMerc, false, Key));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctBind: %s"), *Settings->GetLastError());
	}
}

void AScctPlayerController::ScctBindReset(const FString& Side)
{
	const bool bMerc = Side.Equals(TEXT("merc"), ESearchCase::IgnoreCase);
	UScctControlSettings::Get()->ResetBindings(bMerc);
	UE_LOG(LogOpenSCCT, Log, TEXT("Bindings: the %s's keys and controller are the defaults again (not saved until ScctControlsSave)"), bMerc ? TEXT("merc") : TEXT("spy"));
}

void AScctPlayerController::ScctControlsSave()
{
	UScctControlSettings* Settings = UScctControlSettings::Get();
	Settings->Save();
	Settings->SaveBindings();
}

void AScctPlayerController::ScctControlsLoad()
{
	UScctControlSettings* Settings = UScctControlSettings::Get();
	const bool bSettings = Settings->Load();
	const bool bBindings = Settings->LoadBindings();
	UE_LOG(LogOpenSCCT, Log, TEXT("Control settings read again (settings %s, bindings %s): invert Y %d, mouse scale %.2f"),
		bSettings ? TEXT("ok") : TEXT("refused"), bBindings ? TEXT("ok") : TEXT("refused"), Settings->bInvertY ? 1 : 0, Settings->MouseScale);
}
