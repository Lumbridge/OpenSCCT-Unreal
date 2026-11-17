#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Test/ScctTest.h"
#include "ScctTestRunnerSubsystem.generated.h"

/**
 * Runs the named tests (Test/ScctTest.h) asked for on the command line and ends the process with a verdict, as the
 * Godot client's SmokeRunner does. It belongs to the game instance and ticks on the core ticker, so a test goes on
 * across the level travel it starts.
 *
 * Switches:
 *   -ScctTest=name[,name...]   runs the tests in that order, then quits
 *   -ScctTestTimeout=S         seconds each test may run (default 120)
 *   -ScctTestList              writes the known names and quits
 *   -ScctTestMap=Name          the map a test that plays "a map" uses (FScctTestContext::GetTestMap)
 *   -ScctTestOptions=?A=1?B=2  URL options added to every level a test opens
 * Tools/Test/Run-ScctTests.ps1 is the suite driver: one process per test, repeats, expected failures, crash checks
 * and a JUnit report.
 *
 * Output (LogScctTest, one line each):
 *   SCCT_TEST <name> START
 *   SCCT_TEST <name> OK [detail]
 *   SCCT_TEST <name> FAIL <reason>
 *   SCCT_TEST done: N passed, M failed
 * A test fails on its own verdict, on the timeout, on an ensure, or when its world's session reports a bridge failure
 * (UScctSessionSubsystem::GetFailure). Exit code: 0 when every test passed, 1 when one failed, 2 for a name nobody
 * registered (the known names are written and nothing runs). A passing run whose last test ends the game its own way
 * (IScctTest::QuitThroughGame, the front end's QUIT) writes its summary first and exits through that path with 0.
 *
 * Unreal builds have no C++ exceptions to catch: a test that crashes ends the process with the crash's own non-zero
 * code. The bridge's exceptions are caught on the C# side and arrive as the session's failure.
 */
UCLASS()
class OPENSCCT_API UScctTestRunnerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr uint8 ExitPassed = 0;
	static constexpr uint8 ExitFailed = 1;
	static constexpr uint8 ExitUnknownTest = 2;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Tests are being run in this process (-ScctTest=). */
	static bool IsTestRun();

private:
	bool Tick(float DeltaSeconds);

	/** Begins the next queued test; false when none is left. */
	bool BeginNext();

	void Finish(EScctTestState State, const FString& Detail);

	/** Writes the summary and asks the engine to quit with the exit code. */
	void Quit(uint8 ExitCode);

	void OnEnsure(const ANSICHAR* Expression, const ANSICHAR* File, int32 Line, const TCHAR* Message, const TCHAR* Combined);

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle EnsureHandle;

	TArray<FString> Queue;
	double TimeoutSeconds = 120.0;

	TUniquePtr<IScctTest> Current;

	/** The test that finished last; kept so the last one can end the run through the game (QuitThroughGame). */
	TUniquePtr<IScctTest> Last;

	/** The last test pressed the game's own way out and the runner waits for the engine to go down. */
	bool bQuittingThroughGame = false;
	double QuitDeadline = 0.0;
	static constexpr double QuitThroughGameSeconds = 15.0;
	FScctTestContext Context;
	double StartSeconds = 0.0;

	/** The first ensure since the test began (ensures can fire on any thread). */
	FCriticalSection EnsureLock;
	FString EnsureText;

	int32 Passed = 0;
	int32 Failed = 0;
	bool bDone = false;
};
