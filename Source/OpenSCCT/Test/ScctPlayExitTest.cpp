// The clean-exit test: two matches started from the front end and each left through the in-match menu, then the
// game quit through the front end's QUIT (the Godot client's PlayExitSmoke, MenuEscapeSmoke and ResignMenuSmoke).

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Menu/ScctFrontEndWidget.h"
#include "Menu/ScctMatchMenuWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Test/ScctTest.h"
#include "Test/ScctTestApi.h"
#include "Test/ScctTestUi.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/**
	 * play-exit: from the front end, PLAY OFFLINE, the run's map (-ScctTestMap, default AquaE) with one bot, START;
	 * once the local player's body is on the screen it plays a few seconds, the in-match menu is opened by the
	 * player's ShowMenu command, closed with RESUME, opened again by the Quit command (the original's leave key) and
	 * left with MAIN MENU. Back at the front end it checks the teardown: the match world's session went to 0, the
	 * front end presents none, the bridge is idle and holds no session, and after a full garbage collection no pawn
	 * body and not the match's world is left. A second match (-ScctExitSessions, default 2) is left the same way
	 * (opened by ShowMenu only), then the verdict is written and the game quits through the front end's QUIT button.
	 *
	 * Everything is pressed as a player presses it: the buttons are found by their labels on the screen and pressed
	 * only while shown. A crash or an ensure at exit comes after the verdict line, so the suite driver
	 * (Tools/Test/Run-ScctTests.ps1) also requires exit code 0, no new crash report and no ensure in the log.
	 */
	class FScctPlayExitTest : public IScctTest
	{
	public:
		/** Seconds each match plays once the local player's body is there, and seconds on the front end between. */
		static constexpr double PlaySeconds = 4.0;
		static constexpr double MenuSeconds = 1.0;

		/** Seconds a menu page or a level may take to appear. */
		static constexpr double PageSeconds = 10.0;
		static constexpr double LoadSeconds = 150.0;

		virtual ~FScctPlayExitTest() override
		{
			Unwatch();
		}

		virtual void Start(FScctTestContext& Context) override
		{
			Sessions = 2;
			FParse::Value(FCommandLine::Get(), TEXT("ScctExitSessions="), Sessions);
			Sessions = FMath::Max(Sessions, 1);
			Map = FScctTestContext::GetTestMap(TEXT("AquaE"));
			UE_LOG(LogScctTest, Log, TEXT("SCCT_EXIT %d match(es) on %s, each left through the in-match menu"), Sessions, *Map);
		}

		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			UWorld* World = Context.GetWorld();
			const double InPhase = Context.ElapsedSeconds - PhaseStart;
			FString Error;

			switch (Phase)
			{
			case EPhase::ToFrontEnd:
				if (FindFrontEnd(Context) != nullptr)
				{
					Enter(EPhase::OnFrontEnd, Context);
				}
				else if (!bTravelAsked && World != nullptr && World->HasBegunPlay())
				{
					bTravelAsked = true;
					if (!Context.OpenLevel(UScctOnlineSubsystem::FrontEndLevel, &Error))
					{
						return FScctTestResult::Fail(Error);
					}
				}
				else if (InPhase > LoadSeconds)
				{
					return FScctTestResult::Fail(TEXT("the front end did not open"));
				}
				return FScctTestResult::Running();

			case EPhase::OnFrontEnd:
			{
				UScctFrontEndWidget* FrontEnd = FindFrontEnd(Context);
				if (FrontEnd == nullptr)
				{
					return FScctTestResult::Fail(TEXT("the front end went away by itself"));
				}
				if (InPhase < MenuSeconds)
				{
					return FScctTestResult::Running();
				}
				if (Done >= Sessions)
				{
					// The way out must be there before the verdict says the run can quit through it.
					const TSharedPtr<SWidget> Root = FrontEnd->GetCachedWidget();
					if (!Root.IsValid() || !ScctTestUi::FindButton(Root.ToSharedRef(), TEXT("QUIT")).IsValid())
					{
						return FScctTestResult::Fail(TEXT("the front end shows no QUIT button"));
					}
					return FScctTestResult::Pass(FString::Printf(TEXT("sessions=%d ids=%s map=%s"), Done, *FString::Join(SessionIds, TEXT(",")), *Map));
				}
				if (!ScctTestUi::PressButton(FrontEnd, TEXT("PLAY OFFLINE"), &Error))
				{
					return FScctTestResult::Fail(Error);
				}
				Enter(EPhase::PickMap, Context);
				return FScctTestResult::Running();
			}

			case EPhase::PickMap:
			case EPhase::PickBots:
			case EPhase::PressStart:
			{
				// The offline page is rebuilt when it opens; each of its buttons is looked for until it shows.
				const FString Label = Phase == EPhase::PickMap ? Map.ToUpper() : Phase == EPhase::PickBots ? FString(TEXT("1")) : FString(TEXT("START"));
				if (ScctTestUi::PressButton(FindFrontEnd(Context), Label, &Error))
				{
					if (Phase == EPhase::PressStart)
					{
						FrontEndWorld = World;
						UE_LOG(LogScctTest, Log, TEXT("SCCT_EXIT match %d: START pressed on the offline page (%s, 1 bot)"), Done + 1, *Map);
						Enter(EPhase::WaitMatch, Context);
					}
					else
					{
						Enter(Phase == EPhase::PickMap ? EPhase::PickBots : EPhase::PressStart, Context);
					}
				}
				else if (InPhase > PageSeconds)
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("offline page: %s"), *Error));
				}
				return FScctTestResult::Running();
			}

			case EPhase::WaitMatch:
			{
				UScctSessionSubsystem* Session = Context.GetSession();
				if (World != nullptr && World != FrontEndWorld.Get() && Session != nullptr && Session->IsPlaying()
					&& Session->FindLocalProxy() != nullptr)
				{
					Watch(Session);
					MatchWorld = World;
					SessionIds.Add(FString::FromInt(WatchedId));
					UE_LOG(LogScctTest, Log, TEXT("SCCT_EXIT match %d playing: session %d on %s"), Done + 1, WatchedId, *Context.GetLevelName());
					Enter(EPhase::Playing, Context);
				}
				else if (InPhase > LoadSeconds)
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("match %d did not start within %.0f s (level %s)"), Done + 1, LoadSeconds, *Context.GetLevelName()));
				}
				return FScctTestResult::Running();
			}

			case EPhase::Playing:
				if (Context.GetWorld() != MatchWorld.Get())
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("match %d ended by itself"), Done + 1));
				}
				if (InPhase >= PlaySeconds)
				{
					MenuOpens = 0;
					Enter(EPhase::OpenMenu, Context);
				}
				return FScctTestResult::Running();

			case EPhase::OpenMenu:
			{
				// The first match opens it with both of the player's menu keys (ShowMenu, then Quit after RESUME).
				AScctPlayerController* Player = Context.GetPlayer();
				UScctMatchMenuWidget* Menu = ScctTestUi::FindShownWidget<UScctMatchMenuWidget>(World);
				if (Player == nullptr || Menu == nullptr)
				{
					return FScctTestResult::Fail(TEXT("the match has no player or no in-match menu"));
				}
				if (Menu->IsMenuOpen())
				{
					return FScctTestResult::Fail(TEXT("the in-match menu was open before it was asked for"));
				}
				const FName Command = MenuOpens == 0 ? FName(TEXT("ShowMenu")) : FName(TEXT("Quit"));
				UE_LOG(LogScctTest, Log, TEXT("SCCT_EXIT match %d: command %s"), Done + 1, *Command.ToString());
				Player->OnLocalCommand.Broadcast(Command, 0, true);
				Player->OnLocalCommand.Broadcast(Command, 0, false);
				LastCommand = Command;
				++MenuOpens;
				Enter(EPhase::WaitMenuOpen, Context);
				return FScctTestResult::Running();
			}

			case EPhase::WaitMenuOpen:
			{
				UScctMatchMenuWidget* Menu = ScctTestUi::FindShownWidget<UScctMatchMenuWidget>(World);
				if (Menu == nullptr || !Menu->IsMenuOpen())
				{
					if (InPhase > 2.0)
					{
						return FScctTestResult::Fail(FString::Printf(TEXT("the %s command did not open the in-match menu"), *LastCommand.ToString()));
					}
					return FScctTestResult::Running();
				}
				// Once open, one more frame so the menu has been drawn as the player sees it.
				if (Context.Frame - PhaseFrame < 2)
				{
					return FScctTestResult::Running();
				}
				const bool bResume = Done == 0 && MenuOpens == 1;
				if (!ScctTestUi::PressButton(Menu, bResume ? TEXT("RESUME") : TEXT("MAIN MENU"), &Error))
				{
					return FScctTestResult::Fail(Error);
				}
				UE_LOG(LogScctTest, Log, TEXT("SCCT_EXIT match %d: %s pressed"), Done + 1, bResume ? TEXT("RESUME") : TEXT("MAIN MENU"));
				Enter(bResume ? EPhase::WaitMenuClosed : EPhase::WaitFrontEnd, Context);
				return FScctTestResult::Running();
			}

			case EPhase::WaitMenuClosed:
			{
				UScctMatchMenuWidget* Menu = ScctTestUi::FindShownWidget<UScctMatchMenuWidget>(World);
				if (Menu != nullptr && !Menu->IsMenuOpen())
				{
					if (InPhase >= MenuSeconds)
					{
						Enter(EPhase::OpenMenu, Context);
					}
				}
				else if (InPhase > 2.0)
				{
					return FScctTestResult::Fail(TEXT("RESUME did not close the in-match menu"));
				}
				return FScctTestResult::Running();
			}

			case EPhase::WaitFrontEnd:
				if (FindFrontEnd(Context) != nullptr && Context.GetWorld() != MatchWorld.Get())
				{
					// Whatever the level travel left unreferenced goes now, so what is still there is held by something.
					GEngine->ForceGarbageCollection(true);
					Enter(EPhase::Collect, Context);
				}
				else if (InPhase > LoadSeconds)
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("match %d: MAIN MENU did not bring the front end back"), Done + 1));
				}
				return FScctTestResult::Running();

			case EPhase::Collect:
			{
				if (Context.Frame - PhaseFrame < 3)
				{
					return FScctTestResult::Running();
				}
				const FString Problem = CheckTeardown(Context);
				if (!Problem.IsEmpty())
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("after leaving match %d (session %d): %s"), Done + 1, WatchedId, *Problem));
				}
				UE_LOG(LogScctTest, Log, TEXT("SCCT_EXIT match %d left: session %d -> 0, the bridge is idle, no pawn body and no match world is left"), Done + 1, WatchedId);
				Unwatch();
				++Done;
				Enter(EPhase::OnFrontEnd, Context);
				return FScctTestResult::Running();
			}
			}
			return FScctTestResult::Fail(TEXT("unknown phase"));
		}

		virtual bool QuitThroughGame(FScctTestContext& Context) override
		{
			FString Error;
			if (!ScctTestUi::PressButton(FindFrontEnd(Context), TEXT("QUIT"), &Error))
			{
				UE_LOG(LogScctTest, Error, TEXT("SCCT_EXIT could not quit through the front end: %s"), *Error);
				return false;
			}
			UE_LOG(LogScctTest, Log, TEXT("SCCT_EXIT QUIT pressed on the front end"));
			return true;
		}

	private:
		enum class EPhase : uint8
		{
			ToFrontEnd,
			OnFrontEnd,
			PickMap,
			PickBots,
			PressStart,
			WaitMatch,
			Playing,
			OpenMenu,
			WaitMenuOpen,
			WaitMenuClosed,
			WaitFrontEnd,
			Collect,
		};

		void Enter(EPhase NewPhase, const FScctTestContext& Context)
		{
			Phase = NewPhase;
			PhaseStart = Context.ElapsedSeconds;
			PhaseFrame = Context.Frame;
		}

		/** The front end's menu, once its level plays and the menu is in the viewport. */
		static UScctFrontEndWidget* FindFrontEnd(const FScctTestContext& Context)
		{
			const UWorld* World = Context.GetWorld();
			if (World == nullptr || !World->HasBegunPlay())
			{
				return nullptr;
			}
			return ScctTestUi::FindShownWidget<UScctFrontEndWidget>(World);
		}

		void Watch(UScctSessionSubsystem* Session)
		{
			Unwatch();
			WatchedSession = Session;
			WatchedId = Session->GetSessionId();
			bSawStop = false;
			WatchHandle = Session->OnSessionChanged.AddRaw(this, &FScctPlayExitTest::HandleSessionChanged);
		}

		void Unwatch()
		{
			if (UScctSessionSubsystem* Session = WatchedSession.Get())
			{
				Session->OnSessionChanged.Remove(WatchHandle);
			}
			WatchedSession.Reset();
			WatchHandle.Reset();
		}

		void HandleSessionChanged(int32 OldId, int32 NewId)
		{
			if (OldId == WatchedId && NewId == 0)
			{
				bSawStop = true;
			}
		}

		/** What is wrong once a match has been left and the front end is back; empty when the teardown was clean. */
		FString CheckTeardown(const FScctTestContext& Context) const
		{
			TArray<FString> Problems;
			if (!bSawStop)
			{
				Problems.Add(FString::Printf(TEXT("the match's session never went from %d to 0"), WatchedId));
			}

			const UScctSessionSubsystem* Session = Context.GetSession();
			if (Session != nullptr && Session->GetSessionId() != 0)
			{
				Problems.Add(FString::Printf(TEXT("the front end presents session %d"), Session->GetSessionId()));
			}

			FString Error;
			if (const FScctBridgeApi* Bridge = FOpenSCCTModule::Get().GetBridge(Error))
			{
				FScctBridgeSessionInfo Info = {};
				Bridge->GetSession(&Info);
				if (Info.Id != 0 || Info.State != static_cast<int32>(EScctBridgeState::Idle))
				{
					Problems.Add(FString::Printf(TEXT("the bridge is not idle (session %d, state %d)"), Info.Id, Info.State));
				}
			}
			else
			{
				Problems.Add(FString::Printf(TEXT("the bridge cannot be read: %s"), *Error));
			}
			if (const FScctTestApi* Test = ScctFeature<FScctTestApi>(&Error))
			{
				FScctTestCounters Counters = {};
				if (const int32 Running = Test->GetCounters(&Counters))
				{
					Problems.Add(FString::Printf(TEXT("the bridge still runs session %d (%lld client ticks)"), Running, Counters.ClientTicks));
				}
			}

			int32 Bodies = 0;
			TSet<FString> Worlds;
			for (TObjectIterator<AScctPawnProxy> It; It; ++It)
			{
				if (IsValid(*It) && !It->IsTemplate())
				{
					++Bodies;
					const UWorld* Owner = It->GetWorld();
					Worlds.Add(Owner != nullptr ? Owner->GetName() : FString(TEXT("no world")));
				}
			}
			if (Bodies > 0)
			{
				Problems.Add(FString::Printf(TEXT("%d pawn bod%s left (in %s)"), Bodies, Bodies == 1 ? TEXT("y") : TEXT("ies"), *FString::Join(Worlds.Array(), TEXT(", "))));
			}
			if (MatchWorld.IsValid())
			{
				Problems.Add(FString::Printf(TEXT("the match's world %s is still loaded after a full garbage collection"), *MatchWorld->GetName()));
			}
			return FString::Join(Problems, TEXT("; "));
		}

		EPhase Phase = EPhase::ToFrontEnd;
		double PhaseStart = 0.0;
		int32 PhaseFrame = 0;
		bool bTravelAsked = false;

		int32 Sessions = 2;
		int32 Done = 0;
		FString Map;
		TArray<FString> SessionIds;

		TWeakObjectPtr<UWorld> FrontEndWorld;
		TWeakObjectPtr<UWorld> MatchWorld;
		TWeakObjectPtr<UScctSessionSubsystem> WatchedSession;
		FDelegateHandle WatchHandle;
		int32 WatchedId = 0;
		bool bSawStop = false;

		int32 MenuOpens = 0;
		FName LastCommand;
	};
}

SCCT_TEST("play-exit", FScctPlayExitTest);
