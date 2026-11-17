#include "Play/ScctSessionSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnProxy.h"

namespace
{
	constexpr int32 MaxPawns = 64;
	constexpr int32 MaxBones = 128;
}

bool UScctSessionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScctSessionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctSessionSubsystem, STATGROUP_Tickables);
}

bool UScctSessionSubsystem::StartLocalMatch(const FString& Map, const FString& Mode, bool bMerc, int32 Bots)
{
	StopMatch();
	Failure.Reset();

	FString Error;
	Api = FOpenSCCTModule::Get().GetBridge(Error);
	if (Api == nullptr)
	{
		Fail(Error);
		return false;
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Starting %s (%s) as %s with %d bots"), *Map, *Mode, bMerc ? TEXT("merc") : TEXT("spy"), Bots);
	if (Api->StartLocal(*Map, *Mode, bMerc ? 1 : 0, Bots) == 0)
	{
		Fail(FOpenSCCTModule::Get().GetBridgeError());
		return false;
	}

	PawnBuffer.SetNumUninitialized(MaxPawns);
	PoseBuffer.SetNumUninitialized(MaxBones);
	for (int32 Kind = 0; Kind < UE_ARRAY_COUNT(Skeletons); ++Kind)
	{
		Skeletons[Kind].SetNumUninitialized(MaxBones);
		const int32 Count = Api->GetSkeleton(Kind, Skeletons[Kind].GetData(), MaxBones);
		Skeletons[Kind].SetNum(FMath::Clamp(Count, 0, MaxBones));
	}
	bPlaying = true;
	return true;
}

void UScctSessionSubsystem::StopMatch()
{
	if (Api != nullptr && bPlaying)
	{
		Api->Stop();
	}
	bPlaying = false;
	View = {};

	for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Proxies)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	Proxies.Reset();
}

void UScctSessionSubsystem::Deinitialize()
{
	StopMatch();
	Super::Deinitialize();
}

void UScctSessionSubsystem::SetInput(const FScctBridgeInput& Input)
{
	if (bPlaying)
	{
		Api->SetInput(&Input);
	}
}

void UScctSessionSubsystem::SendCommand(const TCHAR* Name, int32 Arg)
{
	if (bPlaying)
	{
		Api->Command(Name, Arg);
	}
}

void UScctSessionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickAutoShots(DeltaTime);
	if (!bPlaying)
	{
		return;
	}

	Api->Update();
	if (Api->GetState() == static_cast<int32>(EScctBridgeState::Failed))
	{
		Fail(FOpenSCCTModule::Get().GetBridgeError());
		return;
	}

	Api->GetView(&View);
	ApplyTourCamera();
	UpdatePawns();
}

