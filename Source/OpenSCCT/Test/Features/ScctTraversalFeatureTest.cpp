// traversal: the spy climbs at TestRange's stations through the player's keys, as the Godot client's
// RangeTraversalSmoke (game/Scripts/Smoke/TestRangeSmokes.cs) does: the ledge pull-up at Ledge264, the ladder at
// Ladder320 and the pipe at Pipe512. For each station the level is opened with the spy's first spawn on the station's
// spot facing its wall (FeatureTestExports.SetNextStart, Play Level rules), the recorded scenario's keys are played
// (tests/scenarios/traversal/spy-ledge-pull-up, moveset/spy-ladder-up, traversal/spy-pipe-climb-up of the OpenSCCT
// checkout, 60 frames a second) and the spy's feet must end on the top. The stations' spots and heights are those of
// OpenSCCT's fixtures/testrange/stations.json; TestRange is imported with Tools/Test/Import-Fixtures.ps1.

#include "Engine/World.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Test/Features/ScctFeatureKit.h"

namespace ScctTraversalFeatureLocal
{
	constexpr double TraversalSettleSeconds = 3.0;
	/** The spy's first spawn must be this close to the station's spot (uu, across the floor). */
	constexpr float TraversalStartTolerance = 150.0f;

	/** One key change of a scenario, at a frame of 60 a second: forward held or let go, or jump tapped. */
	struct FTraversalKey
	{
		int32 Frame;
		int32 Forward; // 1 hold, 0 let go, -1 unchanged
		bool bJump;
	};

	struct FTraversalStation
	{
		const TCHAR* Id;
		float X, Y, Z;     // the spot (feet)
		int32 Yaw;         // 65536 a turn
		float Top;         // the height the spy's feet must end on
		int32 Duration;    // frames the scenario runs
		TArray<FTraversalKey> Keys;
	};

	TArray<FTraversalStation> MakeTraversalStations()
	{
		return {
			// Walk the last 100 uu to the container's wall, let go, jump on the spot under the lip, forward to climb.
			{ TEXT("Ledge264"), 1408.0f, 5788.0f, 0.0f, 16384, 264.0f, 260, { { 0, 1, false }, { 40, 0, false }, { 55, -1, true }, { 100, 1, false }, { 110, 0, false } } },
			// Walk into the ladder and climb with forward held; let go on the top floor.
			{ TEXT("Ladder320"), 4096.0f, 5738.0f, 0.0f, 16384, 320.0f, 460, { { 0, 1, false }, { 400, 0, false } } },
			// Walk into the pipe, climb with forward held; let go on the roof.
			{ TEXT("Pipe512"), 3584.0f, 5752.0f, 0.0f, 16384, 512.0f, 720, { { 0, 1, false }, { 615, 0, false } } },
		};
	}

	class FScctTraversalFeatureTest : public IScctTest
	{
	public:
		virtual FScctTestResult Step(FScctTestContext& Context) override
		{
			if (Context.GetWorld() == nullptr)
			{
				return FScctTestResult::Running();
			}
			if (Stations.Num() == 0)
			{
				Stations = MakeTraversalStations();
			}
			const FTraversalStation& Station = Stations[StationIndex];
			if (!bOpen)
			{
				bOpen = true;
				NextKey = 0;
				KeysAt = -1.0;
				Highest = -UE_BIG_NUMBER;
				FString Error;
				const FScctFeatureTestApi* Api = ScctFeature<FScctFeatureTestApi>(&Error);
				if (Api == nullptr)
				{
					return FScctTestResult::Fail(FString::Printf(TEXT("the bridge's feature test table is missing: %s"), *Error));
				}
				// The pawn's centre a little over the spot; the spawn settles it onto the floor.
				Api->SetNextStart(Station.X, Station.Y, Station.Z + 60.0f, Station.Yaw);
				if (!Stage.Open(Context, TEXT("TestRange"), TEXT("?Side=spy?Bots=0"), Error))
				{
					Api->ClearNextStart();
					return FScctTestResult::Fail(FString::Printf(TEXT("%s (import it with Tools/Test/Import-Fixtures.ps1)"), *Error));
				}
				return FScctTestResult::Running();
			}
			const FScctTestResult Ready = Stage.Step(Context);
			if (Ready.State != EScctTestState::Passed)
			{
				return Finish(Context, Ready);
			}
			const FScctBridgePawn* Pawn = ScctFeatureKit::GetLocalPawn(Context);
			if (Pawn == nullptr)
			{
				return Finish(Context, FScctTestResult::Fail(FString::Printf(TEXT("%s: the spy has no pawn"), Station.Id)));
			}
			const double Played = Stage.GetPlayedSeconds(Context);
			const float Feet = Pawn->Z - Pawn->CollisionHeight;
			if (Played < TraversalSettleSeconds)
			{
				return FScctTestResult::Running();
			}

			if (KeysAt < 0.0)
			{
				const float Off = static_cast<float>(FVector::Dist2D(FVector(Pawn->X, Pawn->Y, 0.0), FVector(Station.X, Station.Y, 0.0)));
				ScctFeatureKit::Note(GetName(), FString::Printf(TEXT("%s: the spy starts at (%.0f, %.0f) feet z %.0f, yaw %.0f, %.0f uu from the spot%s"), Station.Id, Pawn->X, Pawn->Y, Feet, Pawn->Yaw, Off,
					ScctFeatureKit::IsBroken() ? TEXT("; -ScctFeatureBreak: no key is played") : TEXT("")));
				if (Off > TraversalStartTolerance)
				{
					return Finish(Context, FScctTestResult::Fail(FString::Printf(TEXT("%s: the spy did not start on the station's spot (%.0f uu away): the bridge's start was not used"), Station.Id, Off)));
				}
				StartFeet = Feet;
				KeysAt = Played;
				KeysTick = ScctFeatureKit::GetClientTicks(Context);
				ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("%s-start"), Station.Id));
			}

