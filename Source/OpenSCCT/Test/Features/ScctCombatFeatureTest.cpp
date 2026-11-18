// combat-autofire: a merc alone on the run's map (-ScctTestMap, AquaE by default) looks a little down and holds fire for
// a second and a half, as -ScctAutoFire does. It fails unless the HUD's ammunition went down, the shots arrived as
// events and were drawn (muzzle flashes, on the view model or by the effects), and the rounds left impact marks with
// their bursts and decals (Play/ScctEffectsSubsystem.h). The Godot client's play checks of the rifle (RifleSmoke and
// HudGameplayParity's ammunition).

#include "Engine/World.h"
#include "Hud/ScctHudModel.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctPlayerController.h"
#include "Test/Features/ScctFeatureKit.h"

namespace ScctCombatFeatureLocal
{
	constexpr double CombatSettleSeconds = 2.0;
	constexpr double CombatLookSeconds = 0.8;
	constexpr double CombatFireSeconds = 1.5;
	constexpr double CombatAfterSeconds = 1.5;
	/** Mouse counts a second pulled towards the player while looking down (some 20 degrees over the look). */
	constexpr float CombatLookCounts = -2000.0f;

	class FScctCombatFeatureTest : public IScctTest
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
				if (!Stage.Open(Context, FScctTestContext::GetTestMap(TEXT("AquaE")), TEXT("?Side=merc?Bots=0"), Error))
				{
					return FScctTestResult::Fail(Error);
				}
				return FScctTestResult::Running();
			}
			const FScctTestResult Ready = Stage.Step(Context);
			if (Ready.State != EScctTestState::Passed)
			{
				return Finish(Context, Ready);
			}

			// The simulation's seconds, so a starved run (a few frames a second) still holds fire for 1.5 s of play.
			const double Played = Stage.GetSimSeconds(Context);
			AScctPlayerController* Player = Context.GetPlayer();
			if (Played < CombatSettleSeconds)
			{
				return FScctTestResult::Running();
			}
			if (Played < CombatSettleSeconds + CombatLookSeconds)
			{
				if (Player != nullptr)
				{
					Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, CombatLookCounts * static_cast<float>(FApp::GetDeltaTime()), 1));
				}
				return FScctTestResult::Running();
			}

			const FScctHudModel* Hud = ScctFeatureKit::GetHud(Context);
			const UScctEffectsSubsystem* Effects = ScctFeatureKit::GetEffects(Context);
			if (Hud == nullptr || Effects == nullptr)
			{
				return Played > 20.0 ? Finish(Context, FScctTestResult::Fail(TEXT("the merc's HUD or the effects never came up"))) : FScctTestResult::Running();
			}

			if (!bFiring)
			{
				bFiring = true;
				FireAt = Played;
				AmmoBefore = Hud->State.Ammo;
				ClipBefore = Hud->State.ClipAmmo;
				Before = Effects->GetStats();
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("firing on %s with %d rounds (%d in the clip), weapon kind %d, fire mode %d%s"), *Stage.GetMap(),
					AmmoBefore, ClipBefore, Hud->State.WeaponKind, Hud->State.FireMode, ScctFeatureKit::IsBroken() ? TEXT(" (-ScctFeatureBreak: fire is not pressed)") : TEXT("")));
				ScctFeatureKit::Shot(GetName(), TEXT("before"));
				if (!ScctFeatureKit::IsBroken())
				{
					Keys.Hold(Context, TEXT("fire"), true);
				}
				return FScctTestResult::Running();
			}
			if (Played < FireAt + CombatFireSeconds)
			{
				MaxDecals = FMath::Max(MaxDecals, Effects->GetStats().DecalsShown);
				return FScctTestResult::Running();
			}
			if (Keys.IsHeld(TEXT("fire")))
			{
				Keys.Hold(Context, TEXT("fire"), false);
				ScctFeatureKit::Shot(GetName(), TEXT("fired"));
			}
			MaxDecals = FMath::Max(MaxDecals, Effects->GetStats().DecalsShown);
			if (Played < FireAt + CombatFireSeconds + CombatAfterSeconds)
			{
				return FScctTestResult::Running();
			}

			const UScctEffectsSubsystem::FStats After = Effects->GetStats();
			const int32 Spent = AmmoBefore - Hud->State.Ammo;
			const int32 Flashes = (After.MuzzleFlashes + After.FlashesLeftToViewModel) - (Before.MuzzleFlashes + Before.FlashesLeftToViewModel);
			const int32 Tracers = After.Tracers - Before.Tracers;
			const int32 Impacts = After.Impacts - Before.Impacts;
			const FString Figures = FString::Printf(TEXT("%d rounds spent (HUD %d -> %d), %d muzzle flashes (%d on the view model), %d tracers, %d impact marks, at most %d decals drawn"),
				Spent, AmmoBefore, Hud->State.Ammo, Flashes, After.FlashesLeftToViewModel - Before.FlashesLeftToViewModel, Tracers, Impacts, MaxDecals);
			ScctFeatureKit::Note(GetName(), Figures);

			TArray<FString> Problems;
			if (Spent <= 0)
			{
				Problems.Add(FString::Printf(TEXT("holding fire for %.1f s spent no ammunition on the HUD (%d -> %d)"), CombatFireSeconds, AmmoBefore, Hud->State.Ammo));
			}
			if (Flashes <= 0)
			{
				Problems.Add(TEXT("no shot event arrived: no muzzle flash was drawn or left to the view model"));
			}
			if (Impacts <= 0)
			{
				Problems.Add(TEXT("the rounds left no impact mark"));
			}
			else if (MaxDecals <= 0)
			{
				Problems.Add(FString::Printf(TEXT("%d impact marks arrived but no decal was drawn for them"), Impacts));
			}
			return Finish(Context, Problems.Num() == 0 ? FScctTestResult::Pass(Figures) : FScctTestResult::Fail(FString::Join(Problems, TEXT("; ")) + TEXT(" (") + Figures + TEXT(")")));
		}

	private:
		FScctTestResult Finish(FScctTestContext& Context, const FScctTestResult& Result)
		{
			if (Result.State != EScctTestState::Running)
			{
				Keys.ReleaseAll(Context);
			}
			return Result;
		}

		bool bOpened = false;
		FScctFeatureStage Stage;
		FScctFeatureKeys Keys;
		bool bFiring = false;
		double FireAt = 0.0;
		int32 AmmoBefore = 0;
		int32 ClipBefore = 0;
		int32 MaxDecals = 0;
		UScctEffectsSubsystem::FStats Before;
	};
}

SCCT_TEST("combat-autofire", ScctCombatFeatureLocal::FScctCombatFeatureTest);
