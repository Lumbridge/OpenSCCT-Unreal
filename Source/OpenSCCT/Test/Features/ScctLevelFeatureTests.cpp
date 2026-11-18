// doors, lights and emitters: the map's own devices on the run's map (-ScctTestMap, AquaE by default), set off on the
// local server through the bridge's world debug table (Play/ScctLevelStateDebug.h, what -ScctLevelDo uses) and judged
// on the Unreal actors the level state drives (Play/ScctLevelStateSubsystem.h), as the Godot client's
// PlayDoorTimerSmoke and the level state checks judge the drawn level:
//   doors     every mover's tag is fired: a mover's mesh actor must move or turn
//   lights    every switched light's tag is fired twice and a lamp is shot out: a light component must change, come
//             back on the second trigger, and the shot lamp must go dark
//   emitters  every map Emitter's tag is fired: a particle component must start or stop
// Each first watches the level for a few seconds, so what moves, flickers or bursts by itself is left out of the
// verdict. The seconds are the simulation's, and every stage also lasts at least LevelMinStageFrames engine frames, so
// a starved run (shaders still compiling, a few frames a second) still sees the level state follow the server.

#include "Components/LightComponent.h"
#include "Engine/Light.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "Play/ScctLevelStateDebug.h"
#include "Test/Features/ScctFeatureKit.h"

namespace ScctLevelFeatureLocal
{
	constexpr double LevelSettleSeconds = 2.0;
	constexpr double LevelBaselineSeconds = 3.0;
	constexpr int64 LevelMinStageFrames = 30;