			// The scenario's frames are the simulation's ticks (60 a second): counted on the client's ticks, so a slow
			// machine's long frames do not stretch or squeeze the keys against the simulation.
			const int64 Ticks = ScctFeatureKit::GetClientTicks(Context);
			const double Frames = Ticks >= 0 && KeysTick >= 0 ? static_cast<double>(Ticks - KeysTick) : (Played - KeysAt) * 60.0;
			if (bJumpDown && Frames >= JumpUpFrame)
			{
				Context.HoldAction(TEXT("jump"), false);
				bJumpDown = false;
			}
			while (!ScctFeatureKit::IsBroken() && Station.Keys.IsValidIndex(NextKey) && Frames >= Station.Keys[NextKey].Frame)
			{
				const FTraversalKey& Key = Station.Keys[NextKey++];
				if (Key.Forward >= 0)
				{
					Keys.Hold(Context, TEXT("forward"), Key.Forward == 1);
				}
				if (Key.bJump)
				{
					Context.HoldAction(TEXT("jump"), true);
					bJumpDown = true;
					JumpUpFrame = Frames + 6.0;
				}
			}
			Highest = FMath::Max(Highest, Feet);
			if (!bMidShot && Frames >= Station.Duration * 0.5)
			{
				bMidShot = true;
				ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("%s-climbing"), Station.Id));
			}
			// The scenario's length and a second more for the climb to end.
			if (Frames < Station.Duration + 60.0)
			{
				return FScctTestResult::Running();
			}

			Keys.ReleaseAll(Context);
			ScctFeatureKit::Shot(GetName(), FString::Printf(TEXT("%s-end"), Station.Id));
			const FString Figures = FString::Printf(TEXT("%s: feet z %.0f -> %.0f (highest %.0f), the top at %.0f, end at (%.0f, %.0f)"), Station.Id, StartFeet, Feet, Highest, Station.Top, Pawn->X, Pawn->Y);
			ScctFeatureKit::Note(GetName(), Figures);
			if (Feet < Station.Top - 4.0f)
			{
				return FScctTestResult::Fail(FString::Printf(TEXT("%s: the spy did not climb onto the %.0f top (%s)"), Station.Id, Station.Top, *Figures));
			}
			Report.Add(Figures);
			if (++StationIndex >= Stations.Num())
			{
				return FScctTestResult::Pass(FString::Join(Report, TEXT("; ")));
			}
			bOpen = false;
			bMidShot = false;
			return FScctTestResult::Running();
		}

	private:
		FScctTestResult Finish(FScctTestContext& Context, const FScctTestResult& Result)
		{
			if (Result.State != EScctTestState::Running)
			{
				Keys.ReleaseAll(Context);
				if (bJumpDown)
				{
					Context.HoldAction(TEXT("jump"), false);
					bJumpDown = false;
				}
			}
			return Result;
		}

		TArray<FTraversalStation> Stations;
		int32 StationIndex = 0;
		bool bOpen = false;
		bool bMidShot = false;
		bool bJumpDown = false;
		double JumpUpFrame = 0.0;
		int32 NextKey = 0;
		double KeysAt = -1.0;
		int64 KeysTick = -1;
		float StartFeet = 0.0f;
		float Highest = 0.0f;
		TArray<FString> Report;
		FScctFeatureStage Stage;
		FScctFeatureKeys Keys;
	};
}

SCCT_TEST("traversal", ScctTraversalFeatureLocal::FScctTraversalFeatureTest);
