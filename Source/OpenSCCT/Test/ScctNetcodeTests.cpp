// The netcode checks (the Godot client's PlayNetcodeSmoke, PlayGrabNetcodeSmoke and PlayCoopNetcodeSmoke): the real
// rendered match over a simulated link with latency, jitter, loss, duplication and reordering, on a listen host or a
// dedicated server with bot clients of its own, with OpenSCCT's tuned net profile or the original's.

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Test/ScctTest.h"
#include "UnrealClient.h"

namespace
{
	/** What one check runs: the link profile, the server kind, the net profile and the scenario. */
	struct FScctNetcodeCase
	{
		const TCHAR* Profile; // SimLinkProfile name: 100, 200, 300...
		bool bDedicated;
		bool bStock;
		const TCHAR* Scenario; // walk, grab or coop

		/** A stress link run by hand: only whether the link applied is judged; the figures are printed for reading. */
		bool bStress = false;
	};

	/**
	 * One netcode check. It arms the link (TestExports.SetNextMatchLink), opens SteeE and, once the local player is
	 * in, watches every drawn body every frame:
	 *
	 *  - walk: the local player walks a square under held keys for 26 s (the server's bots walk their own routes) and
	 *    stands for 4 s;
	 *  - grab: a bot spy grabs a bot mercenary, knocks him out and the merc gets up, played by the two bots as two
	 *    network owners while the local player watches;
	 *  - coop: two bot spies boost and hoist each other up the GE63 wall while the local mercenary watches.
	 *
	 * It passes when the link really applied (the client's measured round trip is near the profile's), no drawn body
	 * jumped farther in one frame than the cut allows except on a reset the client was sent (a reinitialisation or a
	 * placement), the local player's server corrections stay under the budget per minute, every pawn the client draws
	 * at the end is within the tolerance of a place the server had it in the last 1.5 s, no bot client resynchronised,
	 * and the scenario completed. It prints corrections/min, the largest drawn jump, the final drift and the round trip.
	 * -ScctTestFrames=10 saves a 10 fps picture sequence to Saved/ScctTestFrames/<test>/ for review.
	 */
	class FScctNetcodeTest : public IScctTest
	{
	public:
		explicit FScctNetcodeTest(const FScctNetcodeCase& InCase)
			: Case(InCase)
		{
		}

		/** Seconds after the join the walk stops (it starts at WalkStartSeconds) and the verdict comes, 4 s standing later. */
		static constexpr double WalkSeconds = 28.0;
		static constexpr double VerdictSeconds = 32.0;

		/** Seconds after the join the grab or the co-op move must have finished in. */
		static constexpr double ScenarioSeconds = 75.0;

		/**
		 * A drawn body may move this far in one frame (uu) plus what a running pawn covers in the frame's time; more is
		 * a teleport unless the client was sent a reset of that pawn. The bridge itself draws a jump over 200 uu a
		 * tick as a cut; anything over 64 uu in a frame is a visible pop.
		 */
		static constexpr float CutDistance = 64.0f;
		static constexpr float MaxSpeed = 900.0f;

		/** Server corrections of the local player's prediction allowed per minute of play. */
		static constexpr double CorrectionBudget = 30.0;

		/**
		 * Seconds after the join before corrections are counted. A new pawn's first ticks are corrected as it lands and
		 * the client's first ticks catch up after the map load; over a 200 ms link those corrections arrive up to 6 s
		 * after the join (measured), all within this window, and none after it in a clean run.
		 */
		static constexpr double SettleSeconds = 8.0;

		/** Seconds after the join the walk starts: the spawn's other spies have left the spawn by then. */
		static constexpr double WalkStartSeconds = 5.0;

		/** Frames up to this long count for the round trip (a frame stalled longer, by a hitch or a level stream, says nothing about the link). */
		static constexpr float SmoothFrameSeconds = 0.2f;

		/** Largest distance at the end from a drawn pawn to the nearest place the server had it in the last 1.5 s. */
		static constexpr float FinalTolerance = 32.0f;