	bool HasLevelTag(const AActor& Actor)
	{
		for (const FName& Tag : Actor.Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("Scct:")))
			{
				return true;
			}
		}
		return false;
	}

	FString LevelTagOf(const AActor& Actor)
	{
		for (const FName& Tag : Actor.Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(TEXT("Scct:")))
			{
				return Text.RightChop(5);
			}
		}
		return Actor.GetName();
	}

	/**
	 * The shared run of the three: open the map with the spy alone, settle, watch a baseline, then the test's stages.
	 * A stage is begun once (Begin) and watched every frame until its time is up (Observe), then judged (End).
	 */
	class FScctLevelFeatureTest : public IScctTest
	{
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
				if (!Stage.Open(Context, FScctTestContext::GetTestMap(TEXT("AquaE")), TEXT("?Side=spy?Bots=0"), Error))
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
			const int64 Frames = Stage.GetFrames();
			if (Played < LevelSettleSeconds)
			{
				return FScctTestResult::Running();
			}
			if (!bIndexed)
			{
				bIndexed = true;
				FString Error;
				Debug = ScctFeatureKit::GetWorldDebug(&Error);
				if (Debug == nullptr)
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("the bridge's world debug table is missing: %s"), *Error));
				}
				const FString Indexed = Index(*Context.GetWorld());
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("%s on %s; watching %.0f s for what changes by itself"), *Indexed, *Stage.GetMap(), LevelBaselineSeconds));
				if (!Indexed.IsEmpty() && Indexed.StartsWith(TEXT("no ")))
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("%s on %s (the map must be imported with its devices)"), *Indexed, *Stage.GetMap()));
				}
				Observe(true);
				BaselineFrame = Frames;
				return FScctTestResult::Running();
			}
			if (Played < LevelSettleSeconds + LevelBaselineSeconds || Frames - BaselineFrame < LevelMinStageFrames)
			{
				Observe(true);
				return FScctTestResult::Running();
			}

			if (StageIndex < 0 || (Played - StageStart >= StageSeconds(StageIndex) && Frames - StageFrame >= LevelMinStageFrames))
			{
				if (StageIndex >= 0)
				{
					FString Problem;
					const FString Figures = End(StageIndex, Problem);
					ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("stage %d: %s"), StageIndex + 1, *Figures));
					ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("stage%d-end"), StageIndex + 1));
					Report.Add(Figures);
					if (!Problem.IsEmpty())
					{
						return FScctTestResult::Fail(FString::Printf(TEXT("%s (%s)"), *Problem, *FString::Join(Report, TEXT("; "))));
					}
				}
				++StageIndex;
				if (StageIndex >= StageCount())
				{
					return FScctTestResult::Pass(FString::Join(Report, TEXT("; ")));
				}
				StageStart = Played;
				StageFrame = Frames;
				FString Problem;
				const bool bBroken = ScctFeatureKit::IsBroken();
				const int32 Queued = bBroken ? -2 : Begin(StageIndex, *Debug, Problem);
				if (!Problem.IsEmpty())
				{
					return FScctTestResult::Fail(Problem);
				}
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("stage %d begun: %s"), StageIndex + 1,
					Queued == -2 ? TEXT("-ScctFeatureBreak, nothing was set off") : Queued == 1 ? TEXT("handed to the local server") : TEXT("the bridge had no local server to do it")));
				if (Queued == 0)
				{
					return FScctTestResult::Fail(TEXT("the bridge's world debug table found no local server to set the devices off"));
				}
				ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("stage%d-begin"), StageIndex + 1));
			}
			Observe(false);
			return FScctTestResult::Running();
		}

	protected:
		/** Finds the actors the test watches; returns a summary ("no ..." when there are none). */
		virtual FString Index(UWorld& World) = 0;
		/** One frame of watching; bBaseline while the baseline runs. */
		virtual void Observe(bool bBaseline) = 0;
		virtual int32 StageCount() const { return 1; }
		virtual double StageSeconds(int32 Phase) const { return 5.0; }
		/** Sets the stage off; returns the debug entry's answer (1 handed to the server). */
		virtual int32 Begin(int32 Phase, const FScctWorldDebugApi& Api, FString& OutProblem) = 0;
		/** Judges the stage; OutProblem is set when it failed. Returns the figures. */
		virtual FString End(int32 Phase, FString& OutProblem) = 0;

		bool bOpened = false;
		bool bIndexed = false;
		int32 StageIndex = -1;
		double StageStart = 0.0;
		int64 StageFrame = 0;
		int64 BaselineFrame = 0;
		FScctFeatureStage Stage;
		const FScctWorldDebugApi* Debug = nullptr;
		TArray<FString> Report;
	};

	// ---- doors ---------------------------------------------------------------------------------------------------

	class FScctDoorsFeatureTest : public FScctLevelFeatureTest
	{
		struct FMover
		{
			TWeakObjectPtr<AActor> Actor;
			FTransform Start;
			float Offset = 0.0f;
			float Turn = 0.0f;
			bool bMovesByItself = false;
		};

		virtual FString Index(UWorld& World) override
		{
			for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
			{
				const USceneComponent* Root = It->GetRootComponent();
				if (HasLevelTag(**It) && Root != nullptr && Root->Mobility == EComponentMobility::Movable)
				{
					Movers.Add({ *It, It->GetActorTransform() });
				}
			}
			return Movers.Num() == 0 ? FString(TEXT("no movable tagged mesh actors (movers)")) : FString::Printf(TEXT("%d movable tagged mesh actors"), Movers.Num());
		}

		virtual void Observe(bool bBaseline) override
		{
			for (FMover& Mover : Movers)
			{
				const AActor* Actor = Mover.Actor.Get();
				if (Actor == nullptr)
				{
					continue;
				}
				const float Offset = static_cast<float>(FVector::Dist(Actor->GetActorLocation(), Mover.Start.GetLocation()));
				const float Turn = static_cast<float>(FMath::RadiansToDegrees(Actor->GetActorQuat().AngularDistance(Mover.Start.GetRotation())));
				if (bBaseline)
				{
					Mover.bMovesByItself |= Offset > 2.0f || Turn > 1.0f;
				}
				else
				{
					Mover.Offset = FMath::Max(Mover.Offset, Offset);
					Mover.Turn = FMath::Max(Mover.Turn, Turn);
				}
			}
		}

		virtual double StageSeconds(int32) const override { return 6.0; }

		virtual int32 Begin(int32, const FScctWorldDebugApi& Api, FString&) override
		{
			return Api.FireMovers();
		}

		virtual FString End(int32, FString& OutProblem) override
		{
			int32 ByItself = 0;
			int32 Moved = 0;
			float Largest = 0.0f;
			float LargestTurn = 0.0f;
			FString Example;
			for (const FMover& Mover : Movers)
			{
				if (Mover.bMovesByItself)
				{
					++ByItself;
					continue;
				}
				if (Mover.Offset > 10.0f || Mover.Turn > 5.0f)
				{
					++Moved;
					if (Mover.Offset + Mover.Turn > Largest + LargestTurn)
					{
						Largest = Mover.Offset;
						LargestTurn = Mover.Turn;
						Example = Mover.Actor.IsValid() ? LevelTagOf(*Mover.Actor) : FString();
					}
				}
			}
			const FString Figures = FString::Printf(TEXT("%d of %d movers' actors moved after their tags were fired (%d moved by themselves before and are left out); the most, %s, by %.0f uu and %.0f degrees"),
				Moved, Movers.Num(), ByItself, Example.IsEmpty() ? TEXT("none") : *Example, Largest, LargestTurn);
			if (Moved == 0)
			{
				OutProblem = TEXT("no mover's actor moved or turned when every mover's tag was fired");
			}
			return Figures;
		}

		TArray<FMover> Movers;
	};

	// ---- lights --------------------------------------------------------------------------------------------------

	class FScctLightsFeatureTest : public FScctLevelFeatureTest
	{
		struct FLamp
		{
			TWeakObjectPtr<ALight> Actor;
			float Baseline = 0.0f;  // the drawn brightness at the end of the baseline
			float Low = 0.0f;
			float High = 0.0f;
			bool bAnimated = false; // changed by itself during the baseline
			bool bToggled = false;  // changed on the first trigger
			float AfterFirst = 0.0f;
		};

		static float Drawn(const ALight& Light)
		{
			const ULightComponent* Component = Light.GetLightComponent();
			return Component != nullptr && Component->IsVisible() ? Component->Intensity : 0.0f;
		}

		static bool Differs(float A, float B)
		{
			return FMath::Abs(A - B) > FMath::Max(0.25f * FMath::Max(A, B), 0.01f);
		}

		virtual FString Index(UWorld& World) override
		{
			for (TActorIterator<ALight> It(&World); It; ++It)
			{
				if (HasLevelTag(**It) && It->GetLightComponent() != nullptr)
				{
					FLamp Lamp;
					Lamp.Actor = *It;
					Lamp.Baseline = Lamp.Low = Lamp.High = Drawn(**It);
					Lamps.Add(Lamp);
				}
			}
			return Lamps.Num() == 0 ? FString(TEXT("no tagged lights")) : FString::Printf(TEXT("%d tagged lights"), Lamps.Num());
		}

		virtual void Observe(bool bBaseline) override
		{
			if (!bBaseline)
			{
				return;
			}
			for (FLamp& Lamp : Lamps)
			{
				if (const ALight* Light = Lamp.Actor.Get())
				{
					const float Now = Drawn(*Light);
					Lamp.bAnimated |= Differs(Now, Lamp.Baseline);
					Lamp.Baseline = Now;
				}
			}
		}

		virtual int32 StageCount() const override { return 3; }
		virtual double StageSeconds(int32 Phase) const override { return Phase == 2 ? 3.0 : 2.0; }

		virtual int32 Begin(int32 Phase, const FScctWorldDebugApi& Api, FString& OutProblem) override
		{
			if (Phase < 2)
			{
				return Api.FireLights();
			}
			// Shoot out a lamp that the triggers switched and that is lit again.
			for (int32 Index = 0; Index < Lamps.Num(); ++Index)
			{
				const ALight* Light = Lamps[Index].Actor.Get();
				if (Lamps[Index].bToggled && Light != nullptr && Drawn(*Light) > 0.0f)
				{
					Target = Index;
					break;
				}
			}
			if (Target < 0)
			{
				OutProblem = TEXT("no switched lamp was lit after the second trigger to be shot out");
				return 0;
			}
			const FVector At = Lamps[Target].Actor->GetActorLocation();
			return Api.BreakLights(static_cast<float>(At.X), static_cast<float>(At.Y), static_cast<float>(At.Z), 1);
		}

		virtual FString End(int32 Phase, FString& OutProblem) override
		{
			int32 Animated = 0;
			int32 Changed = 0;
			int32 Back = 0;
			for (FLamp& Lamp : Lamps)
			{
				const ALight* Light = Lamp.Actor.Get();
				if (Light == nullptr || Lamp.bAnimated)
				{
					Animated += Lamp.bAnimated ? 1 : 0;
					continue;
				}
				const float Now = Drawn(*Light);
				if (Phase == 0 && Differs(Now, Lamp.Baseline))
				{
					Lamp.bToggled = true;
					Lamp.AfterFirst = Now;
					++Changed;
				}
				else if (Phase == 1 && Lamp.bToggled)
				{
					++Changed;
					Back += Differs(Now, Lamp.Baseline) ? 0 : 1;
				}
			}
			if (Phase == 0)
			{
				if (Changed == 0)
				{
					OutProblem = TEXT("no light changed when every switched light's tag was fired");
				}
				return FString::Printf(TEXT("first trigger: %d of %d lights changed (%d flicker or pulse by themselves and are left out)"), Changed, Lamps.Num(), Animated);
			}
			if (Phase == 1)
			{
				if (Back == 0)
				{
					OutProblem = TEXT("no switched light came back to its first state on the second trigger");
				}
				return FString::Printf(TEXT("second trigger: %d of the %d switched lights came back"), Back, Changed);
			}
			if (!Lamps.IsValidIndex(Target))
			{
				OutProblem = TEXT("no lamp was shot out");
				return TEXT("no lamp shot");
			}
			const FLamp& Shot = Lamps[Target];
			const ALight* Light = Shot.Actor.Get();
			const float Now = Light != nullptr ? Drawn(*Light) : -1.0f;
			// The bridge breaks the lit switched light nearest the point: that one, or one hanging beside it.
			int32 DarkNearby = 0;
			if (Light != nullptr)
			{
				for (const FLamp& Other : Lamps)
				{
					const ALight* OtherLight = Other.Actor.Get();
					if (OtherLight != nullptr && Other.bToggled && FVector::Dist(OtherLight->GetActorLocation(), Light->GetActorLocation()) < 200.0 && Drawn(*OtherLight) <= 0.0f)
					{
						++DarkNearby;
					}
				}
			}
			if (DarkNearby == 0)
			{
				OutProblem = FString::Printf(TEXT("the shot lamp %s did not go dark (brightness %.1f)"), Light != nullptr ? *LevelTagOf(*Light) : TEXT("?"), Now);
			}
			return FString::Printf(TEXT("shot lamp %s: brightness %.1f, %d lamp(s) dark within 200 uu"), Light != nullptr ? *LevelTagOf(*Light) : TEXT("?"), Now, DarkNearby);
		}

		TArray<FLamp> Lamps;
		int32 Target = -1;
	};

	// ---- emitters ------------------------------------------------------------------------------------------------

	class FScctEmittersFeatureTest : public FScctLevelFeatureTest
	{
		struct FEmitter
		{
			TWeakObjectPtr<UNiagaraComponent> Component;
			FString Name;
			bool bBaseline = false;  // active at the end of the baseline
			bool bByItself = false;  // started or stopped during the baseline
			bool bChanged = false;   // started or stopped after the trigger
		};

		virtual FString Index(UWorld& World) override
		{
			for (TActorIterator<ANiagaraActor> It(&World); It; ++It)
			{
				UNiagaraComponent* Component = It->GetNiagaraComponent();
				if (HasLevelTag(**It) && Component != nullptr)
				{
					Emitters.Add({ Component, LevelTagOf(**It), Component->IsActive() });
				}
			}
			return Emitters.Num() == 0 ? FString(TEXT("no tagged particle emitters")) : FString::Printf(TEXT("%d tagged particle emitters"), Emitters.Num());
		}

		virtual void Observe(bool bBaseline) override
		{
			for (FEmitter& Emitter : Emitters)
			{
				const UNiagaraComponent* Component = Emitter.Component.Get();
				if (Component == nullptr)
				{
					continue;
				}
				const bool bActive = Component->IsActive();
				if (bBaseline)
				{
					Emitter.bByItself |= bActive != Emitter.bBaseline;
					Emitter.bBaseline = bActive;
				}
				else
				{
					Emitter.bChanged |= bActive != Emitter.bBaseline;
				}
			}
		}

		virtual double StageSeconds(int32) const override { return 3.0; }

		virtual int32 Begin(int32, const FScctWorldDebugApi& Api, FString&) override
		{
			return Api.FireEmitters();
		}

		virtual FString End(int32, FString& OutProblem) override
		{
			int32 ByItself = 0;
			int32 Started = 0;
			int32 Stopped = 0;
			FString Example;
			for (const FEmitter& Emitter : Emitters)
			{
				if (Emitter.bByItself)
				{
					++ByItself;
					continue;
				}
				if (Emitter.bChanged)
				{
					(Emitter.bBaseline ? Stopped : Started) += 1;
					Example = Example.IsEmpty() ? Emitter.Name : Example;
				}
			}
			if (Started + Stopped == 0)
			{
				OutProblem = TEXT("no particle emitter started or stopped when every map Emitter's tag was fired");
			}
			return FString::Printf(TEXT("%d of %d emitters started and %d stopped after the trigger (%d changed by themselves and are left out), e.g. %s"),
				Started, Emitters.Num(), Stopped, ByItself, Example.IsEmpty() ? TEXT("none") : *Example);
		}

		TArray<FEmitter> Emitters;
	};
}

SCCT_TEST("doors", ScctLevelFeatureLocal::FScctDoorsFeatureTest);
SCCT_TEST("lights", ScctLevelFeatureLocal::FScctLightsFeatureTest);
SCCT_TEST("emitters", ScctLevelFeatureLocal::FScctEmittersFeatureTest);
