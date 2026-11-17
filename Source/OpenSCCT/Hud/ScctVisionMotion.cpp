#include "Hud/ScctVisionMotion.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Hud/ScctVisionData.h"
#include "Hud/ScctVisionDebug.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"

namespace
{
	/** As many clean rectangles as the material has (the original's post pass took eight too). */
	constexpr int32 MaxCleanRects = 8;

	const FName SlopeParameter(TEXT("RampSlope"));
	const FName OffsetParameter(TEXT("RampOffset"));
	const FName CountParameter(TEXT("CleanCount"));

	/** LevelInfo's class defaults, for a frame without the level's values. */
	constexpr int32 DefaultMtvSlope = 85;
	constexpr int32 DefaultMtvOffset = 52;

	/** How long the standing bot of -ScctVisionBots is held still. */
	constexpr float BotHoldSeconds = 20.0f;

	FName RectParameter(int32 Index)
	{
		static const FName Names[MaxCleanRects] = {
			TEXT("Rect0"), TEXT("Rect1"), TEXT("Rect2"), TEXT("Rect3"), TEXT("Rect4"), TEXT("Rect5"), TEXT("Rect6"), TEXT("Rect7"),
		};
		return Names[Index];
	}
}

FScctVisionMotion::FScctVisionMotion()
{
	// -ScctVisionBots="8;11;14"
	FString Times;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctVisionBots="), Times, false))
	{
		TArray<FString> Parts;
		Times.ParseIntoArray(Parts, TEXT(";"));
		for (const FString& Part : Parts)
		{
			BotTimes.Add(FCString::Atof(*Part));
		}
		BotTimes.Sort();
	}
}

void FScctVisionMotion::Reset()
{
	MatchSeconds = 0.0f;
	LastCount = -1;
	LastSlope = -1;
	LastOffset = -1;
}

void FScctVisionMotion::Update(UWorld* World, FScctVisionData& Data, UMaterialInstanceDynamic* Material, bool bShown, float DeltaSeconds)
{
	RunDebugBots(DeltaSeconds, Data.GetPawns().Num() > 0);
	if (!bShown || Material == nullptr || World == nullptr)
	{
		return;
	}

	// The level's ramp: s = 1 + 3 MTVSlope / 255, the shader takes 2 s; and MTVOffset / 255.
	const bool bLevel = Data.HasLevel();
	const int32 MtvSlope = bLevel ? Data.GetLevel().MtvSlope : DefaultMtvSlope;
	const int32 MtvOffset = bLevel ? Data.GetLevel().MtvOffset : DefaultMtvOffset;
	Material->SetScalarParameterValue(SlopeParameter, 2.0f * (1.0f + 3.0f * MtvSlope / 255.0f));
	Material->SetScalarParameterValue(OffsetParameter, MtvOffset / 255.0f);

	// The tracker's locks boxed from what the player sees, in the 640 x 480 frame laid on the screen as the original's
	// tracker frame is (MotionHudFrame without the stretch: 480 high, centred at x 320, laid out at most 16:9 wide and
	// stretched wider beyond that).
	int32 Count = 0;
	FLinearColor Rects[MaxCleanRects];
	TArray<FString, TInlineAllocator<MaxCleanRects>> RectLines;
	const APlayerController* Controller = World->GetFirstPlayerController();
	const APlayerCameraManager* Camera = Controller != nullptr ? Controller->PlayerCameraManager.Get() : nullptr;
	int32 Width = 0;
	int32 Height = 0;
	if (Controller != nullptr)
	{
		Controller->GetViewportSize(Width, Height);
	}
	if (Camera != nullptr && Width > 0 && Height > 0 && Data.GetApi() != nullptr)
	{
		const float W = static_cast<float>(Width);
		const float H = static_cast<float>(Height);
		const float ScaleY = H / 480.0f;
		const float Layout = FMath::Min(W, H * 16.0f / 9.0f);
		const float ScaleX = ScaleY * (W / Layout);
		const float OffsetX = W * 0.5f - 320.0f * ScaleX;

		const FVector Eye = Camera->GetCameraLocation();
		const FRotator View = Camera->GetCameraRotation();
		FScctVisionCamera Frame = {};
		Frame.X = Eye.X;
		Frame.Y = Eye.Y;
		Frame.Z = Eye.Z;
		Frame.Pitch = View.Pitch;
		Frame.Yaw = View.Yaw;
		Frame.Roll = View.Roll;
		Frame.FovX = Camera->GetFOVAngle();
		Frame.ScreenWidth = W;
		Frame.ScreenHeight = H;
		Frame.FrameScaleX = ScaleX;
		Frame.FrameScaleY = ScaleY;
		Frame.FrameOffsetX = OffsetX;

		for (const FScctVisionLock& Lock : Data.ReadMotionLocks(Frame))
		{
			if ((Lock.Flags & EScctVisionLockFlags::Clean) == 0 || Count >= MaxCleanRects)
			{
				continue;
			}
			const FLinearColor Rect(
				(OffsetX + Lock.X1 * ScaleX) / W, Lock.Y1 * ScaleY / H,
				(OffsetX + Lock.X2 * ScaleX) / W, Lock.Y2 * ScaleY / H);
			Material->SetVectorParameterValue(RectParameter(Count), Rect);
			Rects[Count] = Rect;
			RectLines.Add(FString::Printf(TEXT("clean rect %d round #%d: u %.3f..%.3f v %.3f..%.3f (frame x %.0f..%.0f y %.0f..%.0f)"),
				Count, Lock.ActorId, Rect.R, Rect.B, Rect.G, Rect.A, Lock.X1, Lock.X2, Lock.Y1, Lock.Y2));
			++Count;
		}
	}
	Material->SetScalarParameterValue(CountParameter, static_cast<float>(Count));

	const double Now = FPlatformTime::Seconds();
	if (Count != LastCount || MtvSlope != LastSlope || MtvOffset != LastOffset || Now - LastLog > 3.0)
	{
		const int32 Locks = bLevel ? Data.GetLevel().LockCount : 0;
		const bool bTracker = bLevel && (Data.GetLevel().Flags & EScctVisionLevelFlags::HasTracker) != 0;
		const bool bActive = bLevel && (Data.GetLevel().Flags & EScctVisionLevelFlags::TrackerActive) != 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: motion ramp slope/offset %d/%d%s, %d clean rects (tracker %s, %d locks)"),
			MtvSlope, MtvOffset, bLevel ? TEXT("") : TEXT(" (class defaults, no level values)"), Count,
			bTracker ? (bActive ? TEXT("scanning") : TEXT("off")) : TEXT("none"), Locks);
		for (const FString& Line : RectLines)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   %s"), *Line);
		}

		// Each pawn in front of the camera: whether the simulation has it moving for the tracker and whether a clean
		// rectangle holds it, so a run's log shows a still pawn in view left in the ramp.
		const UScctSessionSubsystem* Session = World->GetSubsystem<UScctSessionSubsystem>();
		if (Session != nullptr && Controller != nullptr)
		{
			for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Session->GetProxies())
			{
				const AScctPawnProxy* Proxy = Pair.Value;
				const FScctVisionPawn* Row = IsValid(Proxy) ? Data.FindPawn(Proxy->GetActorId()) : nullptr;
				FVector2D Screen;
				if (Row == nullptr || Width <= 0 || Height <= 0
					|| !Controller->ProjectWorldLocationToScreen(Proxy->GetActorLocation(), Screen, true))
				{
					continue;
				}
				const FVector2D Uv(Screen.X / Width, Screen.Y / Height);
				if (Uv.X < 0.0 || Uv.X > 1.0 || Uv.Y < 0.0 || Uv.Y > 1.0)
				{
					continue;
				}
				int32 InRect = INDEX_NONE;
				for (int32 Index = 0; Index < Count && InRect == INDEX_NONE; ++Index)
				{
					InRect = Uv.X >= Rects[Index].R && Uv.X < Rects[Index].B && Uv.Y >= Rects[Index].G && Uv.Y < Rects[Index].A ? Index : INDEX_NONE;
				}
				const float Distance = Controller->PlayerCameraManager != nullptr
					? FVector::Dist(Controller->PlayerCameraManager->GetCameraLocation(), Proxy->GetActorLocation()) : 0.0f;
				UE_LOG(LogOpenSCCT, Log, TEXT("Vision:   pawn #%d in view at u %.2f v %.2f, %.0f cm away, speed %.0f%s%s: %s"),
					Row->ActorId, Uv.X, Uv.Y, Distance, Row->Speed,
					(Row->Flags & EScctVisionPawnFlags::Moving) != 0 ? TEXT(" moving") : TEXT(" still"),
					(Row->Flags & EScctVisionPawnFlags::Tracked) != 0 ? TEXT(" tracked") : TEXT(""),
					InRect != INDEX_NONE ? *FString::Printf(TEXT("clean in rect %d"), InRect) : TEXT("in the ramp"));
			}
		}
		LastCount = Count;
		LastSlope = MtvSlope;
		LastOffset = MtvOffset;
		LastLog = Now;
	}
}

