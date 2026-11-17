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
#include "UnrealClient.h"

namespace
{
	/** The simulation turns in units of 65536 to the circle, 3 of them per mouse count at a multiplier of 1. */
	constexpr float RotationUnitsPerCount = 3.0f;
	constexpr float RotationUnitsPerDegree = 65536.0f / 360.0f;

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

	Super::PlayerTick(DeltaTime);
}

bool AScctPlayerController::HasPlayInput() const
{
	// Nobody is at the keyboard of an autoplay run and its window never has focus: the script is the player.
	if (Autoplay.HasSteps())
	{
		return true;
	}

	// A menu that shows the cursor has the mouse, and a text field or the console takes the keyboard focus from
	// the viewport: in both cases the keys are not the pawn's.
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameViewportClient* Client = LocalPlayer != nullptr ? LocalPlayer->ViewportClient.Get() : nullptr;
	const FViewport* Viewport = Client != nullptr ? Client->Viewport : nullptr;
	return Viewport != nullptr && Viewport->HasFocus() && !bShowMouseCursor;
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
	Raw.LeftX = Input->GetRawKeyValue(EKeys::Gamepad_LeftX);
	Raw.LeftY = Input->GetRawKeyValue(EKeys::Gamepad_LeftY);
	Raw.RightX = Input->GetRawKeyValue(EKeys::Gamepad_RightX);
	Raw.RightY = Input->GetRawKeyValue(EKeys::Gamepad_RightY);
	Raw.LeftTrigger = Input->GetRawKeyValue(EKeys::Gamepad_LeftTriggerAxis);
	Raw.RightTrigger = Input->GetRawKeyValue(EKeys::Gamepad_RightTriggerAxis);
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

	if (PlayerCameraManager != nullptr)
	{
		// Cut stays set for every frame drawn until the next tick, and all through a cinematic camera: only its
		// first frame is the cut, the rest continue it and keep their motion vectors and history.
		if (LookView.Cut != 0 && !bWasCut)
		{
			PlayerCameraManager->SetGameCameraCutThisFrame();
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

	if (bHadLookView && LookView.Cut == 0)
	{
		const float Turn = FMath::Max(
			FMath::Abs(FMath::FindDeltaAngleDegrees(Before.Yaw, LookView.Yaw)),
			FMath::Abs(FMath::FindDeltaAngleDegrees(Before.Pitch, LookView.Pitch)));
		const float Move = FVector::Dist(FVector(Before.X, Before.Y, Before.Z), FVector(LookView.X, LookView.Y, LookView.Z));
		LargestViewTurn = FMath::Max(LargestViewTurn, Turn);
		LargestViewMove = FMath::Max(LargestViewMove, Move);
	}
	bHadLookView = true;
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
			Hud = FString::Printf(TEXT(" | state %s clip %d ammo %d"), SimState, State.ClipAmmo, State.Ammo);
		}
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Autoplay: %s | %s | view at (%.0f %.0f %.0f) yaw %.2f pitch %.2f fov %.1f (drawn %.1f wide, %.1f high) aim blend %.2f%s | since the last line the view turned at most %.3f deg and moved at most %.2f cm between two frames"),
		*Label, *Body, LookView.X, LookView.Y, LookView.Z, LookView.Yaw, LookView.Pitch, LookView.Fov,
		LookView.HorizontalFov, LookView.VerticalFov, LookView.RifleAimBlend, *Hud, LargestViewTurn, LargestViewMove);
	LargestViewTurn = 0.0f;
	LargestViewMove = 0.0f;
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
