#include "Runtime/ScctCameraBoneApi.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

void ScctRunCameraBoneCheck(float MatchSeconds)
{
	static const bool bCameraBoneCheck = FParse::Param(FCommandLine::Get(), TEXT("ScctKnockCheck"));
	if (!bCameraBoneCheck)
	{
		return;
	}

	constexpr float KnockAt = 3.0f;
	constexpr float LogFrom = 2.0f;
	constexpr float LogUntil = 10.0f;
	constexpr float LogEvery = 0.5f;
	static bool bCameraBoneKnocked = false;
	static float CameraBoneNextLog = LogFrom;
	if (MatchSeconds < LogFrom || MatchSeconds > LogUntil)
	{
		return;
	}

	const FScctCameraBoneApi* CameraBones = ScctFeature<FScctCameraBoneApi>();
	if (CameraBones == nullptr)
	{
		return;
	}

	if (!bCameraBoneKnocked && MatchSeconds >= KnockAt)
	{
		bCameraBoneKnocked = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctCameraBone: -ScctKnockCheck, the local player knocked down at %.2f s: %s"), MatchSeconds,
			CameraBones->KnockCheck() != 0 ? TEXT("done") : TEXT("NOT done (no local pawn)"));
	}

	if (MatchSeconds < CameraBoneNextLog)
	{
		return;
	}
	CameraBoneNextLog = MatchSeconds + LogEvery;

	FScctCameraBoneState State = {};
	if (CameraBones->GetState(&State) == 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctCameraBone: %.2f s, no local pawn or view"), MatchSeconds);
		return;
	}

	const FVector View(State.ViewX, State.ViewY, State.ViewZ);
	const FVector Head(State.HeadX, State.HeadY, State.HeadZ);
	const FVector Eye(State.EyeX, State.EyeY, State.EyeZ);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctCameraBone: %.2f s, attached %d, view (%.1f, %.1f, %.1f), head %s (%.1f, %.1f, %.1f), view from head %.1f, view from eye %.1f, lookups answered %d"),
		MatchSeconds, State.Attached, View.X, View.Y, View.Z, State.HasHead != 0 ? TEXT("yes") : TEXT("no"), Head.X, Head.Y, Head.Z,
		State.HasHead != 0 ? FVector::Dist(View, Head) : -1.0, FVector::Dist(View, Eye), State.Answered);
}
