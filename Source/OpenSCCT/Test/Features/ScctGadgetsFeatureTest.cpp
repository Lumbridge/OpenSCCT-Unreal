// gadgets: the spy and then the merc alone on the run's map (-ScctTestMap, AquaE by default) use a grenade through
// their own keys, as the Godot client's GadgetsSmoke does on SteeE: the spy quick-throws the smoke grenade (the quick
// choice key of its slot), the merc selects the frag grenade with its slot key and throws it with a held alternate
// fire. Each leg fails unless the HUD's charge of that gadget went down and Unreal drew what the throw makes: the
// grenade as a projectile, then the smoke's emitter (spy) or the explosion (merc), from the effects subsystem's
// counts (Play/ScctEffectsSubsystem.h).

#include "Engine/World.h"
#include "Hud/ScctHudModel.h"
#include "Play/ScctEffectsSubsystem.h"
#include "Play/ScctPlayerController.h"
#include "Test/Features/ScctFeatureKit.h"

namespace ScctGadgetsFeatureLocal
{
	constexpr double GadgetSettleSeconds = 2.5;
	constexpr double GadgetSelectSeconds = 0.3;   // the grenade selected, then the throw
	constexpr double GadgetHoldSeconds = 1.2;     // the merc's held alternate fire (a longer hold throws further)
	constexpr double GadgetWatchSeconds = 9.0;    // a frag's fuse and a smoke's burst both come within this
	constexpr int64 GadgetMinWatchFrames = 60;    // and the drawing side has had this many frames to show them

	struct FGadgetLeg
	{
		const TCHAR* Side;
		const TCHAR* Gadget;      // the HUD gadget's class
		const TCHAR* Projectile;  // the effect class prefix of the thrown grenade
		bool bMerc;
	};

	const FGadgetLeg GadgetLegs[] = {
		{ TEXT("spy"), TEXT("SGadSmokeGrenade"), TEXT("SProjSmoke"), false },
		{ TEXT("merc"), TEXT("SGadFragGrenade"), TEXT("SProjFrag"), true },
	};

