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
			Proxy->Present(Pawn, bFirstPerson);
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
