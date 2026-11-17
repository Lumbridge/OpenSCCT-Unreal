#include "Hub/ScctHubActivities.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const TCHAR* KindName(int32 Kind)
	{
		static const TCHAR* const Names[] = { TEXT("other"), TEXT("start"), TEXT("finish"), TEXT("checkpoint"), TEXT("travel"), TEXT("firing line"),
			TEXT("jukebox"), TEXT("laptop"), TEXT("goal") };
		return Kind >= 0 && Kind < UE_ARRAY_COUNT(Names) ? Names[Kind] : TEXT("?");
	}
}

FScctHubActivities::FScctHubActivities(UScctHubSubsystem* InOwner)
	: Owner(InOwner)
{
}

const FScctHubActivityApi* FScctHubActivities::GetApi()
{
	if (Api == nullptr && !bReportedNoApi)
	{
		FString Error;
		Api = ScctFeature<FScctHubActivityApi>(&Error);
		if (Api == nullptr)
		{
			bReportedNoApi = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHubActivities: no activity table, the hub plays without its activities, props and emote poses: %s"), *Error);
		}
	}
	return Api;
}

void FScctHubActivities::Tick(UWorld* World, float DeltaSeconds, bool bVerbose)
{
	Cues.Reset();
	if (World == nullptr || GetApi() == nullptr)
	{
		return;
	}

	Api->GetState(&State);
	if (State.Valid == 0)
	{
		return;
	}

	if (const APlayerController* Player = World->GetFirstPlayerController(); Player != nullptr && Player->PlayerCameraManager != nullptr)
	{
		Eye = Player->PlayerCameraManager->GetCameraLocation();
	}

	Marks.SetNumZeroed(FMath::Clamp(State.MarkCount, 0, 256), EAllowShrinking::No);
	const int32 MarkCount = Marks.Num() > 0 ? Api->GetMarks(Marks.GetData(), Marks.Num()) : 0;
	Marks.SetNum(FMath::Clamp(MarkCount, 0, Marks.Num()), EAllowShrinking::No);

	Targets.SetNumZeroed(FMath::Clamp(State.TargetCount, 0, 64), EAllowShrinking::No);
	const int32 TargetCount = Targets.Num() > 0 ? Api->GetTargets(Targets.GetData(), Targets.Num()) : 0;
	Targets.SetNum(FMath::Clamp(TargetCount, 0, Targets.Num()), EAllowShrinking::No);

	TCHAR Cue[64];
	for (int32 Taken = 0; Taken < 32 && Api->TakeCue(Cue, UE_ARRAY_COUNT(Cue)) >= 0; ++Taken)
	{
		Cues.Add(Cue);
		++CuesSinceLog;
		UE_CLOG(bVerbose, LogOpenSCCT, Log, TEXT("ScctHubActivities: cue '%s'"), Cue);
	}

	// The clock runs every frame; the board follows the eye.
	Activity = ReadText(EScctHubActivityText::Activity);
	Notices = ReadText(EScctHubActivityText::Notices);
	BoardTitle = ReadText(EScctHubActivityText::BoardTitle);
	BoardLines = BoardTitle.IsEmpty() ? FString() : ReadText(EScctHubActivityText::BoardLines);
	TrackTitle = ReadText(EScctHubActivityText::TrackTitle);
	LocalEmote = (State.Flags & EScctHubActivityFlags::Emoting) != 0 ? ReadText(EScctHubActivityText::LocalEmote) : FString();

	if (bVerbose)
	{
		LogLine(DeltaSeconds);
	}
}

void FScctHubActivities::Stop()
{
	State = {};
	Marks.Reset();
	Targets.Reset();
	Cues.Reset();
	Activity.Reset();
	Notices.Reset();
	BoardTitle.Reset();
	BoardLines.Reset();
	LocalEmote.Reset();
}

bool FScctHubActivities::FindMark(EScctHubMarkKind Kind, FVector& OutAt) const
{
	for (const FScctHubActivityMark& Mark : Marks)
	{
		if (Mark.Kind == static_cast<int32>(Kind))
		{
			OutAt = FVector(Mark.X, Mark.Y, Mark.Z);
			return true;
		}
	}
	return false;
}

FString FScctHubActivities::ReadText(EScctHubActivityText Kind) const
{
	TCHAR Buffer[2048];
	const int32 Length = Api->GetText(static_cast<int32>(Kind), static_cast<float>(Eye.X), static_cast<float>(Eye.Y), static_cast<float>(Eye.Z), Buffer, UE_ARRAY_COUNT(Buffer));
	return Length > 0 ? FString(Buffer) : FString();
}

void FScctHubActivities::LogLine(float DeltaSeconds)
{
	LogTimer -= DeltaSeconds;
	if (LogTimer > 0.0f)
	{
		return;
	}
	LogTimer = 4.0f;

	TMap<FString, int32> Kinds;
	for (const FScctHubActivityMark& Mark : Marks)
	{
		++Kinds.FindOrAdd(KindName(Mark.Kind));
	}
	FString KindText;
	for (const TPair<FString, int32>& Pair : Kinds)
	{
		KindText += FString::Printf(TEXT(" %s %d"), *Pair.Key, Pair.Value);
	}
	const int32 NewPoses = State.EmotePoses - LoggedEmotePoses;
	LoggedEmotePoses = State.EmotePoses;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubActivities: flags 0x%x, %d marks (%s ), %d targets, ball %s, record %d '%s', %d cues since the last line; activity '%s', board '%s' (%d rows), notices '%s'; emote poses +%d%s"),
		State.Flags, Marks.Num(), *KindText, Targets.Num(),
		(State.Flags & EScctHubActivityFlags::HasBall) != 0 ? *FString::Printf(TEXT("at (%.0f, %.0f, %.0f)"), State.BallX, State.BallY, State.BallZ) : TEXT("not said"),
		State.Track, *TrackTitle, CuesSinceLog, *Activity.Replace(TEXT("\n"), TEXT(" | ")), *BoardTitle, BoardLines.IsEmpty() ? 0 : BoardLines.Len() - BoardLines.Replace(TEXT("\n"), TEXT("")).Len() + 1,
		*Notices.Replace(TEXT("\n"), TEXT(" | ")), NewPoses, LocalEmote.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (the local player plays %s)"), *LocalEmote));
	CuesSinceLog = 0;
}