		virtual void Start(FScctTestContext& Context) override
		{
			FString Error;
			const FScctTestApi* Api = Context.GetTestApi(&Error);
			if (Api == nullptr)
			{
				StartError = FString::Printf(TEXT("test table: %s"), *Error);
				return;
			}
			if (Api->SetNextMatchLink(Case.Profile, Case.bStock ? 1 : 0, Case.bDedicated ? 1 : 0, Case.Scenario) != 1)
			{
				StartError = FString::Printf(TEXT("SetNextMatchLink: %s"), *Context.GetTestApiError());
				return;
			}

			// The dedicated server's bots are written for SteeE; a listen match may be pointed at another map.
			Map = Case.bDedicated ? FString(TEXT("SteeE")) : FScctTestContext::GetTestMap(TEXT("SteeE"));
			const TCHAR* Side = FCString::Strcmp(Case.Scenario, TEXT("coop")) == 0 ? TEXT("merc") : TEXT("spy");
			const FString URL = FString::Printf(TEXT("/Game/Imported/Maps/%s/%s?Side=%s?Bots=3"), *Map, *Map, Side);
			if (!Context.OpenLevel(URL, &Error))
			{
				StartError = Error;
			}

			FParse::Value(FCommandLine::Get(), TEXT("ScctTestFrames="), FramesPerSecond);
			if (FramesPerSecond > 0)
			{
				FrameDirectory = FPaths::ProjectSavedDir() / TEXT("ScctTestFrames") / GetName();
				IFileManager::Get().DeleteDirectory(*FrameDirectory, false, true);
				IFileManager::Get().MakeDirectory(*FrameDirectory, true);
			}
			UE_LOG(LogScctTest, Log, TEXT("%s: link %s ms, %s server, %s profile, %s on %s"), *GetName(), Case.Profile,
				Case.bDedicated ? TEXT("dedicated") : TEXT("listen"), Case.bStock ? TEXT("stock") : TEXT("enhanced"), Case.Scenario, *Map);
		}

		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (!StartError.IsEmpty())
			{
				return FScctTestResult::Fail(StartError);
			}

			UScctSessionSubsystem* Session = Context.GetSession();
			if (Context.GetLevelName() != Map || Session == nullptr || !Session->IsPlaying())
			{
				return FScctTestResult::Running();
			}

			const FScctTestApi* Api = Context.GetTestApi();
			if (SessionId == 0)
			{
				SessionId = Session->GetSessionId();
			}
			else if (Session->GetSessionId() != SessionId)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the match (session %d) ended; the world presents session %d"), SessionId, Session->GetSessionId()));
			}