	class FScctGadgetsFeatureTest : public IScctTest
	{
	public:
		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (Context.GetWorld() == nullptr)
			{
				return FScctTestResult::Running();
			}
			if (!bLegOpen)
			{
				bLegOpen = true;
				bThrown = false;
				Slot = -1;
				FString Error;
				const FString Options = FString::Printf(TEXT("?Side=%s?Bots=0"), GadgetLegs[Leg].Side);
				if (!Stage.Open(Context, FScctTestContext::GetTestMap(TEXT("AquaE")), Options, Error))
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

			const FGadgetLeg& Info = GadgetLegs[Leg];
			// The simulation's seconds: a starved run (shaders still compiling, a few frames a second) plays far less
			// than the wall clock, and a grenade's throw and fuse are the simulation's.
			const double Played = Stage.GetSimSeconds(Context);
			const FScctHudModel* Hud = ScctFeatureKit::GetHud(Context);
			const UScctEffectsSubsystem* Effects = ScctFeatureKit::GetEffects(Context);
			AScctPlayerController* Player = Context.GetPlayer();
			if (Played < GadgetSettleSeconds || Hud == nullptr || Effects == nullptr || Player == nullptr)
			{
				return Played > 25.0 ? Finish(Context, FScctTestResult::Fail(FString::Printf(TEXT("%s: the HUD or the effects never came up"), Info.Side))) : FScctTestResult::Running();
			}

			if (Slot < 0)
			{
				for (int32 Index = 0; Index < Hud->Gadgets.Num(); ++Index)
				{
					Kit.Add(Hud->Text(Hud->Gadgets[Index].ClassId));
					if (Hud->Text(Hud->Gadgets[Index].ClassId) == Info.Gadget)
					{
						Slot = Index;
					}
				}
				if (Slot < 0)
				{
					return Finish(Context, FScctTestResult::Fail(FString::Printf(TEXT("%s: the HUD lists no %s (kit: %s)"), Info.Side, Info.Gadget, *FString::Join(Kit, TEXT(", ")))));
				}
				ChargeBefore = Hud->Gadgets[Slot].Charge;
				Before = Effects->GetStats();
				ClassesBefore = Effects->GetStartedByClass();
				StartAt = Played;
				StartFrame = Stage.GetFrames();
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("%s: %s in slot %d with %d charges (kit: %s)%s"), Info.Side, Info.Gadget, Slot + 1, ChargeBefore,
					*FString::Join(Kit, TEXT(", ")), ScctFeatureKit::IsBroken() ? TEXT("; -ScctFeatureBreak: nothing is thrown") : TEXT("")));
				Kit.Reset();
				ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("%s-before"), Info.Side));
				if (!ScctFeatureKit::IsBroken() && !Info.bMerc)
				{
					// The spy's quick throw: the quick choice key of the grenade's slot.
					const FString Command = FString::Printf(TEXT("GadgetQuickChoice %d"), Slot);
					if (!Player->TapBoundCommand(Command))
					{
						return Finish(Context, FScctTestResult::Fail(FString::Printf(TEXT("%s: no key of the %s's bindings gives '%s'"), Info.Side, Info.Side, *Command)));
					}
				}
				bSelected = !Info.bMerc || ScctFeatureKit::IsBroken() || Hud->State.SelectedGadget == Slot;
				SelectTry = 0;
				SelectTriedAt = -1.0;
				return FScctTestResult::Running();
			}

			// The merc picks the grenade as a player does: the gadget slot keys, then the next-gadget key, until the HUD
			// shows it selected (the HUD's list also holds the flashlights, so its row is not the slot key's number).
			if (!bSelected)
			{
				if (Hud->State.SelectedGadget == Slot)
				{
					bSelected = true;
					StartAt = Played;
					StartFrame = Stage.GetFrames();
					ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("%s: %s selected with '%s'"), Info.Side, Info.Gadget, *SelectCommand));
				}
				else if (SelectTriedAt < 0.0 || Played - SelectTriedAt >= 0.5)
				{
					if (SelectTry >= 12)
					{
						return Finish(Context, FScctTestResult::Fail(FString::Printf(TEXT("%s: neither the slot keys nor the next-gadget key selected the %s (the HUD selects row %d)"),
							Info.Side, Info.Gadget, Hud->State.SelectedGadget)));
					}
					SelectCommand = SelectTry < 4 ? FString::Printf(TEXT("SelectGadget %d"), SelectTry) : FString(TEXT("NextGadget"));
					++SelectTry;
					SelectTriedAt = Played;
					Player->TapBoundCommand(SelectCommand);
				}
				return FScctTestResult::Running();
			}

			const double Since = Played - StartAt;
			if (Info.bMerc && !ScctFeatureKit::IsBroken())
			{
				if (!bThrown && Since >= GadgetSelectSeconds)
				{
					bThrown = true;
					Keys.Hold(Context, TEXT("altfire"), true);
				}
				if (bThrown && Since >= GadgetSelectSeconds + GadgetHoldSeconds && Keys.IsHeld(TEXT("altfire")))
				{
					Keys.Hold(Context, TEXT("altfire"), false);
				}
			}
			if (Since < GadgetWatchSeconds || Stage.GetFrames() - StartFrame < GadgetMinWatchFrames)
			{
				if (Since > 1.5 && !bMidShot)
				{
					bMidShot = true;
					ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("%s-thrown"), Info.Side));
				}
				return FScctTestResult::Running();
			}

			// The leg's verdict.
			const UScctEffectsSubsystem::FStats After = Effects->GetStats();
			const int32 Charge = Hud->Gadgets.IsValidIndex(Slot) ? Hud->Gadgets[Slot].Charge : -1;
			const int32 Projectiles = After.EffectActorsByKind[EScctEffectKind::Projectile] - Before.EffectActorsByKind[EScctEffectKind::Projectile];
			const int32 Emitters = After.EffectActorsByKind[EScctEffectKind::Emitter] - Before.EffectActorsByKind[EScctEffectKind::Emitter];
			const int32 Explosions = After.Explosions - Before.Explosions;
			TArray<FString> Started;
			int32 Grenades = 0;
			for (const TPair<FString, int32>& Pair : Effects->GetStartedByClass())
			{
				const int32 New = Pair.Value - ClassesBefore.FindRef(Pair.Key);
				if (New > 0)
				{
					Started.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key, New));
					// "SBase.SProjSmoke": the class without its package.
					int32 Dot = INDEX_NONE;
					const FString Class = Pair.Key.FindLastChar(TEXT('.'), Dot) ? Pair.Key.Mid(Dot + 1) : Pair.Key;
					Grenades += Class.StartsWith(Info.Projectile) ? New : 0;
				}
			}
			const FString Figures = FString::Printf(TEXT("%s: charge %d -> %d, %d projectiles, %d emitters, %d explosions drawn; started %s"), Info.Side, ChargeBefore, Charge,
				Projectiles, Emitters, Explosions, Started.Num() > 0 ? *FString::Join(Started, TEXT(", ")) : TEXT("nothing"));
			ScctFeatureKit::Note(GetName(), Figures);
			ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("%s-after"), Info.Side));

			TArray<FString> Problems;
			if (ChargeBefore >= 0 && Charge >= ChargeBefore)
			{
				Problems.Add(FString::Printf(TEXT("the HUD's %s charge did not go down (%d -> %d)"), Info.Gadget, ChargeBefore, Charge));
			}
			if (Grenades <= 0)
			{
				Problems.Add(FString::Printf(TEXT("no %s was drawn"), Info.Projectile));
			}
			if (!Info.bMerc && Emitters <= 0)
			{
				Problems.Add(TEXT("the smoke grenade's burst started no emitter"));
			}
			if (Info.bMerc && Explosions <= 0)
			{
				Problems.Add(TEXT("the frag grenade's explosion was not drawn"));
			}
			if (Problems.Num() > 0)
			{
				return Finish(Context, FScctTestResult::Fail(FString::Printf(TEXT("%s: %s (%s)"), Info.Side, *FString::Join(Problems, TEXT("; ")), *Figures)));
			}
			Report.Add(Figures);
			Keys.ReleaseAll(Context);
			if (++Leg >= UE_ARRAY_COUNT(GadgetLegs))
			{
				return FScctTestResult::Pass(FString::Join(Report, TEXT(" || ")));
			}
			bLegOpen = false;
			bMidShot = false;
			return FScctTestResult::Running();
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

		int32 Leg = 0;
		bool bLegOpen = false;
		bool bThrown = false;
		bool bMidShot = false;
		int32 Slot = -1;
		int32 ChargeBefore = 0;
		bool bSelected = false;
		int32 SelectTry = 0;
		double SelectTriedAt = -1.0;
		FString SelectCommand;
		double StartAt = 0.0;
		int64 StartFrame = 0;
		TArray<FString> Kit;
		TArray<FString> Report;
		FScctFeatureStage Stage;
		FScctFeatureKeys Keys;
		UScctEffectsSubsystem::FStats Before;
		TMap<FString, int32> ClassesBefore;
	};
}

SCCT_TEST("gadgets", ScctGadgetsFeatureLocal::FScctGadgetsFeatureTest);
