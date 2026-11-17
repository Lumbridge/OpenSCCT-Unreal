// The frame-time tests: frame-rate (the frame cap and vertical sync give the frame rate they ask for, evenly), smooth
// (the camera, the body and the view model move every frame, evenly, above the 60 Hz simulation; the scope's zoom
// ramps one way), play-hitch (no frame of a minute's play takes over two 60 Hz frames) and first-use (the first
// shot, grenade, vision switch and emitter burst cost no more than a frame budget over the median frame). They are the
// Godot client's FrameRateSmoke, SmoothSmoke, PlayHitchSmoke and GameplayFirstUseSmoke/GameplayFxWarmupSmoke/
// StartupWeaponsSmoke (game/Scripts/Smoke). Each writes its frames to Saved/Test/frames-<test>.csv (Test/ScctFrameRecorder.h).

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Hud/ScctHudSubsystem.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctLevelStateDebug.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Test/ScctFrameRecorder.h"
#include "Test/ScctTest.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	/** Seconds a map may take to load and its match to give the local player a body. */
	constexpr double LoadSeconds = 150.0;

	/** A frame over this is a hitch the player sees: two frames of a 60 Hz display (the Godot client's limit). */
	constexpr double HitchMs = 33.4;

	/**
	 * Mouse counts a second for the steady turn of the circle walk: about 90 degrees a second at the default sensitivity,
	 * a circle some four metres across at a run, which fits in most rooms.
	 */
	constexpr float CircleCounts = 5600.0f;

	bool IsOffscreen()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"));
	}

	/** The refresh rate of the desktop's display mode, or 0 when the platform does not say. */
	int32 GetDisplayHz()
	{
#if PLATFORM_WINDOWS
		DEVMODEW Mode = {};
		Mode.dmSize = sizeof(Mode);
		if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &Mode) && Mode.dmDisplayFrequency > 1)
		{
			return static_cast<int32>(Mode.dmDisplayFrequency);
		}
#endif
		return 0;
	}

	/**
	 * Off-screen, Windows 11 does not honour the engine's 1 ms timer resolution for a process with no visible window,
	 * so the frame limiter's sleeps come in 15.6 ms steps (a 30 fps cap gave 24 fps, a 60 fps cap 44). A player's game
	 * always has its window; the off-screen test opts out of that power saving so it measures what a player gets.
	 */
	void KeepTimerResolution()
	{
#if PLATFORM_WINDOWS && defined(PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION)
		static bool bDone = false;
		if (bDone || !IsOffscreen())
		{
			return;
		}
		bDone = true;
		PROCESS_POWER_THROTTLING_STATE State = {};
		State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		State.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
		State.StateMask = 0;
		const bool bSet = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)) != 0;
		UE_LOG(LogScctTest, Log, TEXT("Off-screen: the process keeps its timer resolution without a visible window: %s"), bSet ? TEXT("yes") : TEXT("no (Windows refused)"));
