#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/ScctHudTextures.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctBridgeApi.h"
#include "Runtime/ScctHudBinocularApi.h"
#include "Runtime/ScctHudMotionApi.h"
#include "SceneView.h"
#include "UnrealClient.h"

// The HUD subsystem's part for the merc's motion tracking: each frame the view Unreal draws (the camera, its projection
// and the full-screen layer's size) goes to the bridge's motion table, which projects the locks of the goggles' tracker
// and of the HUD's own (laser) tracker and builds their draw list; the rows come back into the model. The log gets a
// line when the locks change and one with each snapshot line, and the timed commands Summon and AimMate bring a moving
// target in front of the merc for unattended checks. The painting is in ScctHudElementsMotion.cpp.

namespace
{
	/** The content's pictures the tracker draws with. */
	const TCHAR* const MotionTextures[] = { TEXT("Vision_lock"), TEXT("balayage_H"), TEXT("Vision_friend") };

	constexpr int32 MotionRowCapacity = 256;
	constexpr int32 MotionLockCapacity = 32;
}

void UScctHudSubsystem::BuildMotion()
{
	FScctHudModel& M = *Model;
	M.Motion = FScctHudMotionState{};
	M.MotionRows.Reset();
	const FScctHudMotionApi* Motion = ScctFeature<FScctHudMotionApi>();
	if (Motion == nullptr)
	{
		if (!bMotionTableMissingLogged && M.bHasState)
		{
			bMotionTableMissingLogged = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the bridge has no motion table; the merc's motion tracking draws no lock boxes"));
		}
		return;
	}

	// The tracker is drawn on the merc's own page (not through the rifle scope, which covers the picture and where the laser name pass does not run either, nor while an EMP has
	// his HUD shut down) and on the camera network
	// page while the camera works, as the Godot client's HudLayer draws it; the post-process of motion vision is the
	// vision subsystem's.
	const FScctHudState& S = M.State;
	int32 Page = 0;
	if (M.bHasState && S.GadgetView == 2 && S.CamNetworkDisabledSeconds <= 0
		&& (S.Flags & (EScctHudFlags::HideHud | EScctHudFlags::GameEnded)) == 0)
	{
		Page = 2;
	}
	else if (ScctHudElements::ShowsPlayerHud(M) && S.Side == 1 && S.BinocularMode == 0 && (S.EffectFlags & EScctHudEffectFlags::ShutDown) == 0)
	{
		Page = 1;
	}

	// The view as Unreal projects it for the camera as it stands (the same projection ProjectWorldLocationToScreen uses),
	// in the full-screen layer's units: viewport pixels over the DPI scale.
	FScctHudMotionView View = {};
	View.Page = Page;
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const ULocalPlayer* LocalPlayer = Player != nullptr ? Player->GetLocalPlayer() : nullptr;
	FViewport* Viewport = LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr ? LocalPlayer->ViewportClient->Viewport : nullptr;
	FSceneViewProjectionData Projection;
	if (Page != 0 && Viewport != nullptr && ViewportSize.X > 0 && ViewportSize.Y > 0 && LocalPlayer->GetProjectionData(Viewport, Projection))
	{
		const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
		const FIntRect Rect = Projection.GetConstrainedViewRect();
		FVector Location;
		FRotator Rotation;
		Player->GetPlayerViewPoint(Location, Rotation);
		View.X = static_cast<float>(Projection.ViewOrigin.X);
		View.Y = static_cast<float>(Projection.ViewOrigin.Y);
		View.Z = static_cast<float>(Projection.ViewOrigin.Z);
		View.Pitch = static_cast<float>(Rotation.Pitch);
		View.Yaw = static_cast<float>(Rotation.Yaw);
		View.Roll = static_cast<float>(Rotation.Roll);
		View.FocalX = static_cast<float>(Projection.ProjectionMatrix.M[0][0]) * Rect.Width() * 0.5f / Scale;
		View.FocalY = static_cast<float>(Projection.ProjectionMatrix.M[1][1]) * Rect.Height() * 0.5f / Scale;
		View.CentreX = (Rect.Min.X + Rect.Width() * 0.5f) / Scale;
		View.CentreY = (Rect.Min.Y + Rect.Height() * 0.5f) / Scale;
		View.Width = ViewportSize.X / Scale;
		View.Height = ViewportSize.Y / Scale;
	}
	else
	{
		View.Page = 0;
	}

	M.MotionRows.SetNumUninitialized(MotionRowCapacity, EAllowShrinking::No);
	const int32 Count = Motion->Build(&View, &M.Motion, M.MotionRows.GetData(), MotionRowCapacity);
	M.MotionRows.SetNum(FMath::Clamp(Count, 0, MotionRowCapacity), EAllowShrinking::No);

	// Once a session, when the first rows come: say which of the tracker's pictures the content lacks.
	if (Count > 0 && !bMotionTableMissingLogged)
	{
		bMotionTableMissingLogged = true;
		for (const TCHAR* Name : MotionTextures)
		{
			if (UScctHudTextures::Find(TEXT("SHudTextures"), Name) == nullptr)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHud: the content has no SHudTextures.%s; the motion tracking draws that part as plain lines"), Name);
			}
		}
	}

	// AimMate: the view and aim stay on the nearest teammate on the radar (the laser gadget's target).
	if (M.Clock < AimMateUntil && M.bHasState)
	{
		const FScctHudBlip* Mate = nullptr;
		float Best = TNumericLimits<float>::Max();
		for (const FScctHudBlip& Blip : M.Radar)
		{
			const float Distance = FVector2f(Blip.X - S.X, Blip.Y - S.Y).SizeSquared();
			if (Blip.Kind == 0 && Distance < Best)
			{
				Best = Distance;
				Mate = &Blip;
			}
		}
		const FScctHudBinocularApi* Binoculars = ScctFeature<FScctHudBinocularApi>();
		if (Mate != nullptr && Binoculars != nullptr)
		{
			Binoculars->AimAt(Mate->X, Mate->Y, Mate->Z);
		}
	}

	// A line when the locks change (which actors, which tracker, friend or not).
	uint32 Signature = GetTypeHash(M.Motion.Tracking) ^ (GetTypeHash(M.Motion.Drawn) << 4) ^ (GetTypeHash(M.Motion.Friends) << 8) ^ (GetTypeHash(M.Motion.NameRow) << 12);
	FScctHudMotionLock Locks[MotionLockCapacity];
	const int32 LockCount = FMath::Clamp(Motion->GetLocks(Locks, MotionLockCapacity), 0, MotionLockCapacity);
	for (int32 Index = 0; Index < LockCount; ++Index)
	{
		Signature = HashCombine(Signature, HashCombine(GetTypeHash(Locks[Index].ActorId), GetTypeHash(Locks[Index].Laser * 4 + Locks[Index].Friend * 2 + Locks[Index].InFront)));
	}
	if (Signature != LastMotionSignature)
	{
		LastMotionSignature = Signature;
		LogMotion(true);
	}
}

