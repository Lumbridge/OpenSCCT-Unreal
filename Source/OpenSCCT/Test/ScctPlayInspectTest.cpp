// play-inspect and play-inspect-merc: a match on the run's map (-ScctTestMap, AquaE by default) with three bots, played
// at the keys for about 40 s with every play inspector check on (Test/ScctPlayInspector.h), and failed on any issue.
// The Godot client's PlayInspectSmoke (game/Scripts/Smoke/PlayInspectSmoke.cs).

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Hud/ScctHudSubsystem.h"
#include "InputKeyEventArgs.h"
#include "Menu/ScctMatchMenuWidget.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Test/ScctInspectApi.h"
#include "Test/ScctPlayInspector.h"
#include "Test/ScctTest.h"
#include "Test/ScctTestUi.h"
#include "UnrealClient.h"

namespace ScctPlayInspectTestLocal
{
	/** Seconds a map may take to load and its match to give the local player a body. */
	constexpr double InspectLoadSeconds = 150.0;

	/** Seconds the match plays before the keys start (the spawn settles, the bots set off). */
	constexpr double InspectSettleSeconds = 2.0;

	/** A walking step that moves the local pawn less than this (uu) means the keys do not reach it. */
	constexpr float InspectMinWalk = 100.0f;

	/** With the menu open, a held key may move the pawn no more than this (uu): the deceleration of a stop. */
	constexpr float InspectMenuDrift = 20.0f;

	/** Mouse counts a second for a look step: some 65 degrees a second at the default sensitivity. */
	constexpr float InspectLookCounts = 4000.0f;

	enum class EInspectStepKind : uint8
	{
		Keys,
		MenuOpen,
		MenuClose,
	};

	enum class EInspectCheck : uint8
	{
		None,
		Moves,      // a held movement key must move the pawn (unless a wall is right there)
		Crouches,   // the tap must crouch the pawn
		Stands,     // the tap must stand it up
		Jumps,      // the tap must make it fall
		Fires,      // the held fire must spend ammunition and start a sound
	};

	struct FInspectStep
	{
		const TCHAR* Name;
		double Seconds;
		const TCHAR* Hold1 = nullptr;
		const TCHAR* Hold2 = nullptr;
		const TCHAR* Tap = nullptr;
		float LookX = 0.0f;
		float LookY = 0.0f;
		EInspectCheck Check = EInspectCheck::None;
		bool bBackwards = false;
		EInspectStepKind Kind = EInspectStepKind::Keys;
	};