void UScctSessionSubsystem::UpdatePawns()
{
	const int32 Count = Api->GetPawns(PawnBuffer.GetData(), PawnBuffer.Num());

	TSet<int32> Seen;
	Seen.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctBridgePawn& Pawn = PawnBuffer[Index];
		Seen.Add(Pawn.ActorId);

		TObjectPtr<AScctPawnProxy>& Proxy = Proxies.FindOrAdd(Pawn.ActorId);
		if (!IsValid(Proxy))
		{
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Parameters.ObjectFlags |= RF_Transient;
			Proxy = GetWorld()->SpawnActor<AScctPawnProxy>(FVector(Pawn.X, Pawn.Y, Pawn.Z), FRotator::ZeroRotator, Parameters);
		}
		if (IsValid(Proxy))
		{
			// The local player's own body is drawn only when the camera is outside it.
			const bool bLocal = (Pawn.Flags & EScctPawnFlags::Local) != 0;
			const bool bFirstPerson = bLocal && (Pawn.Kind == 1 || View.SpyFirstPerson != 0);
			const int32 Bones = FMath::Clamp(Api->GetPawnPose(Pawn.ActorId, PoseBuffer.GetData(), PoseBuffer.Num()), 0, PoseBuffer.Num());
			const TConstArrayView<FScctBridgeBoneInfo> Skeleton = Pawn.Kind >= 0 && Pawn.Kind < UE_ARRAY_COUNT(Skeletons)
				? TConstArrayView<FScctBridgeBoneInfo>(Skeletons[Pawn.Kind])
				: TConstArrayView<FScctBridgeBoneInfo>();
			Proxy->Present(Pawn, Skeleton, TConstArrayView<FScctBridgeBone>(PoseBuffer.GetData(), Bones), bFirstPerson);
		}
	}

	for (auto It = Proxies.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (IsValid(It.Value()))
			{
				It.Value()->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}

void UScctSessionSubsystem::Fail(const FString& Reason)
{
	Failure = Reason;
	UE_LOG(LogOpenSCCT, Error, TEXT("OpenSCCT match stopped: %s"), *Reason);
	StopMatch();
}

void UScctSessionSubsystem::TickAutoShots(float DeltaTime)
{
	if (ShotsLeft < 0)
	{
		// First tick: read the request once.
		ShotsLeft = 0;
		FParse::Value(FCommandLine::Get(), TEXT("ScctShots="), ShotsLeft);
		float Every = 5.0f;
		FParse::Value(FCommandLine::Get(), TEXT("ScctShotEvery="), Every);
		ShotTimer = Every;

		FString Cameras;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctCams="), Cameras, false))
		{
			TArray<FString> Entries;
			Cameras.ParseIntoArray(Entries, TEXT(";"));
			for (const FString& Entry : Entries)
			{
				TArray<FString> Numbers;
				Entry.ParseIntoArray(Numbers, TEXT(","));
				if (Numbers.Num() >= 5)
				{
					const FVector Location(FCString::Atof(*Numbers[0]), FCString::Atof(*Numbers[1]), FCString::Atof(*Numbers[2]));
					const FRotator Rotation(FCString::Atof(*Numbers[4]), FCString::Atof(*Numbers[3]), 0.0);
					TourCameras.Add(FTransform(Rotation, Location));
				}
			}
			bTourView = TourCameras.Num() > 0;
		}
	}

	if (QuitTimer >= 0.0f)
	{
		QuitTimer -= DeltaTime;
		if (QuitTimer < 0.0f)
		{
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	if (ShotsLeft <= 0)
	{
		return;
	}

	ShotTimer -= DeltaTime;
	if (ShotTimer > 0.0f)
	{
		return;
	}

	FString Directory = FPaths::ProjectSavedDir() / TEXT("ScctShots");
	FParse::Value(FCommandLine::Get(), TEXT("ScctShotDir="), Directory);
	const FString File = Directory / FString::Printf(TEXT("%s_%02d.png"), *GetWorld()->GetMapName(), ShotIndex++);
	FScreenshotRequest::RequestScreenshot(File, false, false);
	UE_LOG(LogOpenSCCT, Log, TEXT("Screenshot %s (view valid %d at %.0f %.0f %.0f, playing %d)"), *File, View.Valid, View.X, View.Y, View.Z, bPlaying ? 1 : 0);

	float Every = 5.0f;
	FParse::Value(FCommandLine::Get(), TEXT("ScctShotEvery="), Every);
	ShotTimer = Every;
	if (--ShotsLeft == 0)
	{
		// Leave the last request time to be written.
		QuitTimer = 2.0f;
	}
}

void UScctSessionSubsystem::ApplyTourCamera()
{
	if (!bTourView || !TourCameras.IsValidIndex(ShotIndex))
	{
		return;
	}

	// The camera sits at the next shot's point for the whole wait, so lighting and exposure settle there.
	const FTransform& Camera = TourCameras[ShotIndex];
	const FRotator Rotation = Camera.Rotator();
	View.X = Camera.GetLocation().X;
	View.Y = Camera.GetLocation().Y;
	View.Z = Camera.GetLocation().Z;
	View.Pitch = Rotation.Pitch;
	View.Yaw = Rotation.Yaw;
	View.Roll = 0.0f;
	View.Fov = 75.0f;
	View.Valid = 1;
	View.SpyFirstPerson = 0;
}
