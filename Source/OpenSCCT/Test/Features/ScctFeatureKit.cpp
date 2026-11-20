#include "Test/Features/ScctFeatureKit.h"
#include "Online/ScctOnlineSubsystem.h"

#include "Engine/World.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctLevelStateDebug.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Test/ScctTestApi.h"
#include "UnrealClient.h"

namespace ScctFeatureKit
{
	bool IsBroken()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("ScctFeatureBreak"));
	}

	void Note(const FString& Test, const FString& Text)
	{
		UE_LOG(LogScctTest, Log, TEXT("SCCT_FEATURE %s: %s"), *Test, *Text);
	}

	void Shot(const FString& Test, const FString& Label)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("ScctFeatureShots")))
		{
			return;
		}
		static int32 FeatureShotIndex = 0;
		const FString File = FPaths::ProjectSavedDir() / TEXT("ScctShots") / Test / FString::Printf(TEXT("%02d-%s.png"), FeatureShotIndex++, *Label.Replace(TEXT(" "), TEXT("-")));
		FScreenshotRequest::RequestScreenshot(File, true, false);
	}

	const FScctWorldDebugApi* GetWorldDebug(FString* OutError)
	{
		FString Error;
		const FScctWorldDebugApi* Debug = static_cast<const FScctWorldDebugApi*>(FOpenSCCTModule::Get().FindFeatureTable(
			TEXT("OpenScct.UnrealBridge.WorldDebugExports"), sizeof(FScctWorldDebugApi), ScctWorldDebugApiVersion, Error));
		if (OutError != nullptr)
		{
			*OutError = Error;
		}
		return Debug;
	}

	const FScctHudModel* GetHud(const FScctTestContext& Context)
	{
		const UWorld* World = Context.GetWorld();
		const UScctHudSubsystem* Hud = World != nullptr ? World->GetSubsystem<UScctHudSubsystem>() : nullptr;
		return Hud != nullptr && Hud->GetModel().bHasState ? &Hud->GetModel() : nullptr;
	}

	const UScctEffectsSubsystem* GetEffects(const FScctTestContext& Context)
	{
		const UWorld* World = Context.GetWorld();
		return World != nullptr ? World->GetSubsystem<UScctEffectsSubsystem>() : nullptr;
	}

	const FScctBridgePawn* GetLocalPawn(const FScctTestContext& Context)
	{
		const UScctSessionSubsystem* Session = Context.GetSession();
		if (Session == nullptr)
		{
			return nullptr;
		}
		for (const FScctBridgePawn& Pawn : Session->GetPawns())
		{
			if ((Pawn.Flags & EScctPawnFlags::Local) != 0)
			{
				return &Pawn;
			}
		}
		return nullptr;
	}

	FString MapLevel(const FString& Map)
	{
		return UScctOnlineSubsystem::LevelPackageOfMap(Map);
	}

	int64 GetClientTicks(const FScctTestContext& Context)
	{
		const FScctTestApi* Api = Context.GetTestApi();
		FScctTestCounters Counters = {};
		return Api != nullptr && Api->GetCounters(&Counters) != 0 ? Counters.ClientTicks : -1;
	}
}

bool FScctFeatureStage::Open(FScctTestContext& Context, const FString& InMap, const FString& Options, FString& OutError, const FString& Level)
{
	Map = InMap;
	const FString Path = Level.IsEmpty() ? ScctFeatureKit::MapLevel(Map) : Level;
	LevelName = FPaths::GetBaseFilename(Path);
	const UScctSessionSubsystem* Session = Context.GetSession();
	PreviousSessionId = Session != nullptr ? Session->GetSessionId() : 0;
	SessionId = 0;
	ReadyAt = -1.0;
	OpenedAt = Context.ElapsedSeconds;
	if (!Context.OpenLevel(FString::Printf(TEXT("%s?ScctMap=%s%s"), *Path, *Map, *Options), &OutError))
	{
		OutError += TEXT(" (import the map with Tools/Import/Import-Map.ps1)");
		return false;
	}
	return true;
}

FScctTestResult FScctFeatureStage::Step(FScctTestContext& Context, double TimeoutSeconds)
{
	if (ReadyAt >= 0.0)
	{
		return HasEnded(Context) ? FScctTestResult::Fail(FString::Printf(TEXT("the match on %s ended while the test ran"), *Map)) : FScctTestResult::Pass();
	}
	UScctSessionSubsystem* Session = Context.GetSession();
	AScctPlayerController* Player = Context.GetPlayer();
	const bool bReady = Context.GetLevelName() == LevelName && Session != nullptr && Session->IsPlaying() && Session->GetSessionId() != PreviousSessionId
		&& Session->FindLocalProxy() != nullptr && ScctFeatureKit::GetLocalPawn(Context) != nullptr && Player != nullptr && Player->PlayerCameraManager != nullptr;
	if (!bReady)
	{
		return Context.ElapsedSeconds - OpenedAt > TimeoutSeconds
			? FScctTestResult::Fail(FString::Printf(TEXT("no local body on %s after %.0f s"), *Map, TimeoutSeconds))
			: FScctTestResult::Running();
	}
	ReadyAt = Context.ElapsedSeconds;
	ReadyTicks = ScctFeatureKit::GetClientTicks(Context);
	ReadyFrame = static_cast<int64>(GFrameCounter);
	SessionId = Session->GetSessionId();
	// The game counts as having the keys from here on, though its window has no focus (a run off-screen).
	Context.HoldAction(TEXT("firstperson"), false);
	UE_LOG(LogScctTest, Log, TEXT("The local player has a body on %s at %.1f s (session %d)"), *Map, Context.ElapsedSeconds, SessionId);
	return FScctTestResult::Pass();
}

double FScctFeatureStage::GetPlayedSeconds(const FScctTestContext& Context) const
{
	return ReadyAt >= 0.0 ? Context.ElapsedSeconds - ReadyAt : 0.0;
}

double FScctFeatureStage::GetSimSeconds(const FScctTestContext& Context) const
{
	if (ReadyAt < 0.0)
	{
		return 0.0;
	}
	const int64 Ticks = ReadyTicks >= 0 ? ScctFeatureKit::GetClientTicks(Context) : -1;
	return Ticks >= ReadyTicks && ReadyTicks >= 0 ? static_cast<double>(Ticks - ReadyTicks) / 60.0 : GetPlayedSeconds(Context);
}

bool FScctFeatureStage::HasEnded(const FScctTestContext& Context) const
{
	const UScctSessionSubsystem* Session = Context.GetSession();
	return ReadyAt >= 0.0 && (Session == nullptr || Session->GetSessionId() != SessionId || !Session->IsPlaying());
}

void FScctFeatureKeys::Hold(FScctTestContext& Context, const FString& Action, bool bHeld)
{
	if (Held.Contains(Action) == bHeld)
	{
		return;
	}
	Context.HoldAction(Action, bHeld);
	if (bHeld)
	{
		Held.Add(Action);
	}
	else
	{
		Held.Remove(Action);
	}
}

void FScctFeatureKeys::ReleaseAll(FScctTestContext& Context)
{
	for (const FString& Action : Held.Array())
	{
		Context.HoldAction(Action, false);
	}
	Held.Reset();
}