	/**
	 * The keys the inspector plays (PlayInspectSmoke's timeline): walks that go back and forth over the same line so they
	 * stay in the space already walked, looks that are undone, crouching, jumps (the spy's), a burst of fire (the
	 * merc's), and the in-match menu opened with the ShowMenu command and closed with its RESUME button.
	 */
	TArray<FInspectStep> MakeInspectTimeline(bool bMerc, bool bCrouched)
	{
		TArray<FInspectStep> Steps;
		if (bCrouched)
		{
			Steps.Add({ TEXT("stand up"), 1.0, nullptr, nullptr, TEXT("crouch"), 0.0f, 0.0f, EInspectCheck::Stands });
		}
		Steps.Add({ TEXT("walk"), 3.0, TEXT("forward"), nullptr, nullptr, 0.0f, 0.0f, EInspectCheck::Moves });
		Steps.Add({ TEXT("look around"), 2.0, nullptr, nullptr, nullptr, InspectLookCounts, 0.0f });
		Steps.Add({ TEXT("look back"), 2.0, nullptr, nullptr, nullptr, -InspectLookCounts, 0.0f });
		Steps.Add({ TEXT("walk back"), 2.5, TEXT("back"), nullptr, nullptr, 0.0f, 0.0f, EInspectCheck::Moves, true });
		Steps.Add({ TEXT("strafe left"), 1.5, TEXT("left") });
		Steps.Add({ TEXT("strafe right"), 1.5, TEXT("right") });
		Steps.Add({ TEXT("walk turning"), 2.5, TEXT("forward"), nullptr, nullptr, InspectLookCounts * 0.6f, 0.0f });
		Steps.Add({ TEXT("crouch"), 1.2, nullptr, nullptr, TEXT("crouch"), 0.0f, 0.0f, EInspectCheck::Crouches });
		Steps.Add({ TEXT("crouch walk"), 3.0, TEXT("forward"), nullptr, nullptr, InspectLookCounts * 0.35f, 0.0f, EInspectCheck::Moves });
		Steps.Add({ TEXT("crouch still"), 1.5 });
		Steps.Add({ TEXT("stand"), 1.6, nullptr, nullptr, TEXT("crouch"), 0.0f, 0.0f, EInspectCheck::Stands });
		if (!bMerc)
		{
			Steps.Add({ TEXT("jump"), 1.8, nullptr, nullptr, TEXT("jump"), 0.0f, 0.0f, EInspectCheck::Jumps });
			Steps.Add({ TEXT("jump forward"), 2.2, TEXT("forward"), nullptr, TEXT("jump"), 0.0f, 0.0f, EInspectCheck::Jumps });
		}
		Steps.Add({ TEXT("look up"), 1.0, nullptr, nullptr, nullptr, 0.0f, InspectLookCounts * 0.6f });
		Steps.Add({ TEXT("look down"), 1.0, nullptr, nullptr, nullptr, 0.0f, -InspectLookCounts * 0.6f });
		if (bMerc)
		{
			Steps.Add({ TEXT("fire"), 1.5, TEXT("fire"), nullptr, nullptr, 0.0f, 0.0f, EInspectCheck::Fires });
		}
		FInspectStep Open{ TEXT("menu"), 2.0 };
		Open.Kind = EInspectStepKind::MenuOpen;
		Steps.Add(Open);
		FInspectStep Close{ TEXT("resume"), 1.5 };
		Close.Kind = EInspectStepKind::MenuClose;
		Steps.Add(Close);
		Steps.Add({ TEXT("walk after menu"), 2.0, TEXT("forward"), nullptr, nullptr, 0.0f, 0.0f, EInspectCheck::Moves });
		Steps.Add({ TEXT("settle"), 1.0 });
		return Steps;
	}

	/**
	 * Opens the map with three bots on the given side, waits for the local body and the camera, lets the match settle,
	 * then plays the timeline with the inspector attached. At each step's end it judges what the step did to the local
	 * pawn (the Input area), the menu steps judge the PauseMenu area, and at the end every issue is printed on its own
	 * SCCT_INSPECT line with one summary line per area; the test passes only with no issue.
	 *
	 * -ScctInspectShots saves a picture (interface included) at each step's start to Saved/ScctShots/<test>/.
	 */
	class FScctPlayInspectTest : public IScctTest
	{
	public:
		explicit FScctPlayInspectTest(bool bInMerc) : bMerc(bInMerc) {}

