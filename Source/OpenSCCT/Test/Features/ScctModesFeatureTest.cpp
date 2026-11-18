// modes: Disk Hunt and Deathmatch on the run's map (-ScctTestMap, AquaE by default), each opened as the front end opens
// it (?Mode=DiskHunt, ?Mode=Deathmatch) with a spy and one bot, as the Godot client's PlayDiskHunt and PlayDeathmatch
// smokes start them. Each fails unless the match plays and the HUD shows the mode: the replicated round names it, and
// its counter and objectives are the mode's (disks taken of the disks to win for Disk Hunt; no objectives to do in a
// Deathmatch, where the score counts kills).

#include "Engine/World.h"
#include "Hud/ScctHudModel.h"
#include "Test/Features/ScctFeatureKit.h"

namespace ScctModesFeatureLocal
{
	constexpr double ModesRoundSeconds = 20.0;
	constexpr double ModesSettleSeconds = 3.0;

	struct FModeLeg
	{
		const TCHAR* Key;   // the URL's mode key
		const TCHAR* Title;
		int32 GameMode;     // FScctHudRound::GameMode
	};

	const FModeLeg ModeLegs[] = {
		{ TEXT("DiskHunt"), TEXT("Disk Hunt"), 4 },
		{ TEXT("Deathmatch"), TEXT("Deathmatch"), 6 },
	};

	const TCHAR* ModeTitle(int32 GameMode)
	{
		switch (GameMode)
		{
		case 1: return TEXT("Extraction");
		case 2: return TEXT("Sabotage");
		case 3: return TEXT("Neutralization");
		case 4: return TEXT("Disk Hunt");
		case 5: return TEXT("Story");
		case 6: return TEXT("Deathmatch");
		case 7: return TEXT("Merc Deathmatch");
		default: return TEXT("none");
		}
	}

	class FScctModesFeatureTest : public IScctTest
	{
	public:
		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (Context.GetWorld() == nullptr)
			{
				return FScctTestResult::Running();
			}
			const FModeLeg& Leg = ModeLegs[LegIndex];
			if (!bOpen)
			{
				bOpen = true;
				FString Error;
				// -ScctFeatureBreak opens the map without its mode: the map's own (Story).
				const FString Options = ScctFeatureKit::IsBroken() ? FString(TEXT("?Side=spy?Bots=1")) : FString::Printf(TEXT("?Side=spy?Bots=1?Mode=%s"), Leg.Key);
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("opening %s with %s"), *FScctTestContext::GetTestMap(TEXT("AquaE")), *Options));
				if (!Stage.Open(Context, FScctTestContext::GetTestMap(TEXT("AquaE")), Options, Error))
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
			const FScctHudModel* Hud = ScctFeatureKit::GetHud(Context);
			if (Played < ModesSettleSeconds || Hud == nullptr || !Hud->bHasRound || Hud->Round.Valid == 0)
			{
				return Played > ModesRoundSeconds
					? FScctTestResult::Fail(FString::Printf(TEXT("%s: the HUD had no round state %.0f s into the match"), Leg.Title, ModesRoundSeconds))
					: FScctTestResult::Running();
			}

			const FScctHudRound& Round = Hud->Round;
			ScctFeatureKit::Shot(GetName(), Leg.Key);
			const FString Figures = FString::Printf(TEXT("%s: the HUD's round is %s (mode %d), counter %d, objectives %d (%d left, %d listed), disks %d of %d, team lives %d/%d, time limit %d min"),
				Leg.Title, ModeTitle(Round.GameMode), Round.GameMode, Round.CounterMode, Round.NumObjectives, Round.ObjectivesLeft, Hud->ObjectiveLines.Num(),
				Round.NumDisks, Round.NumDisksToWin, Round.TeamLives[0], Round.TeamLives[1], Round.TimeLimit);
			ScctFeatureKit::Note(GetName(), Figures);

			TArray<FString> Problems;
			if (Round.GameMode != Leg.GameMode)
			{
				Problems.Add(FString::Printf(TEXT("the HUD shows %s (mode %d), not %s (mode %d)"), ModeTitle(Round.GameMode), Round.GameMode, Leg.Title, Leg.GameMode));
			}
			else if (Leg.GameMode == 4 && (Round.CounterMode != 2 || Round.NumDisksToWin <= 0))
			{
				Problems.Add(FString::Printf(TEXT("the HUD's counter is not the disks taken of the disks to win (counter %d, %d disks to win)"), Round.CounterMode, Round.NumDisksToWin));
			}
			else if (Leg.GameMode == 6 && (Round.CounterMode == 0 || Round.ObjectivesLeft > 0))
			{
				Problems.Add(FString::Printf(TEXT("the HUD counts objectives in a Deathmatch (counter %d, %d objectives left)"), Round.CounterMode, Round.ObjectivesLeft));
			}
			if (Problems.Num() > 0)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("%s: %s (%s)"), Leg.Title, *FString::Join(Problems, TEXT("; ")), *Figures));
			}
			Report.Add(Figures);
			if (++LegIndex >= UE_ARRAY_COUNT(ModeLegs))
			{
				return FScctTestResult::Pass(FString::Join(Report, TEXT("; ")));
			}
			bOpen = false;
			return FScctTestResult::Running();
		}

	private:
		int32 LegIndex = 0;
		bool bOpen = false;
		FScctFeatureStage Stage;
		TArray<FString> Report;
	};
}

SCCT_TEST("modes", ScctModesFeatureLocal::FScctModesFeatureTest);