bool UScctHudSubsystem::RunMotionCommand(const FString& Name, int32 Argument)
{
	if (Name.Equals(TEXT("Summon"), ESearchCase::IgnoreCase))
	{
		const FScctHudMotionApi* Motion = ScctFeature<FScctHudMotionApi>();
		const bool bQueued = Motion != nullptr && Motion->Summon(Argument != 0 ? 1 : 0) != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Summon %s %s"), Argument != 0 ? TEXT("teammate") : TEXT("enemy"),
			bQueued ? TEXT("queued on the local server") : TEXT("refused (no local server, no pawn or no motion table)"));
		return true;
	}
	if (Name.Equals(TEXT("Laser"), ESearchCase::IgnoreCase))
	{
		// The merc's laser gadget on (1) or off (0) by the simulation's own GadLaserSwitch, sent only when it is not
		// already so (the player's default key for it also presses the roll button, so a key press would do more than this).
		const bool bOn = Model->bHasState && (Model->State.Flags & EScctHudFlags::LaserOn) != 0;
		const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
		const FScctBridgeApi* Bridge = Session != nullptr ? Session->GetBridge() : nullptr;
		if (bOn != (Argument != 0) && Bridge != nullptr)
		{
			Bridge->Command(TEXT("GadLaserSwitch"), 0);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Laser %d (it was %s%s)"), Argument, bOn ? TEXT("on") : TEXT("off"),
			Bridge == nullptr ? TEXT("; no session to send it to") : TEXT(""));
		return true;
	}
	if (Name.Equals(TEXT("AimMate"), ESearchCase::IgnoreCase))
	{
		AimMateUntil = Model->Clock + FMath::Max(Argument, 0);
		return true;
	}
	return false;
}