		virtual ~FScctPlayInspectTest() override = default;

		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (!bOpened)
			{
				if (Context.GetWorld() == nullptr)
				{
					return FScctTestResult::Running();
				}
				bOpened = true;
				bShots = FParse::Param(FCommandLine::Get(), TEXT("ScctInspectShots"));
				Map = FScctTestContext::GetTestMap(TEXT("AquaE"));
				FString Error;
				const FString URL = FString::Printf(TEXT("/Game/Imported/Maps/%s/%s?Side=%s?Bots=3"), *Map, *Map, bMerc ? TEXT("merc") : TEXT("spy"));
				if (!Context.OpenLevel(URL, &Error))
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("%s (import the map with Tools/Import/Import-Map.ps1)"), *Error));
				}
				return FScctTestResult::Running();
			}

			UScctSessionSubsystem* Session = Context.GetSession();
			AScctPlayerController* Player = Context.GetPlayer();
			const bool bReady = Context.GetLevelName() == Map && Session != nullptr && Session->IsPlaying()
				&& Session->FindLocalProxy() != nullptr && Player != nullptr && Player->PlayerCameraManager != nullptr;
			if (ReadyAt < 0.0)
			{
				if (!bReady)
				{
					return Context.ElapsedSeconds > InspectLoadSeconds
						? FScctTestResult::Fail(FString::Printf(TEXT("no local body on %s after %.0f s"), *Map, InspectLoadSeconds))
						: FScctTestResult::Running();
				}
				ReadyAt = Context.ElapsedSeconds;
				SessionId = Session->GetSessionId();
				FString Error;
				if (!Inspector.Attach(Context.GetWorld(), bMerc ? 1 : 0, &Error))
				{
					return FScctTestResult::Fail(Error);
				}
				// The game counts as having the keys from here on, though its window has no focus (a run off-screen).
				Context.HoldAction(TEXT("firstperson"), false);
				UE_LOG(LogScctTest, Log, TEXT("%s: the local player has a body on %s at %.1f s"), *GetName(), *Map, Context.ElapsedSeconds);
				return FScctTestResult::Running();
			}
			if (Session == nullptr || Session->GetSessionId() != SessionId || Player == nullptr)
			{
				return Finish(Context, FScctTestResult::Fail(TEXT("the match ended while the test ran")));
			}

			FScctInspectPawn Local = {};
			const bool bHaveLocal = Inspector.GetLocal(Local);
			const double Played = Context.ElapsedSeconds - ReadyAt;
			if (Steps.Num() == 0)
			{
				if (Played < InspectSettleSeconds || !bHaveLocal)
				{
					return Played > InspectSettleSeconds + 20.0
						? Finish(Context, FScctTestResult::Fail(TEXT("the simulation has no local pawn")))
						: FScctTestResult::Running();
				}
				Steps = MakeInspectTimeline(bMerc, (Local.Flags & EScctInspectPawnFlags::Crouched) != 0);
				StepIndex = -1;
				UE_LOG(LogScctTest, Log, TEXT("%s: %d steps at the keys (%s, %s)"), *GetName(), Steps.Num(), bMerc ? TEXT("merc") : TEXT("spy"),
					(Local.Flags & EScctInspectPawnFlags::Crouched) != 0 ? TEXT("spawned crouched") : TEXT("spawned standing"));
			}

			// The step in progress, or the next one.
			if (StepIndex < 0 || Context.ElapsedSeconds - StepStart >= Steps[StepIndex].Seconds)
			{
				if (StepIndex >= 0)
				{
					EndStep(Context, Steps[StepIndex], bHaveLocal ? &Local : nullptr);
				}
				++StepIndex;
				if (StepIndex >= Steps.Num())
				{
					return Finish(Context, Verdict());
				}
				BeginStep(Context, Steps[StepIndex], bHaveLocal ? &Local : nullptr);
			}

			RunStep(Context, Steps[StepIndex], bHaveLocal ? &Local : nullptr);
			return FScctTestResult::Running();
		}

	private:
		void BeginStep(FScctTestContext& Context, const FInspectStep& Info, const FScctInspectPawn* Local)
		{
			StepStart = Context.ElapsedSeconds;
			Inspector.SetStep(Info.Name);
			bAirborne = false;
			bTapReleased = Info.Tap == nullptr;
			StepStartLocation = Local != nullptr ? FVector(Local->X, Local->Y, Local->Z) : FVector::ZeroVector;
			StepLast = StepStartLocation;
			StepPath = 0.0f;
			StepVoices = Inspector.GetVoicesStarted();
			StepCorrections = Inspector.GetCorrectionsApplied();
			StepAmmo = GetAmmo(Context);
			bMenuSeen = bMenuBlocked = bMenuClosed = bMenuHeld = false;
			UE_LOG(LogScctTest, Log, TEXT("%s: step '%s' (%.1f s) at %.1f s"), *GetName(), Info.Name, Info.Seconds, Inspector.GetSeconds());

			// What the step before held and this one does not is let go; this step's keys are held.
			TSet<FString> Wanted;
			for (const TCHAR* Action : { Info.Hold1, Info.Hold2 })
			{
				if (Action != nullptr)
				{
					Wanted.Add(Action);
				}
			}
			for (const FString& Action : Held.Array())
			{
				if (!Wanted.Contains(Action))
				{
					Hold(Context, Action, false);
				}
			}
			for (const FString& Action : Wanted)
			{
				Hold(Context, Action, true);
			}
			if (Info.Tap != nullptr)
			{
				Context.HoldAction(Info.Tap, true);
			}

			AScctPlayerController* Player = Context.GetPlayer();
			if (Info.Kind == EInspectStepKind::MenuOpen && Player != nullptr)
			{
				UScctMatchMenuWidget* Menu = ScctTestUi::FindShownWidget<UScctMatchMenuWidget>(Context.GetWorld());
				if (Menu == nullptr)
				{
					Inspector.Note(EScctInspectArea::PauseMenu, TEXT("no menu widget"), TEXT("the match level shows no in-match menu widget"));
				}
				else if (Menu->IsMenuOpen())
				{
					Inspector.Note(EScctInspectArea::PauseMenu, TEXT("open before"), TEXT("the in-match menu was open before ShowMenu was given"));
				}
				// The player's ShowMenu binding, as the controller raises it on the key's press and release.
				Player->OnLocalCommand.Broadcast(FName(TEXT("ShowMenu")), 0, true);
				Player->OnLocalCommand.Broadcast(FName(TEXT("ShowMenu")), 0, false);
			}
			else if (Info.Kind == EInspectStepKind::MenuClose)
			{
				FString Error;
				UScctMatchMenuWidget* Menu = ScctTestUi::FindShownWidget<UScctMatchMenuWidget>(Context.GetWorld());
				if (Menu == nullptr || !Menu->IsMenuOpen())
				{
					Inspector.Note(EScctInspectArea::PauseMenu, TEXT("not open to resume"), TEXT("the in-match menu was not open for its RESUME button"));
				}
				else if (!ScctTestUi::PressButton(Menu, TEXT("RESUME"), &Error))
				{
					Inspector.Note(EScctInspectArea::PauseMenu, TEXT("no resume"), FString::Printf(TEXT("the in-match menu has no RESUME button: %s"), *Error));
				}
			}
			Shot(Info.Name);
		}

		void RunStep(FScctTestContext& Context, const FInspectStep& Info, const FScctInspectPawn* Local)
		{
			const double InStep = Context.ElapsedSeconds - StepStart;
			if (!bTapReleased && InStep >= 0.15)
			{
				Context.HoldAction(Info.Tap, false);
				bTapReleased = true;
			}
			if (Local != nullptr && (Local->Flags & EScctInspectPawnFlags::Falling) != 0)
			{
				bAirborne = true;
			}
			if (Local != nullptr)
			{
				const FVector At(Local->X, Local->Y, Local->Z);
				StepPath += static_cast<float>(FVector::Dist2D(At, StepLast));
				StepLast = At;
			}
			AScctPlayerController* Player = Context.GetPlayer();
			if (Player != nullptr && (Info.LookX != 0.0f || Info.LookY != 0.0f))
			{
				// Raw counts on the controller's input, the path a mouse's events take (MouseY positive is the mouse pushed away).
				const float Delta = static_cast<float>(FApp::GetDeltaTime());
				if (Info.LookX != 0.0f)
				{
					Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, Info.LookX * Delta, 1));
				}
				if (Info.LookY != 0.0f)
				{
					Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, Info.LookY * Delta, 1));
				}
			}

			if (Info.Kind == EInspectStepKind::MenuOpen && Player != nullptr)
			{
				const UScctMatchMenuWidget* Menu = ScctTestUi::FindShownWidget<UScctMatchMenuWidget>(Context.GetWorld());
				if (Menu != nullptr && Menu->IsMenuOpen())
				{
					if (!bMenuSeen)
					{
						bMenuSeen = true;
						MenuOpenedAfter = InStep;
					}
					bMenuBlocked |= !Player->HasPlayInput();
				}
				// Once the menu has had its moment, a held movement key must not reach the pawn.
				if (!bMenuHeld && InStep >= 0.5)
				{
					bMenuHeld = true;
					MenuHoldLocation = Local != nullptr ? FVector(Local->X, Local->Y, Local->Z) : FVector::ZeroVector;
					Hold(Context, TEXT("forward"), true);
				}
			}
			else if (Info.Kind == EInspectStepKind::MenuClose && Player != nullptr && !bMenuClosed)
			{
				const UScctMatchMenuWidget* Menu = ScctTestUi::FindShownWidget<UScctMatchMenuWidget>(Context.GetWorld());
				if (Menu != nullptr && !Menu->IsMenuOpen() && Player->HasPlayInput())
				{
					bMenuClosed = true;
					MenuClosedAfter = InStep;
				}
			}
		}

		void EndStep(FScctTestContext& Context, const FInspectStep& Info, const FScctInspectPawn* Local)
		{
			if (!bTapReleased)
			{
				Context.HoldAction(Info.Tap, false);
				bTapReleased = true;
			}
			const FVector Now = Local != nullptr ? FVector(Local->X, Local->Y, Local->Z) : StepStartLocation;
			// The path walked, not the distance from the start: a walk that turns comes back towards where it began.
			const float Moved = StepPath;
			const int32 Voices = Inspector.GetVoicesStarted() - StepVoices;
			StepLog.Add(FString::Printf(TEXT("%s: walked %.0f uu, %s, airborne %d, voices +%d, corrections +%lld"), Info.Name, Moved,
				Local == nullptr ? TEXT("no pawn") : (Local->Flags & EScctInspectPawnFlags::Crouched) != 0 ? TEXT("crouched") : TEXT("standing"), bAirborne ? 1 : 0, Voices, Inspector.GetCorrectionsApplied() - StepCorrections));
			if (Info.Hold1 != nullptr && Moved > InspectMinWalk)
			{
				MovingStepVoices += Voices;
				++MovingSteps;
			}

			const bool bCrouched = Local != nullptr && (Local->Flags & EScctInspectPawnFlags::Crouched) != 0;
			switch (Info.Check)
			{
			case EInspectCheck::Moves:
				++InputChecks;
				if (Moved < InspectMinWalk && !Blocked(Context, Local, Info.bBackwards))
				{
					Inspector.Note(EScctInspectArea::Input, FString::Printf(TEXT("no move %s"), Info.Name),
						FString::Printf(TEXT("holding %s (%s) moved the pawn %.0f uu (less than %.0f) with no wall in the way"), Info.Hold1, Info.Name, Moved, InspectMinWalk));
				}
				break;
			case EInspectCheck::Crouches:
				++InputChecks;
				if (!bCrouched)
				{
					Inspector.Note(EScctInspectArea::Input, TEXT("no crouch"), TEXT("tapping crouch did not crouch the pawn"));
				}
				break;
			case EInspectCheck::Stands:
				++InputChecks;
				if (bCrouched)
				{
					Inspector.Note(EScctInspectArea::Input, FString::Printf(TEXT("no stand %s"), Info.Name), FString::Printf(TEXT("tapping crouch did not stand the pawn up (%s)"), Info.Name));
				}
				break;
			case EInspectCheck::Jumps:
				++InputChecks;
				if (!bAirborne)
				{
					Inspector.Note(EScctInspectArea::Input, FString::Printf(TEXT("no jump %s"), Info.Name), FString::Printf(TEXT("tapping jump (%s) never made the pawn fall"), Info.Name));
				}
				break;
			case EInspectCheck::Fires:
			{
				++InputChecks;
				const int32 Ammo = GetAmmo(Context);
				if (StepAmmo < 0 || Ammo < 0 || Ammo >= StepAmmo)
				{
					Inspector.Note(EScctInspectArea::Input, TEXT("no shot"), FString::Printf(TEXT("holding fire for %.1f s spent no ammunition (HUD ammunition %d -> %d)"), Info.Seconds, StepAmmo, Ammo));
				}
				if (Voices <= 0)
				{
					Inspector.Note(EScctInspectArea::Audio, TEXT("no shot sound"), TEXT("firing started no voice"));
				}
				ShotsFired = FMath::Max(StepAmmo - Ammo, 0);
				break;
			}
			default:
				break;
			}

			if (Info.Kind == EInspectStepKind::MenuOpen)
			{
				const float Drift = Local != nullptr && bMenuHeld ? static_cast<float>(FVector::Dist2D(Now, MenuHoldLocation)) : 0.0f;
				Hold(Context, TEXT("forward"), false);
				MenuFigures = FString::Printf(TEXT("opened after %.2f s, held key moved the pawn %.1f uu"), MenuOpenedAfter, Drift);
				if (!bMenuSeen)
				{
					Inspector.Note(EScctInspectArea::PauseMenu, TEXT("none"), TEXT("ShowMenu opened no in-match menu"));
				}
				else
				{
					if (!bMenuBlocked)
					{
						Inspector.Note(EScctInspectArea::PauseMenu, TEXT("input not blocked"), TEXT("with the in-match menu open the game still had the keys (HasPlayInput)"));
					}
					if (Drift > InspectMenuDrift)
					{
						Inspector.Note(EScctInspectArea::PauseMenu, TEXT("keys reach the pawn"),
							FString::Printf(TEXT("holding forward with the in-match menu open moved the pawn %.0f uu"), Drift), Drift);
					}
				}
			}
			else if (Info.Kind == EInspectStepKind::MenuClose)
			{
				MenuFigures += bMenuClosed ? FString::Printf(TEXT(", RESUME gave the keys back after %.2f s"), MenuClosedAfter) : FString(TEXT(", RESUME did not give the keys back"));
				if (!bMenuClosed)
				{
					Inspector.Note(EScctInspectArea::PauseMenu, TEXT("not closed"), TEXT("RESUME did not close the in-match menu and give the game its keys back"));
				}
			}
		}

		/** A wall within reach ahead of the local pawn (or behind it, for a step backwards). */
		bool Blocked(FScctTestContext& Context, const FScctInspectPawn* Local, bool bBackwards) const
		{
			const UScctSessionSubsystem* Session = Context.GetSession();
			const AScctPawnProxy* Proxy = Session != nullptr ? Session->FindLocalProxy() : nullptr;
			if (Local == nullptr || Proxy == nullptr)
			{
				return false;
			}
			const FVector Start(Local->X, Local->Y, Local->Z);
			const FVector Forward = FRotator(0.0, Proxy->GetPawn().Yaw, 0.0).Vector() * (bBackwards ? -1.0 : 1.0);
			const FVector Extent(Local->CollisionRadius * 0.5f, Local->CollisionRadius * 0.5f, Local->CollisionHeight * 0.5f);
			return Inspector.TraceWorld(Start, Start + Forward * (Local->CollisionRadius + 40.0f), Extent);
		}

		/** Every round the HUD shows (the clip's included), or -1 without the HUD's state. */
		static int32 GetAmmo(const FScctTestContext& Context)
		{
			const UWorld* World = Context.GetWorld();
			const UScctHudSubsystem* Hud = World != nullptr ? World->GetSubsystem<UScctHudSubsystem>() : nullptr;
			return Hud != nullptr && Hud->GetModel().bHasState ? Hud->GetModel().State.Ammo : -1;
		}

		void Hold(FScctTestContext& Context, const FString& Action, bool bHeld)
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

		void Shot(const FString& Label)
		{
			if (!bShots)
			{
				return;
			}
			const FString File = FPaths::ProjectSavedDir() / TEXT("ScctShots") / GetName() / FString::Printf(TEXT("%02d-%s.png"), ShotIndex++, *Label.Replace(TEXT(" "), TEXT("-")));
			FScreenshotRequest::RequestScreenshot(File, true, false);
		}

		FScctTestResult Verdict()
		{
			if (MovingSteps > 0 && MovingStepVoices <= 0)
			{
				Inspector.Note(EScctInspectArea::Audio, TEXT("no footsteps"), FString::Printf(TEXT("%d steps of walking started no voice (footsteps)"), MovingSteps));
			}
			Inspector.SetAreaNote(EScctInspectArea::PauseMenu, MenuFigures);
			Inspector.SetAreaNote(EScctInspectArea::Input, FString::Printf(TEXT("%d step checks%s"), InputChecks,
				bMerc ? *FString::Printf(TEXT(", %d rounds fired"), ShotsFired) : TEXT("")));
			Inspector.SetStep(TEXT("end"));
			Inspector.Finish();

			for (const FString& Line : StepLog)
			{
				UE_LOG(LogScctTest, Log, TEXT("SCCT_INSPECT step %s"), *Line);
			}
			for (const FScctInspectIssue& Issue : Inspector.GetIssues())
			{
				UE_LOG(LogScctTest, Log, TEXT("SCCT_INSPECT issue %s"), *Issue.ToString());
			}
			const TArray<FString> Areas = Inspector.AreaSummaries();
			for (const FString& Line : Areas)
			{
				UE_LOG(LogScctTest, Log, TEXT("SCCT_INSPECT area %s"), *Line);
			}

			const int32 Count = Inspector.GetIssues().Num();
			if (Count == 0)
			{
				return FScctTestResult::Pass(FString::Printf(TEXT("no issues in %.0f s on %s: %s"), Inspector.GetSeconds(), *Map, *FString::Join(Areas, TEXT("; "))));
			}
			TArray<FString> Lines;
			for (const FScctInspectIssue& Issue : Inspector.GetIssues())
			{
				Lines.Add(Issue.ToString());
			}
			return FScctTestResult::Fail(FString::Printf(TEXT("%d issue%s: %s"), Count, Count == 1 ? TEXT("") : TEXT("s"), *FString::Join(Lines, TEXT(" || "))));
		}

		FScctTestResult Finish(FScctTestContext& Context, FScctTestResult Result)
		{
			for (const FString& Action : Held.Array())
			{
				Context.HoldAction(Action, false);
			}
			Held.Reset();
			Inspector.Detach();
			return Result;
		}

		bool bMerc = false;
		bool bOpened = false;
		bool bShots = false;
		int32 ShotIndex = 0;
		FString Map;
		double ReadyAt = -1.0;
		int32 SessionId = 0;
		FScctPlayInspector Inspector;

		TArray<FInspectStep> Steps;
		int32 StepIndex = -1;
		double StepStart = 0.0;
		bool bTapReleased = true;
		bool bAirborne = false;
		FVector StepStartLocation = FVector::ZeroVector;
		FVector StepLast = FVector::ZeroVector;
		float StepPath = 0.0f;
		int32 StepVoices = 0;
		int64 StepCorrections = 0;
		int32 StepAmmo = -1;
		TSet<FString> Held;
		TArray<FString> StepLog;
		int32 InputChecks = 0;
		int32 MovingSteps = 0;
		int32 MovingStepVoices = 0;
		int32 ShotsFired = 0;

		bool bMenuSeen = false;
		bool bMenuBlocked = false;
		bool bMenuHeld = false;
		bool bMenuClosed = false;
		double MenuOpenedAfter = 0.0;
		double MenuClosedAfter = 0.0;
		FVector MenuHoldLocation = FVector::ZeroVector;
		FString MenuFigures;
	};

	class FScctPlayInspectSpyTest : public FScctPlayInspectTest
	{
	public:
		FScctPlayInspectSpyTest() : FScctPlayInspectTest(false) {}
	};

	class FScctPlayInspectMercTest : public FScctPlayInspectTest
	{
	public:
		FScctPlayInspectMercTest() : FScctPlayInspectTest(true) {}
	};
}

SCCT_TEST("play-inspect", ScctPlayInspectTestLocal::FScctPlayInspectSpyTest);
SCCT_TEST("play-inspect-merc", ScctPlayInspectTestLocal::FScctPlayInspectMercTest);
