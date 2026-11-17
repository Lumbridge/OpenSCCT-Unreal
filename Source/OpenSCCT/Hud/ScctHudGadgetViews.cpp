#include "Engine/World.h"
#include "Hud/ScctHudSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctHudGadgetApi.h"

// The HUD subsystem's part for the gadget views: what the sticky cam and mine menus and the sticky cam and camera
// network pages draw from beyond the HUD state (the camera's zoom and gas, the names of the keys in the key help),
// and the unattended -ScctGadgetMenu check. The painting is in ScctHudElementsGadgets.cpp.

namespace
{
	/** The display name of the first key the current side has on Command, or empty. */
	FString KeyName(const AScctPlayerController& Player, const TCHAR* Command)
	{
		const TArray<FKey> Keys = Player.FindKeysBoundTo(Command);
		return Keys.Num() > 0 ? Keys[0].GetDisplayName().ToString() : FString();
	}

	/**
	 * The wheel notch that zooms a camera: the camera views take the wheel whatever the walking binding makes of it
	 * (the stock WheelUp, or NextGadget / Run once the wheel setting rewrote it), so the key is looked for under each
	 * name, the wheel itself preferred.
	 */
	FString WheelKeyName(const AScctPlayerController& Player, bool bUp)
	{
		const TCHAR* Commands[] = { bUp ? TEXT("WheelUp") : TEXT("WheelDown"), bUp ? TEXT("NextGadget") : TEXT("PreviousGadget"), bUp ? TEXT("Run") : TEXT("Walk") };
		const FKey Wheel = bUp ? EKeys::MouseScrollUp : EKeys::MouseScrollDown;
		FKey Found;
		for (const TCHAR* Command : Commands)
		{
			const TArray<FKey> Keys = Player.FindKeysBoundTo(Command);
			if (Keys.Contains(Wheel))
			{
				return Wheel.GetDisplayName().ToString();
			}
			if (!Found.IsValid() && Keys.Num() > 0)
			{
				Found = Keys[0];
			}
		}
		return Found.IsValid() ? Found.GetDisplayName().ToString() : FString();
	}

	/** Two key names as the original's key help joins them ("Mouse Wheel Up / Mouse Wheel Down"). */
	FString KeyPair(const FString& First, const FString& Second)
	{
		if (First.IsEmpty() || Second.IsEmpty())
		{
			return First.IsEmpty() ? Second : First;
		}
		return First + TEXT(" / ") + Second;
	}
}

void UScctHudSubsystem::InitGadgetViews()
{
	// -ScctGadgetMenu=sticky|mine[,At[,Hold[,Down+Down...]]]
	FString Switch;
	TArray<FString> Parts;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctGadgetMenu="), Switch, false) && Switch.ParseIntoArray(Parts, TEXT(",")) > 0)
	{
		GadgetMenuCheck.Menu = Parts[0].Equals(TEXT("sticky"), ESearchCase::IgnoreCase) ? EScctHudGadgetMenu::StickyCam
			: Parts[0].Equals(TEXT("mine"), ESearchCase::IgnoreCase) ? EScctHudGadgetMenu::Mine : 0;
		GadgetMenuCheck.At = Parts.IsValidIndex(1) ? FCString::Atof(*Parts[1]) : GadgetMenuCheck.At;
		GadgetMenuCheck.Hold = Parts.IsValidIndex(2) ? FCString::Atof(*Parts[2]) : GadgetMenuCheck.Hold;
		if (Parts.IsValidIndex(3))
		{
			TArray<FString> Steps;
			Parts[3].ParseIntoArray(Steps, TEXT("+"));
			for (const FString& Step : Steps)
			{
				GadgetMenuCheck.DownSteps.Add(FCString::Atof(*Step));
			}
		}
		if (GadgetMenuCheck.Menu == 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctGadgetMenu: '%s' is not a menu (sticky or mine)"), *Parts[0]);
		}
	}

	// -ScctGadgetView=sticky|camnet[,At[,Stay]]
	Parts.Reset();
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctGadgetView="), Switch, false) && Switch.ParseIntoArray(Parts, TEXT(",")) > 0)
	{
		GadgetViewCheck.Kind = Parts[0].Equals(TEXT("sticky"), ESearchCase::IgnoreCase) ? EScctHudGadgetKind::StickyCam
			: Parts[0].Equals(TEXT("camnet"), ESearchCase::IgnoreCase) ? EScctHudGadgetKind::CamNetwork : 0;
		GadgetViewCheck.At = Parts.IsValidIndex(1) ? FCString::Atof(*Parts[1]) : GadgetViewCheck.At;
		GadgetViewCheck.Stay = Parts.IsValidIndex(2) ? FCString::Atof(*Parts[2]) : GadgetViewCheck.Stay;
		if (GadgetViewCheck.Kind == 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctGadgetView: '%s' is not a gadget view (sticky or camnet)"), *Parts[0]);
		}
	}
}