			FScctNetStats Stats = {};
			if (Api->GetNetStats(&Stats) != SessionId || (Stats.Flags & EScctNetFlags::Linked) == 0)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("session %d does not run over the simulated link (%s)"), SessionId, *Context.GetTestApiError()));
			}

			const double Now = Context.ElapsedSeconds;
			if (Stats.JoinedTick < 0 || Session->GetProxies().Num() == 0)
			{
				return Now > 120.0 ? FScctTestResult::Fail(FString::Printf(TEXT("the local player never joined the match: %s"), *Detail(*Api))) : FScctTestResult::Running();
			}
			if (JoinedAt < 0.0)
			{
				JoinedAt = Now;
				LastFrameTime = Now;
				UE_LOG(LogScctTest, Log, TEXT("%s: joined at client tick %lld, %d bodies drawn"), *GetName(), Stats.JoinedTick, Session->GetProxies().Num());
			}
			const double Played = Now - JoinedAt;
			const float FrameSeconds = static_cast<float>(Now - LastFrameTime);
			LastFrameTime = Now;

			const int32 Count = FMath::Clamp(Api->GetNetPawns(Pawns, UE_ARRAY_COUNT(Pawns)), 0, UE_ARRAY_COUNT(Pawns));
			WatchBodies(*Session, Count, FrameSeconds);
			++Frames;
			// The round trip is sampled on smooth frames only: the link's datagrams are read once a simulation tick,
			// so a starved game (other games sharing the machine) adds its frame time to every measurement.
			if (FrameSeconds > 0.0f && FrameSeconds <= SmoothFrameSeconds && Stats.RttMs > 0.0f && Played >= 2.0)
			{
				RttSamples.Add(Stats.RttMs);
				FrameSamples.Add(FrameSeconds * 1000.0f);
			}
			if (CorrectionsAtStart < 0 && Played >= SettleSeconds)
			{
				// The spawn's own corrections (the first ticks of a new pawn) are not the link's.
				CorrectionsAtStart = Stats.CorrectionsApplied;
			}
			if (bWalk)
			{
				Drive(Context, Played, Count);
			}
			SaveFrame(Played);

			if (Now >= NextLog)
			{
				NextLog = Now + 2.0;
				UE_LOG(LogScctTest, Log, TEXT("%s: t=%.0fs ticks=%lld/%lld rtt=%.0fms (link %d) corrections=%lld/%lld reinits=%lld resets=%lld maxJump=%.1fuu teleports=%d link sent=%lld lost=%lld bots=%d/%d %s"),
					*GetName(), Played, Stats.ClientTicks, Stats.ServerTicks, Stats.RttMs, Stats.LatencyMs, Stats.CorrectionsApplied, Stats.CorrectionsReceived,
					Stats.ServerReinits, Stats.ClientResets, MaxJump, Teleports, Stats.LinkSent, Stats.LinkLost, Stats.BotsJoined, Stats.BotProblems, *Detail(*Api));
			}

			if ((Stats.Flags & EScctNetFlags::ScenarioFailed) != 0)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("%s scenario: %s"), Case.Scenario, *Detail(*Api)));
			}
			if (bWalk ? Played < VerdictSeconds : (Stats.Flags & EScctNetFlags::ScenarioDone) == 0)
			{
				return !bWalk && Played > ScenarioSeconds
					? FScctTestResult::Fail(FString::Printf(TEXT("the %s scenario did not finish in %.0f s: %s"), Case.Scenario, ScenarioSeconds, *Detail(*Api)))
					: FScctTestResult::Running();
			}
			return Verdict(*Api, Stats, Count, Played);
		}

	private:
		/** Every drawn body against where it was drawn last frame, and the resets the client was sent of it. */
		void WatchBodies(const UScctSessionSubsystem& Session, int32 Count, float FrameSeconds)
		{
			const float Allowed = CutDistance + MaxSpeed * FrameSeconds;
			for (const FScctBridgePawn& Drawn : Session.GetPawns())
			{
				const AScctPawnProxy* Proxy = Session.FindProxy(Drawn.ActorId);
				if (Proxy == nullptr)
				{
					continue;
				}
				int32 Resets = 0;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if (Pawns[Index].ClientActorId == Drawn.ActorId)
					{
						Resets = Pawns[Index].Resets;
					}
				}

				// The body's origin is the pawn's location plus its PrePivot (AScctPawnProxy::Present): a crouch moves
				// the PrePivot, not the pawn.
				const FVector At = Proxy->GetActorLocation() - FVector(Drawn.PrePivotX, Drawn.PrePivotY, Drawn.PrePivotZ);
				if (const FBody* Last = Bodies.Find(Drawn.ActorId))
				{
					const float Jump = static_cast<float>(FVector::Dist(Last->At, At));
					if (Last->Resets != Resets)
					{
						++ResetJumps;
					}
					else
					{
						if (Jump > MaxJump)
						{
							MaxJump = Jump;
							MaxJumpAllowed = Allowed;
						}
						if (Jump > Allowed)
						{
							++Teleports;
							if (FirstTeleport.IsEmpty())
							{
								FirstTeleport = FString::Printf(TEXT("pawn %d jumped %.1f uu in one frame (%.3f s, cut %.0f uu) from %s to %s with no reset"),
									Drawn.ActorId, Jump, FrameSeconds, Allowed, *Last->At.ToCompactString(), *At.ToCompactString());
								UE_LOG(LogScctTest, Warning, TEXT("%s: %s"), *GetName(), *FirstTeleport);
							}
						}
					}
				}
				Bodies.Add(Drawn.ActorId, FBody{ At, Resets });
			}
		}

		/**
		 * The walk's pilot: the local player walks a square (forward, right, back, left) through the keys a player
		 * holds, so the server corrects real predicted moves sent over the link, then stands still for the verdict.
		 */
		void Drive(FScctTestContext& Context, double Played, int32 Count)
		{
			static const TCHAR* const Legs[] = { TEXT("forward"), TEXT("right"), TEXT("back"), TEXT("left") };
			static constexpr double LegSeconds[] = { 3.0, 1.5, 3.0, 1.5 };
			int32 Wanted = -1;
			if (Played >= WalkStartSeconds && Played < WalkSeconds)
			{
				double Into = FMath::Fmod(Played - WalkStartSeconds, 9.0);
				for (int32 Leg = 0; Leg < UE_ARRAY_COUNT(Legs); ++Leg)
				{
					if (Into < LegSeconds[Leg])
					{
						Wanted = Leg;
						break;
					}
					Into -= LegSeconds[Leg];
				}
			}
			if (Wanted != HeldLeg)
			{
				if (HeldLeg >= 0)
				{
					Context.HoldAction(Legs[HeldLeg], false);
				}
				if (Wanted >= 0 && !Context.HoldAction(Legs[Wanted], true))
				{
					UE_LOG(LogScctTest, Warning, TEXT("%s: could not hold %s"), *GetName(), Legs[Wanted]);
				}
				HeldLeg = Wanted;
			}

			for (int32 Index = 0; Index < Count; ++Index)
			{
				if ((Pawns[Index].Flags & EScctNetPawnFlags::Local) != 0)
				{
					const FVector At(Pawns[Index].ClientX, Pawns[Index].ClientY, Pawns[Index].ClientZ);
					LocalWalked += bHaveLocal ? FVector::Dist2D(LastLocal, At) : 0.0f;
					LastLocal = At;
					bHaveLocal = true;
				}
			}
		}

		/** -ScctTestFrames=N: one picture every 1/N s of play into Saved/ScctTestFrames/<test>/. */
		void SaveFrame(double Played)
		{
			if (FramesPerSecond <= 0 || Played < NextFrameAt)
			{
				return;
			}
			NextFrameAt = Played + 1.0 / FramesPerSecond;
			FScreenshotRequest::RequestScreenshot(FrameDirectory / FString::Printf(TEXT("frame_%05d.png"), FrameIndex++), false, false);
		}

		FScctTestResult Verdict(const FScctTestApi& Api, const FScctNetStats& Stats, int32 Count, double Played)
		{
			// The drawn pawns at the end against the server's recent places.
			float Drift = 0.0f;
			int32 Compared = 0;
			FString DriftPawn;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FScctNetPawn& Pawn = Pawns[Index];
				if ((Pawn.Flags & (EScctNetPawnFlags::OnClient | EScctNetPawnFlags::OnServer)) != (EScctNetPawnFlags::OnClient | EScctNetPawnFlags::OnServer) || Pawn.Drift < 0.0f)
				{
					continue;
				}
				++Compared;
				if (Pawn.Drift > Drift)
				{
					Drift = Pawn.Drift;
					DriftPawn = FString::Printf(TEXT("pawn %d%s: client %s, server %s"), Pawn.ClientActorId, (Pawn.Flags & EScctNetPawnFlags::Local) != 0 ? TEXT(" (local)") : TEXT(""),
						*FVector(Pawn.ClientX, Pawn.ClientY, Pawn.ClientZ).ToCompactString(), *FVector(Pawn.ServerX, Pawn.ServerY, Pawn.ServerZ).ToCompactString());
				}
			}

			const double Minutes = FMath::Max((Played - SettleSeconds) / 60.0, 0.1);
			const int64 Corrections = Stats.CorrectionsApplied - FMath::Max<int64>(CorrectionsAtStart, 0);
			const double CorrectionsPerMinute = Corrections / Minutes;
			float Rtt = 0.0f;
			float FrameMs = 0.0f;
			if (RttSamples.Num() > 0)
			{
				RttSamples.Sort();
				FrameSamples.Sort();
				Rtt = RttSamples[RttSamples.Num() / 2];
				FrameMs = FrameSamples[FrameSamples.Num() / 2];
			}
			const FString Figures = FString::Printf(
				TEXT("corrections/min=%.1f (%lld in %.0f s after the spawn's %lld; %lld received) maxJump=%.1fuu (cut %.0f) teleports=%d resetJumps=%d drift=%.1fuu over %d pawns rtt=%.0fms median of %d frames of %.0f ms (link %d ms +-%d, %.0f%% loss; last %.0f, min %.0f) reinits=%lld resets=%lld link sent=%lld lost=%lld delivered=%lld bots=%d walked=%.0fuu frames=%d (%.0f fps) %s"),
				CorrectionsPerMinute, Corrections, Played - SettleSeconds, FMath::Max<int64>(CorrectionsAtStart, 0), Stats.CorrectionsReceived, MaxJump, MaxJumpAllowed, Teleports,
				ResetJumps, Drift, Compared, Rtt, RttSamples.Num(), FrameMs, Stats.LatencyMs, Stats.JitterMs, Stats.LossPercent, Stats.RttMs, Stats.MinRttMs, Stats.ServerReinits,
				Stats.ClientResets, Stats.LinkSent, Stats.LinkLost, Stats.LinkDelivered, Stats.BotsJoined, LocalWalked, Frames, Frames / FMath::Max(Played, 1.0), *Detail(Api));
			UE_LOG(LogScctTest, Log, TEXT("%s: %s"), *GetName(), *Figures);

			// The client's own round trip says whether the link really applied: half the latency each way plus the
			// ticks a datagram waits to be read and answered. Both ends read their datagrams once a simulation tick and
			// the ticks run once a frame, so up to two frames come on top of the link on a slow (shared) machine.
			const float Expected = static_cast<float>(Stats.LatencyMs);
			const float Highest = Expected * 1.6f + 40.0f + 2.0f * FrameMs;
			if (RttSamples.Num() < 30)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("only %d frames were under %.0f ms, too few to measure the link (the machine is starved: other games running?); %s"),
					RttSamples.Num(), SmoothFrameSeconds * 1000.0f, *Figures));
			}
			if (Rtt < Expected * 0.7f || Rtt > Highest)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the client measures a %.0f ms round trip on a %d ms link (%.0f to %.0f expected); %s"),
					Rtt, Stats.LatencyMs, Expected * 0.7f, Highest, *Figures));
			}
			if (Case.bStress)
			{
				return FScctTestResult::Pass(TEXT("stress link (figures not judged): ") + Figures);
			}
			if (Teleports > 0)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("%d drawn teleports; first: %s; %s"), Teleports, *FirstTeleport, *Figures));
			}
			if (CorrectionsPerMinute > CorrectionBudget)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("%.1f corrections per minute (budget %.0f); %s"), CorrectionsPerMinute, CorrectionBudget, *Figures));
			}
			if (Compared < 2)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("only %d pawns could be compared with the server; %s"), Compared, *Figures));
			}
			if (Drift > FinalTolerance)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("%s is %.1f uu from the server (tolerance %.0f); %s"), *DriftPawn, Drift, FinalTolerance, *Figures));
			}
			if (Stats.BotProblems > 0)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("%d bot clients left or resynchronised; %s"), Stats.BotProblems, *Figures));
			}
			if (bWalk && LocalWalked < 300.0f)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("the local player walked only %.0f uu under the held keys; %s"), LocalWalked, *Figures));
			}
			return FScctTestResult::Pass(Figures);
		}

		static FString Detail(const FScctTestApi& Api)
		{
			TCHAR Buffer[512];
			Api.GetNetDetail(Buffer, UE_ARRAY_COUNT(Buffer));
			return Buffer;
		}

		struct FBody
		{
			FVector At;
			int32 Resets;
		};

		FScctNetcodeCase Case;
		bool bWalk = FCString::Strcmp(Case.Scenario, TEXT("walk")) == 0;
		FString StartError;
		FString Map;
		int32 SessionId = 0;
		double JoinedAt = -1.0;
		double LastFrameTime = 0.0;
		double NextLog = 0.0;
		TMap<int32, FBody> Bodies;
		float MaxJump = 0.0f;
		float MaxJumpAllowed = CutDistance;
		int32 Teleports = 0;
		int32 ResetJumps = 0;
		FString FirstTeleport;
		int32 HeldLeg = -1;
		bool bHaveLocal = false;
		FVector LastLocal = FVector::ZeroVector;
		float LocalWalked = 0.0f;
		int32 FramesPerSecond = 0;
		FString FrameDirectory;
		double NextFrameAt = 0.0;
		int32 FrameIndex = 0;
		int32 Frames = 0;
		TArray<float> RttSamples;
		TArray<float> FrameSamples;
		int64 CorrectionsAtStart = -1;
		FScctNetPawn Pawns[32] = {};
	};

	/** Registers one check per case under its Godot-style name. */
	struct FScctNetcodeRegistration
	{
		FScctNetcodeRegistration(const TCHAR* Name, FScctNetcodeCase Case)
		{
			FScctTestRegistry::Register(Name, [Case] { return TUniquePtr<IScctTest>(MakeUnique<FScctNetcodeTest>(Case)); });
		}
	};

	const FScctNetcodeRegistration NetListen100(TEXT("play-net-listen100"), { TEXT("100"), false, false, TEXT("walk") });
	const FScctNetcodeRegistration NetListen200(TEXT("play-net-listen200"), { TEXT("200"), false, false, TEXT("walk") });
	const FScctNetcodeRegistration NetListen100Stock(TEXT("play-net-listen100-stock"), { TEXT("100"), false, true, TEXT("walk") });
	const FScctNetcodeRegistration NetListen200Stock(TEXT("play-net-listen200-stock"), { TEXT("200"), false, true, TEXT("walk") });
	const FScctNetcodeRegistration NetDedicated100(TEXT("play-net-dedicated100"), { TEXT("100"), true, false, TEXT("walk") });
	const FScctNetcodeRegistration NetDedicated200(TEXT("play-net-dedicated200"), { TEXT("200"), true, false, TEXT("walk") });
	const FScctNetcodeRegistration NetDedicated100Stock(TEXT("play-net-dedicated100-stock"), { TEXT("100"), true, true, TEXT("walk") });
	const FScctNetcodeRegistration NetDedicated200Stock(TEXT("play-net-dedicated200-stock"), { TEXT("200"), true, true, TEXT("walk") });
	const FScctNetcodeRegistration NetGrab100(TEXT("play-net-grab100"), { TEXT("100"), true, false, TEXT("grab") });
	const FScctNetcodeRegistration NetGrab200(TEXT("play-net-grab200"), { TEXT("200"), true, false, TEXT("grab") });
	const FScctNetcodeRegistration NetGrab100Stock(TEXT("play-net-grab100-stock"), { TEXT("100"), true, true, TEXT("grab") });
	const FScctNetcodeRegistration NetCoop100(TEXT("play-net-coop100"), { TEXT("100"), true, false, TEXT("coop") });
	const FScctNetcodeRegistration NetCoop200(TEXT("play-net-coop200"), { TEXT("200"), true, false, TEXT("coop") });
	// Stress300 (300 ms, +-40 ms, 5% loss, 2% duplicated and reordered): run by hand to see the figures grow; only the link is judged.
	const FScctNetcodeRegistration NetListen300(TEXT("play-net-listen300"), { TEXT("300"), false, false, TEXT("walk"), true });
	const FScctNetcodeRegistration NetDedicated300(TEXT("play-net-dedicated300"), { TEXT("300"), true, false, TEXT("walk"), true });
}