#endif
	}

	float GetFloatVariable(const TCHAR* Name, float Default)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable != nullptr ? Variable->GetFloat() : Default;
	}

	/**
	 * What every frame-time test shares: it opens the run's map (-ScctTestMap, AquaE by default) with a side and a
	 * number of bots, waits until the local player has a body and a camera, then records a row every frame and hands
	 * the frame to StepPlaying with the seconds since the body appeared. It drives the player as a person would:
	 * the keys through the controller (HoldScriptedAction), the mouse as raw counts on the controller's input, the
	 * player's bound commands through their keys.
	 */
	class FScctFrameTestBase : public IScctTest
	{
	public:
		virtual ~FScctFrameTestBase() override = default;

		virtual FScctTestResult Step(FScctTestContext& Context) override final
		{
			if (!bOpened)
			{
				if (Context.GetWorld() == nullptr)
				{
					return FScctTestResult::Running();
				}
				bOpened = true;
				KeepTimerResolution();
				Map = FScctTestContext::GetTestMap(TEXT("AquaE"));
				FString Error;
				const FString URL = FString::Printf(TEXT("/Game/Imported/Maps/%s/%s?Side=%s?Bots=%d"), *Map, *Map, Side, Bots);
				if (!Context.OpenLevel(URL, &Error))
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("%s (import the map with Tools/Import/Import-Map.ps1)"), *Error));
				}
				return FScctTestResult::Running();
			}

			UScctSessionSubsystem* Session = Context.GetSession();
			const AScctPlayerController* Player = Context.GetPlayer();
			const bool bReady = Context.GetLevelName() == Map && Session != nullptr && Session->IsPlaying()
				&& Session->FindLocalProxy() != nullptr && Player != nullptr && Player->PlayerCameraManager != nullptr;
			if (PlayStart < 0.0)
			{
				if (!bReady)
				{
					return Context.ElapsedSeconds > LoadSeconds
						? FScctTestResult::Fail(FString::Printf(TEXT("no local body on %s after %.0f s"), *Map, LoadSeconds))
						: FScctTestResult::Running();
				}
				PlayStart = Context.ElapsedSeconds;
				SessionId = Session->GetSessionId();
				UE_LOG(LogScctTest, Log, TEXT("%s: the local player has a body on %s at %.1f s"), *GetName(), *Map, Context.ElapsedSeconds);
				// The game counts as having the keys from here on, though its window has no focus (a run off-screen).
				Context.HoldAction(TEXT("firstperson"), false);
			}
			else if (Session == nullptr || Session->GetSessionId() != SessionId)
			{
				return Finish(Context, FScctTestResult::Fail(TEXT("the match ended while the test ran")));
			}

			Recorder.Sample(Context);
			const FScctTestResult Result = StepPlaying(Context, Context.ElapsedSeconds - PlayStart);
			return Result.State == EScctTestState::Running ? Result : Finish(Context, Result);
		}

	protected:
		/** One frame of the match, Seconds after the local body appeared; this frame's row is already recorded. */
		virtual FScctTestResult StepPlaying(FScctTestContext& Context, double Seconds) = 0;

		/** Puts back what the test changed (console variables); the keys are let go by Finish. */
		virtual void Restore(FScctTestContext& Context) {}

		/** Holds or lets go one of the player's named actions, remembering what is held so Finish can let it go. */
		void Hold(FScctTestContext& Context, const TCHAR* Action, bool bHeld)
		{
			const bool bWasHeld = Held.Contains(Action);
			if (bHeld == bWasHeld)
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

		/**
		 * Walks forward and back in turns of Period seconds (so the player stays near where he began, whatever the map),
		 * Seconds into the walk; a negative Seconds stands still.
		 */
		void Walk(FScctTestContext& Context, double Seconds, double Period = 1.5)
		{
			const bool bForward = Seconds >= 0.0 && FMath::FloorToInt(Seconds / Period) % 2 == 0;
			const bool bBack = Seconds >= 0.0 && !bForward;
			Hold(Context, TEXT("forward"), bForward);
			Hold(Context, TEXT("back"), bBack);
		}

		/**
		 * Moves the mouse sideways at CountsPerSecond for this frame, as raw counts on the controller's input (the
		 * path a mouse's events take). The counts are the frame's share at its delta time; fractions are kept, the
		 * bridge takes the counts as they come.
		 */
		void Turn(FScctTestContext& Context, float CountsPerSecond)
		{
			if (AScctPlayerController* Player = Context.GetPlayer())
			{
				const float Counts = CountsPerSecond * static_cast<float>(FApp::GetDeltaTime());
				if (Counts != 0.0f)
				{
					Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, Counts, 1));
				}
			}
		}

		/**
		 * Walks forward while the mouse turns him steadily: a circle a few metres across, so the walk never reaches a
		 * wall and the camera, the body and the view model all move at a steady speed (no stops, no turnarounds).
		 */
		void Circle(FScctTestContext& Context, float CountsPerSecond)
		{
			Hold(Context, TEXT("forward"), true);
			Hold(Context, TEXT("back"), false);
			Turn(Context, CountsPerSecond);
		}

		/** Presses the key the player's bindings put a command on ("SwitchHeadset 1"). */
		bool Tap(FScctTestContext& Context, const TCHAR* Command)
		{
			AScctPlayerController* Player = Context.GetPlayer();
			return Player != nullptr && Player->TapBoundCommand(Command);
		}

		/** What the HUD shows this frame, or null. */
		const FScctHudModel* GetHud(const FScctTestContext& Context) const
		{
			const UWorld* World = Context.GetWorld();
			const UScctHudSubsystem* Hud = World != nullptr ? World->GetSubsystem<UScctHudSubsystem>() : nullptr;
			return Hud != nullptr && Hud->GetModel().bHasState ? &Hud->GetModel() : nullptr;
		}

		/** Starts a new phase of the recording at this frame. */
		void Enter(FName Phase, double Seconds)
		{
			Recorder.SetPhase(Phase);
			PhaseStart = Seconds;
			UE_LOG(LogScctTest, Log, TEXT("%s: phase %s at %.2f s"), *GetName(), *Phase.ToString(), Seconds);
		}

		/** Ends the test: lets the keys go, puts things back, writes the CSV and adds its path to the verdict. */
		FScctTestResult Finish(FScctTestContext& Context, FScctTestResult Result)
		{
			for (const FString& Action : Held.Array())
			{
				Context.HoldAction(Action, false);
			}
			Held.Reset();
			Restore(Context);

			const FString Csv = Recorder.WriteCsv(GetName());
			UE_LOG(LogScctTest, Log, TEXT("%s: %d frames written to %s"), *GetName(), Recorder.GetSamples().Num(), Csv.IsEmpty() ? TEXT("(nothing: the file could not be written)") : *Csv);
			if (Result.State == EScctTestState::Passed)
			{
				Result.Detail += FString::Printf(TEXT(" csv=%s"), *FPaths::GetCleanFilename(Csv));
			}
			return Result;
		}

		const TCHAR* Side = TEXT("merc");
		int32 Bots = 0;
		FString Map;
		FScctFrameRecorder Recorder;
		double PhaseStart = 0.0;

	private:
		bool bOpened = false;
		double PlayStart = -1.0;
		int32 SessionId = 0;
		TSet<FString> Held;
	};

	// =================================================================================================================

	/**
	 * frame-rate: the merc on the map, walking a small circle (forward while the mouse turns him steadily). For each case the frame cap
	 * (t.MaxFPS) and vertical sync (r.VSync) are set as console variables (there is no frame-cap or vsync option in
	 * the settings registry to set them through yet), the game settles for a second and a half, then three seconds are
	 * measured (after four seconds uncapped for the map's first pictures). A frame cannot be shorter than its slowest
	 * thread (game, render, GPU), so a cap above what that allows is measured against what it allows. Checks for each
	 * capped case: the mean frame rate is within 10% of what was asked, the 99th percentile of the frames' difference
	 * from the period asked for is under FrameJitterLimit, leaving out frames a thread made late (unless the threads leave the cap no room, when the pacing is
	 * the machine's and is only reported), and the camera's steady turn moves every frame with its 99th percentile speed jitter under
	 * TurnJitterLimit. Vertical sync is judged only with a window (-Windowed): off-screen there is no display to wait
	 * for, so the case is measured and reported. -ScctFrameCases=0,30,60,120,vsync chooses the cases.
	 */
	class FScctFrameRateTest : public FScctFrameTestBase
	{
	public:
		static constexpr double WarmupSeconds = 4.0;
		static constexpr double SettleSeconds = 1.5;
		static constexpr double MeasureSeconds = 3.0;
		static constexpr double RateTolerance = 0.10;
		static constexpr double FrameJitterLimit = 0.5;
		static constexpr double TurnJitterLimit = 0.5;
		static constexpr float TurnCounts = CircleCounts;

		virtual void Start(FScctTestContext& Context) override
		{
			FString Text = TEXT("0,30,60,120,vsync");
			FParse::Value(FCommandLine::Get(), TEXT("ScctFrameCases="), Text, false);
			TArray<FString> Names;
			Text.ParseIntoArray(Names, TEXT(","));
			for (FString& CaseName : Names)
			{
				CaseName.TrimStartAndEndInline();
				if (CaseName.Equals(TEXT("vsync"), ESearchCase::IgnoreCase))
				{
					Cases.Add(FCase{ TEXT("vsync"), 0, true });
				}
				else if (!CaseName.IsEmpty())
				{
					const int32 Cap = FMath::Max(0, FCString::Atoi(*CaseName));
					Cases.Add(FCase{ Cap == 0 ? FString(TEXT("uncapped")) : FString::Printf(TEXT("cap%d"), Cap), Cap, false });
				}
			}
			SavedMaxFps = GetFloatVariable(TEXT("t.MaxFPS"), 0.0f);
			SavedVSync = static_cast<int32>(GetFloatVariable(TEXT("r.VSync"), 0.0f));
		}

	protected:
		struct FCase
		{
			FString Label;
			int32 Cap = 0;
			bool bVSync = false;
		};

		virtual FScctTestResult StepPlaying(FScctTestContext& Context, double Seconds) override
		{
			if (Cases.Num() == 0)
			{
				return FScctTestResult::Fail(TEXT("no cases (-ScctFrameCases)"));
			}
			if (Current < 0 && Recorder.GetPhase().IsNone())
			{
				// A few seconds uncapped first: the first pictures of a map compile shaders and stream, which is not pacing.
				Context.RunConsoleCommand(TEXT("t.MaxFPS 0"));
				Context.RunConsoleCommand(TEXT("r.VSync 0"));
				Enter(TEXT("warmup"), Seconds);
			}
			else if ((Current < 0 && Seconds - PhaseStart >= WarmupSeconds) || (Current >= 0 && Seconds - PhaseStart >= SettleSeconds + MeasureSeconds))
			{
				if (Current >= 0)
				{
					Judge(Cases[Current]);
				}
				if (++Current >= Cases.Num())
				{
					return Verdict();
				}
				const FCase& Case = Cases[Current];
				Context.RunConsoleCommand(FString::Printf(TEXT("t.MaxFPS %d"), Case.Cap));
				Context.RunConsoleCommand(FString::Printf(TEXT("r.VSync %d"), Case.bVSync ? 1 : 0));
				Enter(*(Case.Label + TEXT("-settle")), Seconds);
			}
			else if (Current >= 0 && Seconds - PhaseStart >= SettleSeconds && Recorder.GetPhase() != FName(*Cases[Current].Label))
			{
				const double Start = PhaseStart;
				Enter(*Cases[Current].Label, Seconds);
				PhaseStart = Start;
			}

			Circle(Context, TurnCounts);
			return FScctTestResult::Running();
		}

		void Judge(const FCase& Case)
		{
			int32 First = 0;
			int32 End = 0;
			if (!Recorder.FindPhase(*Case.Label, First, End))
			{
				Failures.Add(FString::Printf(TEXT("%s: no frames measured"), *Case.Label));
				return;
			}
			const FScctFrameTimeStats Times = Recorder.FrameTimes(First, End);
			const FScctMotionStats TurnStats = Recorder.CameraTurn(First, End);
			const FScctMotionStats Body = Recorder.BodyMove(First, End);

			// What the machine can do in this case: a frame cannot be shorter than its slowest thread (game, render or
			// GPU). The median of that bounds the rate; when even its 90th percentile leaves the case's frame time
			// some room, the frames are the limiter's alone and their evenness is judged too.
			TArray<double> Busiest;
			for (int32 Index = First; Index < End; ++Index)
			{
				const FScctFrameSample& Row = Recorder.GetSamples()[Index];
				Busiest.Add(FMath::Max3(Row.GameMs, Row.RenderMs, Row.GpuMs));
			}
			const double BusiestMs = FScctFrameRecorder::Median(Busiest);
			const double BusiestP90Ms = FScctFrameRecorder::Quantile(Busiest, 0.9);
			const double BoundFps = BusiestMs > 0.0 ? 1000.0 / BusiestMs : 0.0;
			const double RoomFps = BusiestP90Ms > 0.0 ? 1000.0 / BusiestP90Ms : 0.0;

			// What the case asks for: the cap or the display's rate, or what the threads allow if that is less.
			double Asked = 0.0;
			FString Note;
			if (Case.bVSync)
			{
				if (IsOffscreen())
				{
					Note = TEXT(" (off-screen: no display to wait for, not judged)");
				}
				else if (const int32 Hz = GetDisplayHz())
				{
					Asked = Hz;
					Note += FString::Printf(TEXT(" (the display runs at %d Hz)"), Hz);
				}
				else
				{
					Note = TEXT(" (the display's rate is unknown, not judged)");
				}
			}
			else if (Case.Cap > 0)
			{
				Asked = Case.Cap;
			}
			double Expected = Asked;
			bool bJudgePacing = Asked > 0.0;
			if (Asked > 0.0 && BoundFps > 0.0 && BoundFps < Asked)
			{
				Expected = BoundFps;
				Note += FString::Printf(TEXT(" (the slowest thread takes %.1f ms, so %.1f fps is what can be had)"), BusiestMs, BoundFps);
			}
			if (Asked > 0.0 && RoomFps > 0.0 && RoomFps < Asked * (1.0 + RateTolerance))
			{
				bJudgePacing = false;
				Note += FString::Printf(TEXT(" (thread-bound: p90 of the slowest thread %.1f ms, pacing not judged)"), BusiestP90Ms);
			}

			// The limiter's pacing: each frame against the period asked for, leaving out the frames a thread of the game made
			// late (its slowest thread took most of the period: the machine's load, another process, not the limiter).
			double PacedJitter = 0.0;
			int32 LateFrames = 0;
			if (Expected > 0.0)
			{
				const double PeriodMs = 1000.0 / Expected;
				TArray<double> Paced;
				for (int32 Index = First; Index < End; ++Index)
				{
					if (Busiest[Index - First] >= PeriodMs * 0.9)
					{
						++LateFrames;
						continue;
					}
					Paced.Add(FMath::Abs(Recorder.GetSamples()[Index].FrameMs - PeriodMs) / PeriodMs);
				}
				PacedJitter = FScctFrameRecorder::Quantile(Paced, 0.99);
				if (Paced.Num() < (End - First) / 2)
				{
					bJudgePacing = false;
					Note += FString::Printf(TEXT(" (%d of %d frames thread-late, pacing not judged)"), LateFrames, End - First);
				}
			}

			const FString Line = FString::Printf(TEXT("%s: asked=%s achieved=%.1ffps mean=%.2fms median=%.2fms p99=%.2fms max=%.2fms frameJitterP99=%.3f pacedJitterP99=%.3f threadLate=%d turn[%s] body[%s]%s"),
				*Case.Label, Expected > 0.0 ? *FString::Printf(TEXT("%.1ffps"), Expected) : TEXT("-"), Times.Fps(), Times.MeanMs, Times.MedianMs, Times.P99Ms, Times.MaxMs, Times.P99Jitter,
				PacedJitter, LateFrames, *TurnStats.ToString(), *Body.ToString(), *Note);
			UE_LOG(LogScctTest, Display, TEXT("SCCT_FRAMERATE %s"), *Line);
			Summary.Add(FString::Printf(TEXT("%s=%.1ffps/pace%.2f/turn%.3f"), *Case.Label, Times.Fps(), PacedJitter, TurnStats.P99Jitter));

			if (Expected > 0.0)
			{
				if (FMath::Abs(Times.Fps() - Expected) > Expected * RateTolerance)
				{
					Failures.Add(FString::Printf(TEXT("%s: %.1f fps, asked %.1f (more than %.0f%% off)"), *Case.Label, Times.Fps(), Expected, RateTolerance * 100.0));
				}
				if (bJudgePacing && PacedJitter > FrameJitterLimit)
				{
					Failures.Add(FString::Printf(TEXT("%s: frame pacing jitter p99 %.3f over %.2f (%d thread-late frames left out)"), *Case.Label, PacedJitter, FrameJitterLimit, LateFrames));
				}
			}
			if (Times.MedianMs > HitchMs * 2.0)
			{
				Failures.Add(FString::Printf(TEXT("%s: %d frames in %.0f s (%.0f ms each): the machine is too busy to measure pacing"), *Case.Label, Times.Frames, MeasureSeconds, Times.MedianMs));
			}
			else if (TurnStats.Moving == 0)
			{
				Failures.Add(FString::Printf(TEXT("%s: the camera did not turn under the steady mouse"), *Case.Label));
			}
			else if (TurnStats.Held > 0 || TurnStats.P99Jitter > TurnJitterLimit)
			{
				Failures.Add(FString::Printf(TEXT("%s: the camera's turn held still on %d frames, jitter p99 %.3f (limit %.2f)"), *Case.Label, TurnStats.Held, TurnStats.P99Jitter, TurnJitterLimit));
			}
		}

		FScctTestResult Verdict()
		{
			if (Failures.Num() > 0)
			{
				return FScctTestResult::Fail(FString::Join(Failures, TEXT("; ")));
			}
			return FScctTestResult::Pass(FString::Join(Summary, TEXT(" ")) + (IsOffscreen() ? TEXT(" (off-screen)") : TEXT(" (windowed)")));
		}

		virtual void Restore(FScctTestContext& Context) override
		{
			Context.RunConsoleCommand(FString::Printf(TEXT("t.MaxFPS %g"), SavedMaxFps));
			Context.RunConsoleCommand(FString::Printf(TEXT("r.VSync %d"), SavedVSync));
		}

		TArray<FCase> Cases;
		int32 Current = -1;
		TArray<FString> Failures;
		TArray<FString> Summary;
		float SavedMaxFps = 0.0f;
		int32 SavedVSync = 0;
	};

	// =================================================================================================================

	/**
	 * smooth: what the player sees from frame to frame while the screen runs faster than the 60 Hz simulation. The
	 * merc plays with the frame cap at 120 (-ScctSmoothCap) and vertical sync off. He walks forward and back while the
	 * mouse turns him steadily: the drawn camera's turn and move, his drawn body and the first-person view model must
	 * move on every frame (none held still between two ticks) with the 99th percentile of each frame's speed against
	 * its own moving average under the limits below. Then the scope (SwitchBinocularVision, the key of the merc's
	 * bindings) is raised and put down again: the field of view must ramp down and back up without a frame going the
	 * wrong way. The frame rate must be above 66 fps for the every-frame checks to mean anything; below that the test
	 * fails and says so.
	 */
	class FScctSmoothTest : public FScctFrameTestBase
	{
	public:
		static constexpr double SettleSeconds = 2.0;
		static constexpr double WalkSeconds = 5.0;
		static constexpr double ScopeSeconds = 2.0;
		static constexpr float TurnCounts = CircleCounts;
		static constexpr double TurnJitterLimit = 0.5;
		static constexpr double MoveJitterLimit = 0.75;
		static constexpr double HeldShareLimit = 0.05;
		static constexpr double MinimumFps = 66.0;

		virtual void Start(FScctTestContext& Context) override
		{
			FParse::Value(FCommandLine::Get(), TEXT("ScctSmoothCap="), Cap);
			SavedMaxFps = GetFloatVariable(TEXT("t.MaxFPS"), 0.0f);
			SavedVSync = static_cast<int32>(GetFloatVariable(TEXT("r.VSync"), 0.0f));
		}

	protected:
		enum class EStep : uint8 { Begin, Settle, Walk, ScopeIn, ScopeOut, Done };

		virtual FScctTestResult StepPlaying(FScctTestContext& Context, double Seconds) override
		{
			const double InPhase = Seconds - PhaseStart;
			switch (StepNow)
			{
			case EStep::Begin:
				Context.RunConsoleCommand(FString::Printf(TEXT("t.MaxFPS %d"), Cap));
				Context.RunConsoleCommand(TEXT("r.VSync 0"));
				StepNow = EStep::Settle;
				Enter(TEXT("settle"), Seconds);
				break;
			case EStep::Settle:
				Circle(Context, TurnCounts);
				if (InPhase >= SettleSeconds)
				{
					StepNow = EStep::Walk;
					Enter(TEXT("walk-turn"), Seconds);
				}
				break;
			case EStep::Walk:
				Circle(Context, TurnCounts);
				if (InPhase >= WalkSeconds)
				{
					Walk(Context, -1.0);
					if (!Tap(Context, TEXT("SwitchBinocularVision")))
					{
						return FScctTestResult::Fail(TEXT("no key of the merc's bindings raises the scope (SwitchBinocularVision)"));
					}
					StepNow = EStep::ScopeIn;
					Enter(TEXT("scope-in"), Seconds);
				}
				break;
			case EStep::ScopeIn:
				if (const FScctHudModel* Hud = GetHud(Context))
				{
					bScoped |= Hud->State.BinocularMode == 2;
				}
				if (InPhase >= ScopeSeconds)
				{
					Tap(Context, TEXT("SwitchBinocularVision"));
					StepNow = EStep::ScopeOut;
					Enter(TEXT("scope-out"), Seconds);
				}
				break;
			case EStep::ScopeOut:
				if (InPhase >= ScopeSeconds)
				{
					StepNow = EStep::Done;
					return Verdict();
				}
				break;
			default:
				break;
			}
			return FScctTestResult::Running();
		}

		/** The field of view's ramp in a phase: the frames it moved the wrong way (by over a hundredth of a degree), its range. */
		void JudgeRamp(FName Phase, bool bDown, TArray<FString>& Failures, TArray<FString>& Lines)
		{
			int32 First = 0;
			int32 End = 0;
			if (!Recorder.FindPhase(Phase, First, End) || End - First < 3)
			{
				Failures.Add(FString::Printf(TEXT("%s: no frames"), *Phase.ToString()));
				return;
			}
			const TArray<FScctFrameSample>& Rows = Recorder.GetSamples();
			int32 Wrong = 0;
			int32 Moving = 0;
			int32 HeldFrames = 0;
			float Start = Rows[First].Fov;
			float Low = Start;
			float High = Start;
			bool bStarted = false;
			bool bEnded = false;
			for (int32 Index = First + 1; Index < End; ++Index)
			{
				const float Step = Rows[Index].Fov - Rows[Index - 1].Fov;
				Low = FMath::Min(Low, Rows[Index].Fov);
				High = FMath::Max(High, Rows[Index].Fov);
				const bool bForward = bDown ? Step < -0.01f : Step > 0.01f;
				const bool bBackward = bDown ? Step > 0.01f : Step < -0.01f;
				if (bBackward)
				{
					++Wrong;
				}
				if (bForward)
				{
					if (bEnded)
					{
						++Wrong; // a second ramp after the first had stopped
					}
					bStarted = true;
					++Moving;
				}
				else if (bStarted && !bEnded)
				{
					// A held frame inside the ramp, or its end: it has ended once the ramp holds for good.
					bool bMovesLater = false;
					for (int32 Later = Index + 1; Later < End && !bMovesLater; ++Later)
					{
						const float LaterStep = Rows[Later].Fov - Rows[Later - 1].Fov;
						bMovesLater = bDown ? LaterStep < -0.01f : LaterStep > 0.01f;
					}
					if (bMovesLater)
					{
						++HeldFrames;
					}
					else
					{
						bEnded = true;
					}
				}
			}
			const FString Line = FString::Printf(TEXT("%s: fov %.1f..%.1f over %d frames, ramp frames=%d held=%d wrong-way=%d"),
				*Phase.ToString(), Low, High, End - First, Moving, HeldFrames, Wrong);
			UE_LOG(LogScctTest, Display, TEXT("SCCT_SMOOTH %s"), *Line);
			Lines.Add(FString::Printf(TEXT("%s=%d/%d/%d"), *Phase.ToString(), Moving, HeldFrames, Wrong));
			if (High - Low < 5.0f || Moving == 0)
			{
				Failures.Add(FString::Printf(TEXT("%s: the field of view did not ramp (%.1f..%.1f)"), *Phase.ToString(), Low, High));
			}
			else if (Wrong > 0)
			{
				Failures.Add(FString::Printf(TEXT("%s: the zoom went the wrong way on %d frames"), *Phase.ToString(), Wrong));
			}
			else if (HeldFrames > FMath::Max(1, Moving / 10))
			{
				Failures.Add(FString::Printf(TEXT("%s: the zoom ramp held still on %d of its %d frames"), *Phase.ToString(), HeldFrames, Moving + HeldFrames));
			}
		}

		FScctTestResult Verdict()
		{
			TArray<FString> Failures;
			TArray<FString> Lines;
			int32 First = 0;
			int32 End = 0;
			Recorder.FindPhase(TEXT("walk-turn"), First, End);
			const FScctFrameTimeStats Times = Recorder.FrameTimes(First, End);
			const FScctMotionStats TurnStats = Recorder.CameraTurn(First, End);
			const FScctMotionStats CameraMove = Recorder.CameraMove(First, End);
			const FScctMotionStats Body = Recorder.BodyMove(First, End);
			const FScctMotionStats ViewModel = Recorder.ViewModel(First, End);
			UE_LOG(LogScctTest, Display, TEXT("SCCT_SMOOTH walk-turn: cap=%d achieved=%.1ffps median=%.2fms p99=%.2fms"), Cap, Times.Fps(), Times.MedianMs, Times.P99Ms);
			UE_LOG(LogScctTest, Display, TEXT("SCCT_SMOOTH camera turn: %s"), *TurnStats.ToString());
			UE_LOG(LogScctTest, Display, TEXT("SCCT_SMOOTH camera move: %s"), *CameraMove.ToString());
			UE_LOG(LogScctTest, Display, TEXT("SCCT_SMOOTH body move: %s"), *Body.ToString());
			UE_LOG(LogScctTest, Display, TEXT("SCCT_SMOOTH view model: %s"), *ViewModel.ToString());

			if (Times.Fps() < MinimumFps)
			{
				Failures.Add(FString::Printf(TEXT("%.1f fps with the cap at %d: not above 60 Hz, nothing to judge"), Times.Fps(), Cap));
			}
			const auto CheckSmoothMotion = [&Failures](const TCHAR* What, const FScctMotionStats& Stats, double JitterLimit)
			{
				if (Stats.Moving < 30)
				{
					Failures.Add(FString::Printf(TEXT("%s: moved on only %d frames"), What, Stats.Moving));
				}
				else if (Stats.HeldShare() > HeldShareLimit)
				{
					Failures.Add(FString::Printf(TEXT("%s held still on %d of %d frames (steps at the tick rate)"), What, Stats.Held, Stats.Moving));
				}
				else if (JitterLimit > 0.0 && Stats.P99Jitter > JitterLimit)
				{
					Failures.Add(FString::Printf(TEXT("%s jitter p99 %.3f over %.2f"), What, Stats.P99Jitter, JitterLimit));
				}
			};
			CheckSmoothMotion(TEXT("camera turn"), TurnStats, TurnJitterLimit);
			CheckSmoothMotion(TEXT("camera move"), CameraMove, MoveJitterLimit);
			CheckSmoothMotion(TEXT("body"), Body, MoveJitterLimit);
			// The view model's clips play at their own pace (a walk bob speeds up and slows down): only that it advances.
			CheckSmoothMotion(TEXT("view model"), ViewModel, 0.0);

			if (!bScoped)
			{
				Failures.Add(TEXT("the HUD never showed the merc's scope after SwitchBinocularVision"));
			}
			JudgeRamp(TEXT("scope-in"), true, Failures, Lines);
			JudgeRamp(TEXT("scope-out"), false, Failures, Lines);

			if (Failures.Num() > 0)
			{
				return FScctTestResult::Fail(FString::Join(Failures, TEXT("; ")));
			}
			return FScctTestResult::Pass(FString::Printf(TEXT("fps=%.1f turnJitterP99=%.3f camMoveJitterP99=%.3f bodyJitterP99=%.3f held turn/cam/body/viewmodel=%d/%d/%d/%d zoom %s"),
				Times.Fps(), TurnStats.P99Jitter, CameraMove.P99Jitter, Body.P99Jitter, TurnStats.Held, CameraMove.Held, Body.Held, ViewModel.Held, *FString::Join(Lines, TEXT(" "))));
		}

		virtual void Restore(FScctTestContext& Context) override
		{
			Context.RunConsoleCommand(FString::Printf(TEXT("t.MaxFPS %g"), SavedMaxFps));
			Context.RunConsoleCommand(FString::Printf(TEXT("r.VSync %d"), SavedVSync));
		}

		EStep StepNow = EStep::Begin;
		int32 Cap = 120;
		bool bScoped = false;
		float SavedMaxFps = 0.0f;
		int32 SavedVSync = 0;
	};

	// =================================================================================================================

	/**
	 * play-hitch: the spy with three bots plays the map (the Godot client's PlayHitchSmoke), walking forward and back
	 * and turning, uncapped. After WarmupSeconds (the first replication, spawns, the first pictures) it measures
	 * -ScctHitchSeconds (60 by default). Every frame over 33.4 ms is listed with what ran in it (the thread times,
	 * garbage collections and their time, pipeline states created on the spot, shader compile jobs), and the test fails
	 * when there are more of them than -ScctHitchBudget (0 by default). -ScctTestStall=<ms> runs 'scct.TestStall <ms>'
	 * halfway through, an artificial hitch to see it fail.
	 */
	class FScctPlayHitchTest : public FScctFrameTestBase
	{
	public:
		static constexpr double WarmupSeconds = 5.0;
		static constexpr int32 MaxListed = 20;

		FScctPlayHitchTest()
		{
			Side = TEXT("spy");
			Bots = 3;
		}

		virtual void Start(FScctTestContext& Context) override
		{
			FParse::Value(FCommandLine::Get(), TEXT("ScctHitchSeconds="), MeasureSeconds);
			FParse::Value(FCommandLine::Get(), TEXT("ScctHitchBudget="), Budget);
			FParse::Value(FCommandLine::Get(), TEXT("ScctTestStall="), StallMs);
			MeasureSeconds = FMath::Max(MeasureSeconds, 1.0);
		}

	protected:
		virtual FScctTestResult StepPlaying(FScctTestContext& Context, double Seconds) override
		{
			if (Recorder.GetPhase().IsNone())
			{
				Enter(TEXT("warmup"), Seconds);
			}
			if (Seconds >= WarmupSeconds && Recorder.GetPhase() == FName(TEXT("warmup")))
			{
				Enter(TEXT("measure"), Seconds);
			}
			const double Measured = Recorder.GetPhase() == FName(TEXT("measure")) ? Seconds - PhaseStart : -1.0;
			if (StallMs > 0.0f && !bStalled && Measured >= MeasureSeconds * 0.5)
			{
				bStalled = true;
				Context.RunConsoleCommand(FString::Printf(TEXT("scct.TestStall %g"), StallMs));
			}

			// The spy roams: forward and back, turning the other way every few seconds.
			Walk(Context, Seconds, 2.0);
			Turn(Context, FMath::FloorToInt(Seconds / 4.0) % 2 == 0 ? 300.0f : -300.0f);
			return Measured >= MeasureSeconds ? Verdict() : FScctTestResult::Running();
		}

		FScctTestResult Verdict()
		{
			int32 First = 0;
			int32 End = 0;
			Recorder.FindPhase(TEXT("measure"), First, End);
			const FScctFrameTimeStats Times = Recorder.FrameTimes(First, End);
			const TArray<FScctFrameSample>& Rows = Recorder.GetSamples();

			TArray<FString> Slow;
			int32 Collections = 0;
			double CollectMs = 0.0;
			int32 WithCollection = 0;
			int32 WithPso = 0;
			int32 WithShaders = 0;
			for (int32 Index = First; Index < End; ++Index)
			{
				const FScctFrameSample& Row = Rows[Index];
				Collections += Row.Collections;
				CollectMs += Row.CollectMs;
				if (Row.FrameMs > HitchMs)
				{
					WithCollection += Row.Collections > 0 ? 1 : 0;
					WithPso += Row.PsoCreated > 0 ? 1 : 0;
					WithShaders += Row.ShaderJobs > 0 || Row.ShadersDone > 0 ? 1 : 0;
					Slow.Add(FScctFrameRecorder::Describe(Row));
				}
			}

			const double Minutes = FMath::Max(Times.Frames > 0 ? Rows[End - 1].Time - Rows[First].Time : 0.0, 1.0) / 60.0;
			const FString Summary = FString::Printf(TEXT("seconds=%.0f frames=%d fps=%.1f frame ms median=%.1f p99=%.1f max=%.1f over%.1f=%d (gc %d, pso %d, shaders %d) budget=%d gc/min=%.1f gcMs/min=%.1f"),
				Minutes * 60.0, Times.Frames, Times.Fps(), Times.MedianMs, Times.P99Ms, Times.MaxMs, HitchMs, Slow.Num(), WithCollection, WithPso, WithShaders, Budget,
				Collections / Minutes, CollectMs / Minutes);
			for (int32 Index = 0; Index < Slow.Num(); ++Index)
			{
				UE_LOG(LogScctTest, Display, TEXT("SCCT_HITCH slow %s"), *Slow[Index]);
			}
			UE_LOG(LogScctTest, Display, TEXT("SCCT_HITCH summary %s"), *Summary);

			if (Times.Frames == 0)
			{
				return FScctTestResult::Fail(TEXT("no frames were measured"));
			}
			if (Slow.Num() > Budget)
			{
				FString Reason = FString::Printf(TEXT("%d frames over %.1f ms (budget %d): %s"), Slow.Num(), HitchMs, Budget, *Summary);
				for (int32 Index = 0; Index < FMath::Min(MaxListed, Slow.Num()); ++Index)
				{
					Reason += TEXT("\n  ") + Slow[Index];
				}
				return FScctTestResult::Fail(Reason);
			}
			return FScctTestResult::Pass(Summary);
		}

		double MeasureSeconds = 60.0;
		int32 Budget = 0;
		float StallMs = 0.0f;
		bool bStalled = false;
	};

	// =================================================================================================================

	/**
	 * first-use: the merc alone on the map, standing and looking a little up. After a warm-up, each first occurrence of
	 * something the player does is triggered and the frames after it are measured: the first rifle shot (fire held a
	 * quarter of a second), the first grenade (alt-fire, the merc's gadget throw, measured to its explosion), the first
	 * vision switch (SwitchHeadset 1 through its bound key; 2, then the cycle key, when the map withholds the first), the first switch to the
	 * side's other vision mode and the first burst of the map's emitters (every map Emitter's tag fired
	 * through the bridge's check table, as -ScctLevelDo=emitters does). Each is then done a second time. A first
	 * occurrence fails when its worst frame is over the median frame of the second before it by more than
	 * -ScctFirstUseBudgetMs (33.4 ms by default: one more hitch-sized frame); each is also checked to have
	 * happened (the ammunition went down, the gadget was thrown, the vision mode changed, emitters were reached).
	 * The Godot client's GameplayFirstUseSmoke, GameplayFxWarmupSmoke and StartupWeaponsSmoke are the reference.
	 */
	class FScctFirstUseTest : public FScctFrameTestBase
	{
	public:
		static constexpr double WarmupSeconds = 5.0;
		static constexpr double BaselineSeconds = 1.0;
		static constexpr double WindowSeconds = 2.0;

		/** A grenade's window runs to its explosion and the first frames of its smoke and sound. */
		static constexpr double GrenadeWindowSeconds = 4.5;

		/** Mouse counts that tilt the view up (about 30 degrees), so a grenade flies far from the merc's feet. */
		static constexpr float LookUpCounts = 1900.0f;

		virtual void Start(FScctTestContext& Context) override
		{
			FParse::Value(FCommandLine::Get(), TEXT("ScctFirstUseBudgetMs="), BudgetMs);
		}

	protected:
		enum class EEvent : uint8 { Shot, Grenade, Vision, OtherVision, Emitters, Count };

		static const TCHAR* EventName(EEvent Event)
		{
			switch (Event)
			{
			case EEvent::Shot: return TEXT("shot");
			case EEvent::Grenade: return TEXT("grenade");
			case EEvent::Vision: return TEXT("vision");
			case EEvent::OtherVision: return TEXT("other-vision");
			default: return TEXT("emitters");
			}
		}

		/** The vision commands tried in turn for the first switch (see SPlayerController.SwitchHeadset: 3 cycles). */
		static constexpr const TCHAR* VisionCommands[] = { TEXT("SwitchHeadset 1"), TEXT("SwitchHeadset 2"), TEXT("SwitchHeadset 3") };

		/** The command that switches from the mode the first switch found to the side's other one (the cycle key again when only that worked). */
		const TCHAR* OtherVisionCommand() const { return VisionTry == 0 ? TEXT("SwitchHeadset 2") : VisionTry == 1 ? TEXT("SwitchHeadset 1") : TEXT("SwitchHeadset 3"); }

		/** Plain sight is 0 or 5 on the HUD. */
		static int32 NormalVision(int32 Mode) { return Mode == 5 ? 0 : Mode; }

		struct FRun
		{
			EEvent Event = EEvent::Shot;
			bool bFirst = true;
		};

		virtual FScctTestResult StepPlaying(FScctTestContext& Context, double Seconds) override
		{
			if (Runs.Num() == 0)
			{
				for (int32 Pass = 0; Pass < 2; ++Pass)
				{
					for (int32 Event = 0; Event < static_cast<int32>(EEvent::Count); ++Event)
					{
						Runs.Add(FRun{ static_cast<EEvent>(Event), Pass == 0 });
					}
				}
				Enter(TEXT("warmup"), Seconds);
			}

			const double InPhase = Seconds - PhaseStart;
			if (Current < 0 && LookedUp < LookUpCounts)
			{
				// Over the first second of the warm-up: the view tilts up, as a player aiming a throw would.
				if (AScctPlayerController* Player = Context.GetPlayer())
				{
					const float Counts = FMath::Min(LookUpCounts - LookedUp, LookUpCounts * static_cast<float>(FApp::GetDeltaTime()));
					Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, Counts, 1));
					LookedUp += Counts;
				}
			}
			const FScctHudModel* Hud = GetHud(Context);
			if (Hud != nullptr && Current >= 0 && bInWindow)
			{
				Observe(*Hud);
			}
			if (ReleaseAt > 0.0 && InPhase >= ReleaseAt)
			{
				ReleaseAt = 0.0;
				Hold(Context, Runs[Current].Event == EEvent::Shot ? TEXT("fire") : TEXT("altfire"), false);
			}

			if (Current < 0)
			{
				if (InPhase >= WarmupSeconds - BaselineSeconds)
				{
					Next(Context, Seconds, Hud);
				}
				return FScctTestResult::Running();
			}

			if (!bInWindow)
			{
				// The quiet second before the event: its median frame is the event's baseline.
				if (InPhase >= BaselineSeconds)
				{
					if (!Trigger(Context, Seconds, Hud))
					{
						return FScctTestResult::Fail(Problem);
					}
				}
				return FScctTestResult::Running();
			}

			if (Runs[Current].Event == EEvent::Vision && !bHappened && Runs[Current].bFirst && VisionTry + 1 < UE_ARRAY_COUNT(VisionCommands) && InPhase >= 0.5 * (VisionTry + 1) && Context.Frame - TriggerFrame >= 20 * (VisionTry + 1))
			{
				// The side's first mode may be withheld on this map (the game's rules): the second, then the cycle key.
				++VisionTry;
				Tap(Context, VisionCommands[VisionTry]);
			}

			if (InPhase >= (Runs[Current].Event == EEvent::Grenade ? GrenadeWindowSeconds : WindowSeconds))
			{
				Judge();
				if (Current + 1 >= Runs.Num())
				{
					return Verdict();
				}
				Next(Context, Seconds, Hud);
			}
			return FScctTestResult::Running();
		}

		FName PhaseName(const FRun& Run, bool bWindow) const
		{
			return *FString::Printf(TEXT("%s-%s%s"), Run.bFirst ? TEXT("first") : TEXT("repeat"), EventName(Run.Event), bWindow ? TEXT("") : TEXT("-before"));
		}

		void Next(FScctTestContext& Context, double Seconds, const FScctHudModel* Hud)
		{
			++Current;
			bInWindow = false;
			Enter(PhaseName(Runs[Current], false), Seconds);
		}

		bool Trigger(FScctTestContext& Context, double Seconds, const FScctHudModel* Hud)
		{
			const FRun& Run = Runs[Current];
			bInWindow = true;
			bHappened = false;
			Reached = 0;
			if (Hud != nullptr)
			{
				AmmoBefore = Hud->State.Ammo;
				VisionBefore = NormalVision(Hud->State.VisionMode);
				GadgetCharges.Reset();
				for (const FScctHudGadget& Gadget : Hud->Gadgets)
				{
					GadgetCharges.Add(Gadget.Charge);
				}
			}
			Enter(PhaseName(Run, true), Seconds);
			TriggerFrame = Context.Frame;
			switch (Run.Event)
			{
			case EEvent::Shot:
				Hold(Context, TEXT("fire"), true);
				ReleaseAt = 0.25;
				break;
			case EEvent::Grenade:
				Hold(Context, TEXT("altfire"), true);
				ReleaseAt = 0.6; // a longer hold throws further
				break;
			case EEvent::Vision:
				// The first switch turns a mode on; the repeat (the key that worked, again) comes back to it from the other mode.
				if (Run.bFirst)
				{
					VisionTry = 0;
				}
				if (!Tap(Context, VisionCommands[VisionTry]))
				{
					Problem = FString::Printf(TEXT("no key of the merc's bindings switches the vision (%s)"), VisionCommands[VisionTry]);
					return false;
				}
				break;
			case EEvent::OtherVision:
				// The side's other mode, from the first: its own material's first picture (each pass switches over again).
				if (!Tap(Context, OtherVisionCommand()))
				{
					Problem = FString::Printf(TEXT("no key of the merc's bindings switches the vision (%s)"), OtherVisionCommand());
					return false;
				}
				break;
			case EEvent::Emitters:
			{
				FString Error;
				const FScctWorldDebugApi* Debug = static_cast<const FScctWorldDebugApi*>(FOpenSCCTModule::Get().FindFeatureTable(
					TEXT("OpenScct.UnrealBridge.WorldDebugExports"), sizeof(FScctWorldDebugApi), ScctWorldDebugApiVersion, Error));
				if (Debug == nullptr)
				{
					Problem = FString::Printf(TEXT("the bridge's check table is missing: %s"), *Error);
					return false;
				}
				Reached = Debug->FireEmitters();
				bHappened = Reached > 0;
				break;
			}
			default:
				break;
			}
			return true;
		}

		/** Whether the event has shown on the HUD: the clip went down, a gadget's charge changed, the vision mode changed. */
		void Observe(const FScctHudModel& Hud)
		{
			switch (Runs[Current].Event)
			{
			case EEvent::Shot:
				bHappened |= Hud.State.Ammo < AmmoBefore;
				break;
			case EEvent::Grenade:
				bHappened |= Hud.State.Ballistic > 0.0f;
				for (int32 Index = 0; Index < FMath::Min(GadgetCharges.Num(), Hud.Gadgets.Num()); ++Index)
				{
					bHappened |= Hud.Gadgets[Index].Charge != GadgetCharges[Index];
				}
				break;
			case EEvent::Vision:
			case EEvent::OtherVision:
				if (NormalVision(Hud.State.VisionMode) != VisionBefore)
				{
					bHappened = true;
					VisionAfter = NormalVision(Hud.State.VisionMode);
				}
				break;
			default:
				break;
			}
		}

		void Judge()
		{
			const FRun& Run = Runs[Current];
			int32 BeforeFirst = 0, BeforeEnd = 0, First = 0, End = 0;
			Recorder.FindPhase(PhaseName(Run, false), BeforeFirst, BeforeEnd);
			Recorder.FindPhase(PhaseName(Run, true), First, End);
			const FScctFrameTimeStats Before = Recorder.FrameTimes(BeforeFirst, BeforeEnd);
			const TArray<FScctFrameSample>& Rows = Recorder.GetSamples();
			int32 Worst = INDEX_NONE;
			for (int32 Index = First; Index < End; ++Index)
			{
				if (Worst == INDEX_NONE || Rows[Index].FrameMs > Rows[Worst].FrameMs)
				{
					Worst = Index;
				}
			}
			const double WorstMs = Worst != INDEX_NONE ? Rows[Worst].FrameMs : 0.0;
			const double Over = WorstMs - Before.MedianMs;
			const FString Label = PhaseName(Run, true).ToString();
			FString Extra;
			if (Run.Event == EEvent::Emitters)
			{
				Extra = FString::Printf(TEXT(" emittersReached=%d"), Reached);
			}
			else if (Run.Event == EEvent::OtherVision)
			{
				Extra = FString::Printf(TEXT(" vision %d->%d (%s)"), VisionBefore, VisionAfter, OtherVisionCommand());
			}
			else if (Run.Event == EEvent::Vision)
			{
				Extra = FString::Printf(TEXT(" vision %d->%d (%s)"), VisionBefore, VisionAfter, VisionCommands[VisionTry]);
			}
			UE_LOG(LogScctTest, Display, TEXT("SCCT_FIRST_USE %s: median before=%.2fms worst=%.2fms (+%.2fms) happened=%s%s worst frame: %s"),
				*Label, Before.MedianMs, WorstMs, Over, bHappened ? TEXT("yes") : TEXT("no"), *Extra,
				Worst != INDEX_NONE ? *FScctFrameRecorder::Describe(Rows[Worst]) : TEXT("-"));
			Summary.Add(FString::Printf(TEXT("%s=+%.1fms"), *Label, Over));
			// Hitches in the quiet second before: frames as slow with nothing new happening are the machine's, not the event's.
			for (int32 Index = BeforeFirst; Index < BeforeEnd; ++Index)
			{
				QuietHitches += Rows[Index].FrameMs > HitchMs ? 1 : 0;
			}
			if (Run.bFirst && Before.MedianMs > HitchMs)
			{
				Failures.Add(FString::Printf(TEXT("%s: the quiet second before ran at %.0f ms a frame, too slow to measure a first use (a busy machine?)"), *Label, Before.MedianMs));
			}
			else if (!bHappened)
			{
				Failures.Add(FString::Printf(TEXT("%s: it did not happen (nothing on the HUD or in the level showed it)"), *Label));
			}
			else if (Run.bFirst && Over > BudgetMs)
			{
				Failures.Add(FString::Printf(TEXT("%s: worst frame %.1f ms, %.1f ms over the median %.1f ms (budget %.1f ms)"), *Label, WorstMs, Over, Before.MedianMs, BudgetMs));
			}
		}

		FScctTestResult Verdict()
		{
			if (Failures.Num() > 0)
			{
				return FScctTestResult::Fail(FString::Join(Failures, TEXT("; ")) + FString::Printf(TEXT(" | quietHitches=%d "), QuietHitches) + FString::Join(Summary, TEXT(" ")));
			}
			return FScctTestResult::Pass(FString::Printf(TEXT("budget=%.1fms quietHitches=%d %s"), BudgetMs, QuietHitches, *FString::Join(Summary, TEXT(" "))));
		}

		TArray<FRun> Runs;
		int32 Current = -1;
		bool bInWindow = false;
		double ReleaseAt = 0.0;
		/** Frames over 33.4 ms in the quiet seconds before the events: a busy machine shows here. */
		int32 QuietHitches = 0;
		/** The runner's frame the event was triggered on (a vision key is retried only after enough frames for the first to show). */
		int32 TriggerFrame = 0;
		float LookedUp = 0.0f;
		int32 VisionTry = 0;
		bool bHappened = false;
		int32 Reached = 0;
		int32 AmmoBefore = 0;
		int32 VisionBefore = 0;
		int32 VisionAfter = 0;
		TArray<int32> GadgetCharges;
		double BudgetMs = HitchMs;
		FString Problem;
		TArray<FString> Failures;
		TArray<FString> Summary;
	};
}

SCCT_TEST("frame-rate", FScctFrameRateTest);
SCCT_TEST("smooth", FScctSmoothTest);
SCCT_TEST("play-hitch", FScctPlayHitchTest);
SCCT_TEST("first-use", FScctFirstUseTest);
