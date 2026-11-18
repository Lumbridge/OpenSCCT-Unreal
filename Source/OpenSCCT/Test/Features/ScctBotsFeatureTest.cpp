// bots: a spy with three bots on the run's map (-ScctTestMap, AquaE by default). For 20 s of the simulation the test
// follows every bot's drawn body (its pawn proxy, Play/ScctPawnProxy.h) and fails unless all three bots were drawn and
// at least two of them got more than 300 uu from where they were first seen, the Godot client's play-bots rule
// (game/Scripts/Smoke/PlayBotsSmoke.cs: a merc bot may hold a guard spot). The path each walked is reported too; a
// body that moves farther between two frames than a running pawn could in that much simulation time (a respawn) adds
// nothing to it. The seconds are the simulation's, so a starved run (a few frames a second) still watches 20 s of play.

#include "Engine/World.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Test/Features/ScctFeatureKit.h"

namespace ScctBotsFeatureLocal
{
	constexpr int32 BotsWanted = 3;
	constexpr double BotsSettleSeconds = 3.0;
	constexpr double BotsWatchSeconds = 20.0;
	/** How far from where it was first seen a bot must get to count as moving (uu), Godot's MoveDistance. */
	constexpr float BotsMoveDistance = 300.0f;
	/** Bots that must count as moving, as in Godot's play-bots. */
	constexpr int32 BotsMovingWanted = 2;
	/** Faster than this (uu a second of the simulation, a sprint and some) between two frames is a respawn, not walking. */
	constexpr double BotsMaxSpeed = 900.0;

	class FScctBotsFeatureTest : public IScctTest
	{
		struct FBotTrack
		{
			FVector First = FVector::ZeroVector;
			FVector Last = FVector::ZeroVector;
			float Path = 0.0f;
			float Away = 0.0f;
			double Seen = 0.0;
			int32 Kind = 0;
			bool bShown = false;
		};

	public:
		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (!bOpened)
			{
				if (Context.GetWorld() == nullptr)
				{
					return FScctTestResult::Running();
				}
				bOpened = true;
				FString Error;
				const int32 Bots = ScctFeatureKit::IsBroken() ? 0 : BotsWanted;
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("opening with %d bots%s"), Bots, ScctFeatureKit::IsBroken() ? TEXT(" (-ScctFeatureBreak)") : TEXT("")));
				if (!Stage.Open(Context, FScctTestContext::GetTestMap(TEXT("AquaE")), FString::Printf(TEXT("?Side=spy?Bots=%d"), Bots), Error))
				{
					return FScctTestResult::Fail(Error);
				}
				return FScctTestResult::Running();
			}
			const FScctTestResult Ready = Stage.Step(Context);
			if (Ready.State != EScctTestState::Passed)
			{
				return Ready;
			}
			const double Played = Stage.GetSimSeconds(Context);
			if (Played < BotsSettleSeconds)
			{
				return FScctTestResult::Running();
			}
			if (LastSeconds < 0.0)
			{
				LastSeconds = Played;
				ScctFeatureKit::Shot(GetName(), TEXT("start"));
			}
			const double Delta = Played - LastSeconds;
			LastSeconds = Played;

			const UScctSessionSubsystem* Session = Context.GetSession();
			for (const TPair<int32, TObjectPtr<AScctPawnProxy>>& Pair : Session->GetProxies())
			{
				const AScctPawnProxy* Proxy = Pair.Value.Get();
				if (Proxy == nullptr || Proxy->IsLocal())
				{
					continue;
				}
				const FVector At = Proxy->GetActorLocation();
				FBotTrack* Track = Tracks.Find(Pair.Key);
				if (Track == nullptr)
				{
					Track = &Tracks.Add(Pair.Key);
					Track->First = At;
					Track->Last = At;
					Track->Kind = Proxy->GetPawn().Kind;
				}
				const float Step = static_cast<float>(FVector::Dist(At, Track->Last));
				Track->Path += Step <= BotsMaxSpeed * FMath::Max(Delta, 1.0 / 60.0) + 20.0 ? Step : 0.0f;
				Track->Away = FMath::Max(Track->Away, static_cast<float>(FVector::Dist(At, Track->First)));
				Track->Last = At;
				Track->Seen += Delta;
				Track->bShown |= Proxy->IsShown();
			}

			if (Played < BotsSettleSeconds + BotsWatchSeconds)
			{
				return FScctTestResult::Running();
			}
			ScctFeatureKit::Shot(GetName(), TEXT("end"));

			TArray<FString> Lines;
			int32 Drawn = 0;
			int32 Moving = 0;
			for (const TPair<int32, FBotTrack>& Pair : Tracks)
			{
				const FBotTrack& Track = Pair.Value;
				Drawn += Track.bShown ? 1 : 0;
				Moving += Track.Away > BotsMoveDistance ? 1 : 0;
				Lines.Add(FString::Printf(TEXT("#%d %s %.0f uu from its first place at most, %.0f uu walked, in %.1f s%s"), Pair.Key, Track.Kind == 1 ? TEXT("merc") : TEXT("spy"),
					Track.Away, Track.Path, Track.Seen, Track.bShown ? TEXT("") : TEXT(" (never drawn)")));
			}
			const FString Figures = FString::Printf(TEXT("%d bot bodies on the client, %d drawn, %d more than %.0f uu from where first seen in %.0f s of play over %lld frames: %s"),
				Tracks.Num(), Drawn, Moving, BotsMoveDistance, BotsWatchSeconds, Stage.GetFrames(), Lines.Num() > 0 ? *FString::Join(Lines, TEXT(", ")) : TEXT("none"));
			ScctFeatureKit::Note(GetName(), Figures);
			if (Tracks.Num() == 0)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("no bot's body was on the client in %.0f s of play"), BotsWatchSeconds));
			}
			if (Drawn < BotsWanted)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("only %d of the %d bots were drawn (%s)"), Drawn, BotsWanted, *Figures));
			}
			if (Moving < BotsMovingWanted)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("only %d bots got more than %.0f uu from where they were first seen, %d wanted (%s)"),
					Moving, BotsMoveDistance, BotsMovingWanted, *Figures));
			}
			return FScctTestResult::Pass(Figures);
		}

	private:
		bool bOpened = false;
		double LastSeconds = -1.0;
		FScctFeatureStage Stage;
		TMap<int32, FBotTrack> Tracks;
	};
}

SCCT_TEST("bots", ScctBotsFeatureLocal::FScctBotsFeatureTest);