void FScctVisionMotion::RunDebugBots(float DeltaSeconds, bool bHasPawns)
{
	if (!bHasPawns || (BotTimes.Num() == 0 && DebugApi == nullptr))
	{
		return;
	}
	MatchSeconds += DeltaSeconds;
	if (DebugApi != nullptr)
	{
		// What the server did with the last request, once it has done it.
		TCHAR Report[512];
		const int32 Serial = DebugApi->GetReport(Report, UE_ARRAY_COUNT(Report));
		if (Serial != LastReport)
		{
			LastReport = Serial;
			UE_LOG(LogOpenSCCT, Log, TEXT("Vision: -ScctVisionBots on the server: %s"), Report);
		}
	}
	if (BotTimes.Num() == 0 || MatchSeconds < BotTimes[0])
	{
		return;
	}
	BotTimes.RemoveAt(0);

	if (DebugApi == nullptr && !bTriedDebugApi)
	{
		bTriedDebugApi = true;
		FString Error;
		DebugApi = static_cast<const FScctVisionDebugApi*>(FOpenSCCTModule::Get().FindFeatureTable(
			TEXT("OpenScct.UnrealBridge.VisionDebugExports"), sizeof(FScctVisionDebugApi), ScctVisionDebugApiVersion, Error));
		if (DebugApi == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Vision: -ScctVisionBots does nothing, the bridge's vision check entry points are missing: %s"), *Error);
		}
	}
	if (DebugApi != nullptr)
	{
		const int32 Queued = DebugApi->StandSpyBots(BotHoldSeconds);
		UE_LOG(LogOpenSCCT, Log, TEXT("Vision: -ScctVisionBots at %.1f s: %s"), MatchSeconds,
			Queued != 0 ? TEXT("a spy bot to walk across the merc's view and one to hold still asked of the server") : TEXT("no local server"));
	}
}