void UScctHudSubsystem::LogMotion(bool bChange)
{
	const FScctHudModel& M = *Model;
	const FScctHudMotionApi* Motion = ScctFeature<FScctHudMotionApi>();
	if (Motion == nullptr || (!bChange && M.Motion.Tracking == 0 && M.Motion.Locks == 0 && M.Motion.LaserLocks == 0))
	{
		return;
	}

	// Each lock as #actor:side(s spy, m merc, p other)[L laser][F friend][B behind][R fading] speed box.
	FScctHudMotionLock Locks[MotionLockCapacity];
	const int32 LockCount = FMath::Clamp(Motion->GetLocks(Locks, MotionLockCapacity), 0, MotionLockCapacity);
	FString Rows;
	for (int32 Index = 0; Index < LockCount && Rows.Len() < 400; ++Index)
	{
		const FScctHudMotionLock& Lock = Locks[Index];
		Rows += FString::Printf(TEXT(" #%d:%s%s%s%s%s v%.0f (%.0f,%.0f)-(%.0f,%.0f)"), Lock.ActorId,
			Lock.Side == 0 ? TEXT("s") : Lock.Side == 1 ? TEXT("m") : TEXT("p"), Lock.Laser ? TEXT("L") : TEXT(""), Lock.Friend ? TEXT("F") : TEXT(""),
			Lock.InFront ? TEXT("") : TEXT("B"), Lock.Removing ? TEXT("R") : TEXT(""), Lock.Speed, Lock.X1, Lock.Y1, Lock.X2, Lock.Y2);
	}
	// The check of the bridge's projection: the first lock in front, its actor's location put on the screen by Unreal's
	// own ProjectWorldLocationToScreen, against the middle of its box (which centres on that point up to perspective).
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GetWorld()), 0.01f);
	for (int32 Index = 0; Index < LockCount && Player != nullptr; ++Index)
	{
		const FScctHudMotionLock& Lock = Locks[Index];
		FVector2D Screen;
		if (Lock.InFront && Player->ProjectWorldLocationToScreen(FVector(Lock.WorldX, Lock.WorldY, Lock.WorldZ), Screen, false))
		{
			Rows += FString::Printf(TEXT(" | check #%d: Unreal puts it at (%.0f,%.0f), box middle (%.0f,%.0f)"), Lock.ActorId,
				Screen.X / Scale, Screen.Y / Scale, (Lock.X1 + Lock.X2) * 0.5f, (Lock.Y1 + Lock.Y2) * 0.5f);
			break;
		}
	}
	int32 Kinds[4] = {};
	for (const FScctHudMotionRow& Row : M.MotionRows)
	{
		++Kinds[FMath::Clamp(Row.Kind, 0, 3)];
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: motion%s tracking %d vision %d locks %d laser %d drawn %d friends %d | rows %d of %d (corners %d, sweeps %d, friend marks %d, texts %d) name %d frame x%.2f |%s"),
		bChange ? TEXT(" change:") : TEXT(":"), M.Motion.Tracking, M.State.VisionMode, M.Motion.Locks, M.Motion.LaserLocks, M.Motion.Drawn, M.Motion.Friends,
		M.Motion.Rows, M.Motion.TotalRows, Kinds[0], Kinds[1], Kinds[2], Kinds[3], M.Motion.NameRow, M.Motion.FrameScale, *Rows);
}