void UScctHudSubsystem::ReadGadgetViews()
{
	FScctHudModel& M = *Model;
	const FScctHudGadgetApi* Gadgets = ScctFeature<FScctHudGadgetApi>();
	M.CameraView = {};
	if (Gadgets != nullptr && M.bHasState && M.State.GadgetView != 0)
	{
		Gadgets->GetCameraView(&M.CameraView);
	}

	// The key help follows the bindings, which the player can change while playing: read again on entering a view
	// and every two seconds while in one.
	const int32 View = M.bHasState ? M.State.GadgetView : 0;
	if (View != 0 && (View != LastGadgetView || M.Clock - GadgetKeysAge > 2.0f))
	{
		GadgetKeysAge = M.Clock;
		if (const AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			// A binding is matched action by action, as the controls table holds it ("InvButton bUsing").
			M.GadgetKeys.Zoom = KeyPair(WheelKeyName(*Player, true), WheelKeyName(*Player, false));
			M.GadgetKeys.Use = KeyName(*Player, TEXT("InvButton bUsing"));
			M.GadgetKeys.Cameras = KeyPair(M.GadgetKeys.Use, KeyName(*Player, TEXT("InvButton bUseBackToWall")));
			M.GadgetKeys.Exit = KeyName(*Player, TEXT("AltFire"));
		}
	}
	if (View != LastGadgetView)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: gadget view %d -> %d (camera fov %.0f of %.0f..%.0f, gas %d; keys zoom '%s', use '%s', exit '%s')"),
			LastGadgetView, View, M.CameraView.Fov, M.CameraView.MinFov, M.CameraView.MaxFov, M.CameraView.GasDoses,
			*M.GadgetKeys.Zoom, *M.GadgetKeys.Use, *M.GadgetKeys.Exit);
		LastGadgetView = View;
	}

	const bool bAlert = View == 2 && M.CamNetworkAlert();
	if (bAlert != bLastCamAlert)
	{
		bLastCamAlert = bAlert;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: CamNetworkAlert %s on camera '%s'%s"), bAlert ? TEXT("up") : TEXT("down"),
			*M.Text(M.State.CamNetworkNameId), M.bPreviewCamAlert ? TEXT(" (PreviewCamAlert)") : TEXT(""));
	}

	// The menu's rows as they change, to check a screenshot against.
	uint32 Signature = static_cast<uint32>(M.MenuLines.Num());
	for (const FScctHudMenuLine& Line : M.MenuLines)
	{
		Signature = HashCombine(Signature, HashCombine(static_cast<uint32>(Line.TextId), static_cast<uint32>(Line.Look + 1)));
	}
	if (Signature != LastMenuSignature)
	{
		LastMenuSignature = Signature;
		FString Rows;
		static const TCHAR* Looks[] = { TEXT("normal"), TEXT("selected"), TEXT("unavailable"), TEXT("untouched") };
		for (const FScctHudMenuLine& Line : M.MenuLines)
		{
			Rows += FString::Printf(TEXT("%s'%s' %s"), Rows.IsEmpty() ? TEXT("") : TEXT(" | "), *M.Text(Line.TextId), Looks[FMath::Clamp(Line.Look, 0, 3)]);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: menu %s"), M.MenuLines.Num() > 0 ? *Rows : TEXT("closed"));
	}
}

