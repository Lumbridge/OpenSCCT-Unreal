// The TestBox check (the Godot client's TestBoxSmoke): a listen host with the local player and three bots on the
// TestBox fixture, every pawn on the client and drawn where the simulation has it, and the server agreeing at the end.

#include "Misc/Paths.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Test/ScctTest.h"
#include "Test/ScctTestLevels.h"

namespace
{
	/**
	 * Opens the TestBox level with three bots (the local match hosts the dedicated server's code on its thread and
	 * joins it over loopback) and runs it for 600 client ticks. Every frame after the join the client must have all
	 * four pawns, each with a body, and each body must be drawn between the pawn's replicated locations of the last two
	 * ticks (where the bridge's interpolation puts it). At the end the server is stopped and every server pawn must be
	 * on the client within 100 uu of where the server has it, the server must have run its ticks and applied the local
	 * player's moves.
	 */
	class FScctTestBoxTest : public IScctTest
	{
	public:
		static constexpr int32 Walkers = 4;
		static constexpr int32 MatchTicks = 600;

		/** Client ticks allowed for every pawn to be on the client and drawn. */
		static constexpr int32 JoinTickLimit = 180;

		/**
		 * Largest distance between a pawn on the server when it stops and the newest state the client has: the client
		 * is up to one flush plus scheduling behind and a pawn covers some 7 uu a tick at full speed (as Godot's).
		 */
		static constexpr float FinalTolerance = 100.0f;

		/** A body may be this far off the segment between its two last replicated locations (float rounding). */
		static constexpr float DrawTolerance = 0.5f;

		virtual void Start(FScctTestContext& Context) override
		{
			const FString URL = FString::Printf(TEXT("%s?ScctMap=%s?Bots=%d?Side=spy"), ScctTestLevels::TestBoxLevel, ScctTestLevels::TestBoxMap, Walkers - 1);
			if (!Context.OpenLevel(URL, &OpenError))
			{
				OpenError += TEXT(" (make it with Tools/Test/Make-TestBox.ps1)");
			}
		}

		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (!OpenError.IsEmpty())
			{
				return FScctTestResult::Fail(OpenError);
			}

			UScctSessionSubsystem* Session = Context.GetSession();
			if (Context.GetLevelName() != FPaths::GetBaseFilename(ScctTestLevels::TestBoxLevel) || Session == nullptr || !Session->IsPlaying())
			{
				return FScctTestResult::Running();
			}

