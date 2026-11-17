#include "Play/ScctPoseAim.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** The table, or null; a missing table is logged once. */
	const FScctPoseAimApi* PoseAimTable()
	{
		static bool bReported = false;
		FString Error;
		const FScctPoseAimApi* Api = ScctFeature<FScctPoseAimApi>(&Error);
		if (Api == nullptr && !bReported)
		{
			bReported = true;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Pose aim: the bridge has no PoseAimExports table (%s); the local body aims from the ticks alone"), *Error);
		}
		return Api;
	}

	const AScctPlayerController* PoseAimPlayer(UWorld& World)
	{
		return Cast<AScctPlayerController>(World.GetFirstPlayerController());
	}

	float PoseAimMuzzleDistance = 0.0f;

	/** -ScctAimProbe's running sums between two summaries. */
	struct FPoseAimProbeSums
	{
		double PosedLag = 0.0;
		double TickLag = 0.0;
		double Turn = 0.0;
		double Seconds = 0.0;
		int32 Frames = 0;
		int32 FromView = 0;
		float LastViewYaw = 0.0f;
		bool bHasLast = false;
	};
	FPoseAimProbeSums PoseAimProbeSums;

	TMap<FString, float> PoseAimLoggedClips;
}

void ScctPoseAim::FeedDrawnView(UWorld& World, int32 LocalActorId)
{
	const FScctPoseAimApi* Api = PoseAimTable();
	if (Api == nullptr)
	{
		return;
	}
	const AScctPlayerController* Player = PoseAimPlayer(World);
	const FScctLookView* View = Player != nullptr ? &Player->GetLookView() : nullptr;
	if (LocalActorId == 0 || View == nullptr || View->Valid == 0)
	{
		Api->SetDrawnView(0, 0.0f, 0.0f, 0.0f);
		return;
	}
	// AimPitch and AimYaw are the controller's rotation plus the mouse turn not yet simulated: the view this frame
	// draws. The bridge takes it only for the local player's own spy (not a merc, not a demo's observed pawn).
	Api->SetDrawnView(LocalActorId, View->AimPitch, View->AimYaw, 0.0f);
}

void ScctPoseAim::LogAimProbe(UWorld& World, int32 LocalActorId, float DeltaSeconds)
{
	static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("ScctAimProbe"));
	const FScctPoseAimApi* Api = bProbe ? PoseAimTable() : nullptr;
	const AScctPlayerController* Player = Api != nullptr ? PoseAimPlayer(World) : nullptr;
	if (Player == nullptr || LocalActorId == 0 || Player->GetLookView().Valid == 0)
	{
		return;
	}

	FScctAimProbe Probe = {};
	if (Api->GetAimProbe(LocalActorId, &Probe) == 0)
	{
		return;
	}

	const FScctLookView& View = Player->GetLookView();
	FPoseAimProbeSums& Sums = PoseAimProbeSums;
	const float Turn = Sums.bHasLast ? FMath::Abs(FMath::FindDeltaAngleDegrees(Sums.LastViewYaw, View.AimYaw)) : 0.0f;
	Sums.LastViewYaw = View.AimYaw;
	Sums.bHasLast = true;
	const float PosedLag = FMath::FindDeltaAngleDegrees(Probe.PosedViewYaw, View.AimYaw);
	const float TickLag = FMath::FindDeltaAngleDegrees(Probe.TickViewYaw, View.AimYaw);

	// Only frames that turn are worth a line: standing still both agree.
	if (Turn > 0.01f)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Aim probe: camera yaw %.2f, view yaw %.2f, body yaw %.2f; upper body aims at %.2f (lag %.2f deg)%s, the ticks alone %.2f (lag %.2f deg); turned %.2f deg this frame"),
			View.Yaw, View.AimYaw, Probe.BodyYaw, Probe.PosedViewYaw, PosedLag, Probe.FromView != 0 ? TEXT(" from the drawn view") : TEXT(""),
			Probe.TickViewYaw, TickLag, Turn);
		Sums.PosedLag += FMath::Abs(PosedLag);
		Sums.TickLag += FMath::Abs(TickLag);
		Sums.Turn += Turn;
		Sums.Seconds += DeltaSeconds;
		Sums.FromView += Probe.FromView != 0 ? 1 : 0;
		++Sums.Frames;
	}
	if (Sums.Seconds >= 2.0 && Sums.Frames > 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Aim probe summary: %d turning frames (%d aimed from the drawn view), turning %.1f deg/s: mean lag of the drawn upper body %.2f deg, of the ticks alone %.2f deg"),
			Sums.Frames, Sums.FromView, Sums.Turn / Sums.Seconds, Sums.PosedLag / Sums.Frames, Sums.TickLag / Sums.Frames);
		const float Last = Sums.LastViewYaw;
		Sums = FPoseAimProbeSums();
		Sums.LastViewYaw = Last;
		Sums.bHasLast = true;
	}
}

int32 ScctPoseAim::ReadHiddenSockets()
{
	PoseAimMuzzleDistance = 0.0f;
	const FScctPoseAimApi* Api = PoseAimTable();
	return Api != nullptr ? Api->GetFirstPersonSockets(&PoseAimMuzzleDistance) : 0;
}

float ScctPoseAim::LastMuzzleDistance()
{
	return PoseAimMuzzleDistance;
}

void ScctPoseAim::LogViewModelClipRates()
{
	const FScctPoseAimApi* Api = PoseAimTable();
	if (Api == nullptr)
	{
		return;
	}
	FScctClipRate Rates[8] = {};
	const int32 Count = FMath::Clamp(Api->GetViewModelClipRates(Rates, UE_ARRAY_COUNT(Rates)), 0, static_cast<int32>(UE_ARRAY_COUNT(Rates)));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FScctClipRate& Rate = Rates[Index];
		Rate.Name[UE_ARRAY_COUNT(Rate.Name) - 1] = TEXT('\0');
		const FString Name(Rate.Name);
		const float* Logged = PoseAimLoggedClips.Find(Name);
		if (Logged != nullptr && FMath::IsNearlyEqual(*Logged, Rate.Fps))
		{
			continue;
		}
		PoseAimLoggedClips.Add(Name, Rate.Fps);
		UE_LOG(LogOpenSCCT, Log, TEXT("View model clip %s (channel %d) runs at %.1f fps (%s)"), *Name, Rate.Channel, Rate.Fps,
			Rate.FromProject != 0 ? TEXT("the animation project's gameplay clip") : TEXT("stock rate: no animation project clip bound"));
	}
}