void UScctHudSubsystem::RunGadgetChecks(float DeltaSeconds)
{
	if (!Model->bHasState || (Model->State.Flags & EScctHudFlags::HasPawn) == 0)
	{
		return;
	}
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	const FScctHudGadgetApi* Gadgets = ScctFeature<FScctHudGadgetApi>();
	if (Player == nullptr || Gadgets == nullptr)
	{
		return;
	}
	const float Now = Model->Clock;

	FGadgetMenuCheck& Menu = GadgetMenuCheck;
	if (Menu.Menu != 0 && Menu.Stage == 0 && Now >= Menu.At)
	{
		// The menu first, then the key: the Alt-fire binding also carries a gadget quick choice, which would take
		// another gadget outside the menu (the menus ignore it). The bridge holds Alt-fire for the next tick, and
		// the key pressed now is down by the frame after.
		const bool bIn = Gadgets->ForceMenu(Menu.Menu) != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctGadgetMenu: %s menu at %.1f s: %s"), Menu.Menu == EScctHudGadgetMenu::StickyCam ? TEXT("sticky cam") : TEXT("mine"),
			Now, bIn ? TEXT("the controller is in the menu, Alt-fire held") : TEXT("refused (the pawn has no such gadget)"));
		if (bIn)
		{
			Player->HoldScriptedAction(TEXT("altfire"), true);
		}
		Menu.Stage = bIn ? 1 : 2;
	}
	else if (Menu.Stage == 1)
	{
		if (Menu.DownSteps.IsValidIndex(Menu.NextStep) && Now >= Menu.DownSteps[Menu.NextStep])
		{
			// The down key of the bindings (BT_Down) steps the selection on its press, as under a finger.
			++Menu.NextStep;
			Player->TapBoundCommand(TEXT("Button BT_Down"));
		}
		if (Now >= Menu.At + Menu.Hold)
		{
			Player->HoldScriptedAction(TEXT("altfire"), false);
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctGadgetMenu: Alt-fire up at %.1f s"), Now);
			Menu.Stage = 2;
		}
	}

	// The view by the player's keys. Stages: 1 selected, 2 weapon key down (spy), 3 weapon key up, 4 Alt-fire down,
	// 5 Alt-fire up (in the view), 6 Alt-fire down to leave, 7 done.
	FGadgetViewCheck& View = GadgetViewCheck;
	const auto Next = [&View, Now](int32 Stage) { View.Stage = Stage; View.StageAt = Now; };
	const float InStage = Now - View.StageAt;
	const bool bSpy = View.Kind == EScctHudGadgetKind::StickyCam;
	switch (View.Kind != 0 ? View.Stage : -1)
	{
	case 0:
		if (Now >= View.At)
		{
			const bool bSelected = Gadgets->SelectGadget(View.Kind) != 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctGadgetView: %s selected at %.1f s: %s"), bSpy ? TEXT("sticky cam") : TEXT("camera network"), Now, bSelected ? TEXT("yes") : TEXT("the pawn has no such gadget"));
			Next(bSelected ? 1 : 7);
		}
		break;
	case 1:
		if (InStage >= 0.5f)
		{
			// The spy draws the Shocker (the cam leaves from its muzzle); holding the weapon key draws it without the laser.
			if (bSpy)
			{
				Player->HoldScriptedAction(TEXT("weapon"), true);
			}
			Next(2);
		}
		break;
	case 2:
		if (InStage >= 0.8f)
		{
			if (bSpy)
			{
				Player->HoldScriptedAction(TEXT("weapon"), false);
			}
			Next(3);
		}
		break;
	case 3:
		if (InStage >= (bSpy ? 1.2f : 0.2f))
		{
			Player->HoldScriptedAction(TEXT("altfire"), true);
			Next(4);
		}
		break;
	case 4:
		if (InStage >= 0.15f)
		{
			Player->HoldScriptedAction(TEXT("altfire"), false);
			Next(5);
		}
		break;
	case 5:
		if (InStage >= View.Stay)
		{
			Player->HoldScriptedAction(TEXT("altfire"), true);
			Next(6);
		}
		break;
	case 6:
		if (InStage >= 0.15f)
		{
			Player->HoldScriptedAction(TEXT("altfire"), false);
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctGadgetView: Alt-fire to leave at %.1f s"), Now);
			Next(7);
		}
		break;
	default:
		break;
	}
}