			FString Error;
			const FScctTestApi* Api = Context.GetTestApi(&Error);
			if (Api == nullptr)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("test table: %s"), *Error));
			}

			if (SessionId == 0)
			{
				SessionId = Session->GetSessionId();
				if (FString(Session->GetSessionInfo().Map) != ScctTestLevels::TestBoxMap || Session->IsRemoteSession())
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("the level plays %s, not a local %s match"), Session->GetSessionInfo().Map, ScctTestLevels::TestBoxMap));
				}
			}
			else if (Session->GetSessionId() != SessionId)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the match (session %d) ended; the world presents session %d"), SessionId, Session->GetSessionId()));
			}

			FScctTestCounters Counters = {};
			Api->GetCounters(&Counters);
			Count = FMath::Clamp(Api->GetPawns(Pawns, UE_ARRAY_COUNT(Pawns)), 0, UE_ARRAY_COUNT(Pawns));
			int32 OnClient = 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				OnClient += (Pawns[Index].Flags & EScctTestPawnFlags::OnClient) != 0 ? 1 : 0;
			}
			const int32 Drawn = Session->GetProxies().Num();

			if (JoinedTick < 0)
			{
				if (OnClient == Walkers && Drawn == Walkers)
				{
					JoinedTick = Counters.ClientTicks;
					UE_LOG(LogScctTest, Log, TEXT("testbox: all %d pawns on the client and drawn at client tick %lld"), Walkers, Counters.ClientTicks);
				}
				else if (Counters.ClientTicks > JoinTickLimit)
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("after %d ticks the client has %d pawns, %d drawn (expected %d)"), JoinTickLimit, OnClient, Drawn, Walkers));
				}
				return FScctTestResult::Running();
			}

			if (OnClient != Walkers || Drawn != Walkers)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("tick %lld: the client has %d pawns, %d drawn (expected %d)"), Counters.ClientTicks, OnClient, Drawn, Walkers));
			}

			FString ViewFailure;
			if (!CheckBodies(*Session, ViewFailure))
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("tick %lld: %s"), Counters.ClientTicks, *ViewFailure));
			}
			++CheckedFrames;
			Drive(Context, Counters.ClientTicks);

			return Counters.ClientTicks < MatchTicks ? FScctTestResult::Running() : Finish(*Api, Context);
		}

	private:
		/**
		 * The scripted pilot (Godot's TestBox runs its local walker on one): the local player walks forward, to the
		 * right and back through the keys a player would press, so the bodies are checked while they move and the
		 * server has the local player's moves to apply. The local pawn's walked distance is kept for the verdict.
		 */
		void Drive(FScctTestContext& Context, int64 ClientTicks)
		{
			static const TCHAR* const Legs[] = { TEXT("forward"), TEXT("right"), TEXT("back") };
			const int32 Leg = ClientTicks < JoinedTick + 20 ? -1 : static_cast<int32>((ClientTicks - JoinedTick - 20) / 120);
			const int32 Wanted = Leg >= 0 && Leg < UE_ARRAY_COUNT(Legs) ? Leg : -1;
			if (Wanted != HeldLeg)
			{
				if (HeldLeg >= 0)
				{
					Context.HoldAction(Legs[HeldLeg], false);
				}
				if (Wanted >= 0 && !Context.HoldAction(Legs[Wanted], true))
				{
					UE_LOG(LogScctTest, Warning, TEXT("testbox: could not hold %s"), Legs[Wanted]);
				}
				HeldLeg = Wanted;
			}

			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FScctTestPawn& Pawn = Pawns[Index];
				if ((Pawn.Flags & EScctTestPawnFlags::Local) != 0)
				{
					const FVector Location(Pawn.ClientX, Pawn.ClientY, Pawn.ClientZ);
					if (bHaveLocal)
					{
						LocalWalked += FVector::Dist2D(LastLocal, Location);
					}
					LastLocal = Location;
					bHaveLocal = true;
				}
			}
		}

		/** Each body against the segment between its pawn's replicated locations of the last two ticks. */
		bool CheckBodies(const UScctSessionSubsystem& Session, FString& OutFailure)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FScctTestPawn& Pawn = Pawns[Index];
				if ((Pawn.Flags & EScctTestPawnFlags::OnClient) == 0)
				{
					continue;
				}

				const AScctPawnProxy* Proxy = Session.FindProxy(Pawn.ClientActorId);
				const FScctBridgePawn* Drawn = Session.GetPawns().FindByPredicate([&](const FScctBridgePawn& Candidate) { return Candidate.ActorId == Pawn.ClientActorId; });
				if (Proxy == nullptr || Drawn == nullptr)
				{
					OutFailure = FString::Printf(TEXT("pawn %d (server %d) has no body"), Pawn.ClientActorId, Pawn.ServerActorId);
					return false;
				}

				// The body's origin is the pawn's location plus its PrePivot (AScctPawnProxy::Present).
				const FVector Body = Proxy->GetActorLocation() - FVector(Drawn->PrePivotX, Drawn->PrePivotY, Drawn->PrePivotZ);
				const FVector From(Pawn.PreviousX, Pawn.PreviousY, Pawn.PreviousZ);
				const FVector To(Pawn.ClientX, Pawn.ClientY, Pawn.ClientZ);
				const float Off = FMath::PointDistToSegment(Body, From, To);
				MaxDrawError = FMath::Max(MaxDrawError, Off);
				MaxStep = FMath::Max(MaxStep, static_cast<float>(FVector::Dist(From, To)));
				if (Off > DrawTolerance)
				{
					OutFailure = FString::Printf(TEXT("pawn %d drawn at %s, %.2f uu off its replicated path %s -> %s"),
						Pawn.ClientActorId, *Body.ToCompactString(), Off, *From.ToCompactString(), *To.ToCompactString());
					return false;
				}
			}
			return true;
		}

		FScctTestResult Finish(const FScctTestApi& Api, FScctTestContext& Context)
		{
			if (Api.StopServer() != 1)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("could not stop the server: %s"), *Context.GetTestApiError()));
			}

			FScctTestCounters Counters = {};
			Api.GetCounters(&Counters);
			Count = FMath::Clamp(Api.GetPawns(Pawns, UE_ARRAY_COUNT(Pawns)), 0, UE_ARRAY_COUNT(Pawns));
			int32 OnServer = 0;
			int32 LocalMoves = -1;
			float MaxError = 0.0f;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FScctTestPawn& Pawn = Pawns[Index];
				if ((Pawn.Flags & EScctTestPawnFlags::OnServer) == 0)
				{
					continue;
				}
				++OnServer;
				if ((Pawn.Flags & EScctTestPawnFlags::OnClient) == 0)
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("server pawn %d is not on the client"), Pawn.ServerActorId));
				}
				if ((Pawn.Flags & EScctTestPawnFlags::Local) != 0)
				{
					LocalMoves = Pawn.MovesApplied;
				}
				const float Error = FVector::Dist(FVector(Pawn.ServerX, Pawn.ServerY, Pawn.ServerZ), FVector(Pawn.ClientX, Pawn.ClientY, Pawn.ClientZ));
				MaxError = FMath::Max(MaxError, Error);
			}

			if (OnServer != Walkers)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the server has %d pawns, expected %d"), OnServer, Walkers));
			}
			if (Counters.ServerTicks < MatchTicks * 95 / 100)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the server ran %lld ticks while the client ran %lld"), Counters.ServerTicks, Counters.ClientTicks));
			}
			if (LocalMoves < MatchTicks / 2)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the server applied only %d moves of the local player"), LocalMoves));
			}
			if (LocalWalked < MinimumWalk)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the local player walked only %.0f uu under the scripted keys"), LocalWalked));
			}
			if (MaxError > FinalTolerance)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("a replicated pawn is %.1f uu from the server's (limit %.0f uu)"), MaxError, FinalTolerance));
			}

			return FScctTestResult::Pass(FString::Printf(
				TEXT("walkers=%d joinedAt=%lld clientTicks=%lld serverTicks=%lld frames=%d walked=%.0fuu maxStep=%.2fuu maxDrawError=%.3fuu maxError=%.2fuu localMoves=%d corrections=%lld reinits=%lld"),
				Walkers, JoinedTick, Counters.ClientTicks, Counters.ServerTicks, CheckedFrames, LocalWalked, MaxStep, MaxDrawError, MaxError, LocalMoves,
				Counters.CorrectionsApplied, Counters.Reinits));
		}

		/** The scripted walk must move the local player at least this far (uu), or the body checks saw nothing move. */
		static constexpr float MinimumWalk = 300.0f;

		FString OpenError;
		int32 HeldLeg = -1;
		bool bHaveLocal = false;
		FVector LastLocal = FVector::ZeroVector;
		float LocalWalked = 0.0f;
		int32 SessionId = 0;
		int64 JoinedTick = -1;
		int32 CheckedFrames = 0;
		float MaxDrawError = 0.0f;

		/** The longest tick-to-tick step a checked body was drawn along (0 would mean the check saw nothing move). */
		float MaxStep = 0.0f;
		FScctTestPawn Pawns[32] = {};
		int32 Count = 0;
	};
}

SCCT_TEST("testbox", FScctTestBoxTest);
