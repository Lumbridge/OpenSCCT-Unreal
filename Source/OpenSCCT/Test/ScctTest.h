#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

class UGameInstance;
class UScctSessionSubsystem;
class AScctPlayerController;
struct FScctTestApi;

DECLARE_LOG_CATEGORY_EXTERN(LogScctTest, Log, All);

// ---------------------------------------------------------------------------------------------------------------
// NAMED TESTS
//
// A named check of the real client, run with nobody at the keyboard and answered with a verdict and an exit code
// (the Godot client's smoke tests, game/Scripts/Smoke/SmokeRunner.cs). A test is a class with a Step called once a
// frame until it says Passed or Failed, so one test can load a level, wait for its match and watch it for many
// frames. It is registered under its name in its own .cpp:
//
//   class FMyTest : public IScctTest
//   {
//       virtual void Start(FScctTestContext& Context) override { Context.OpenLevel(TEXT("/Game/...?Bots=3")); }
//       virtual FScctTestResult Step(FScctTestContext& Context) override
//       {
//           if (!Context.IsMatchPlaying()) { return FScctTestResult::Running(); }
//           return FScctTestResult::Pass(TEXT("detail"));
//       }
//   };
//   SCCT_TEST("my-test", FMyTest);
//
// and run with -ScctTest=my-test[,other,...] (UScctTestRunnerSubsystem has the switches and the output lines).
// ---------------------------------------------------------------------------------------------------------------

enum class EScctTestState : uint8
{
	Running,
	Passed,
	Failed,
};

struct FScctTestResult
{
	EScctTestState State = EScctTestState::Running;
	FString Detail;

	static FScctTestResult Running() { return FScctTestResult(); }
	static FScctTestResult Pass(const FString& Detail = FString()) { return FScctTestResult{ EScctTestState::Passed, Detail }; }
	static FScctTestResult Fail(const FString& Reason) { return FScctTestResult{ EScctTestState::Failed, Reason }; }
};

/** What a test can see and do while it runs. The runner owns it; it lives as long as the test does. */
struct OPENSCCT_API FScctTestContext
{
	/** Frames since the test started, counting the current one (0 in Start). */
	int32 Frame = 0;

	/** Wall-clock seconds since the test started. */
	double ElapsedSeconds = 0.0;

	/** The game instance; it outlives level travel, as the runner does. */
	UGameInstance* GameInstance = nullptr;

	/** The game world of this frame (a new one after each level travel), or null between levels. */
	UWorld* GetWorld() const;

	/** The short name of the level that world plays ("TestBox"), empty between levels. */
	FString GetLevelName() const;

	/** The session subsystem of that world, or null. */
	UScctSessionSubsystem* GetSession() const;

	/** The local player's controller in that world, or null. */
	AScctPlayerController* GetPlayer() const;

	/** A match plays in that world (a session is presented). */
	bool IsMatchPlaying() const;

	/** The session subsystem's failure (the bridge's reason a match stopped or did not start); empty while all is well. */
	FString GetSessionFailure() const;

	/**
	 * Travels to a level URL ("/Game/OpenSCCT/Test/TestBox/TestBox?ScctMap=TestBox?Bots=3"). False, with the reason in
	 * OutError, when the level's package does not exist; the travel itself happens over the next frames.
	 */
	bool OpenLevel(const FString& URL, FString* OutError = nullptr);

	/** Holds or releases one of the player's actions through the controller, as a real key would (HoldScriptedAction). */
	bool HoldAction(const FString& Action, bool bHeld);

	/** Runs a console command in the current world, as typed in the console. */
	bool RunConsoleCommand(const FString& Command);

	/** The bridge's test table, or null with the reason in OutError (the bridge not loaded, or a mismatched build). */
	const FScctTestApi* GetTestApi(FString* OutError = nullptr) const;

	/** The test table's last error text. */
	FString GetTestApiError() const;
};

/**
 * A named test. Start runs once when the test begins; Step once a frame after that until it returns Passed or
 * Failed. The runner fails a test that runs past the timeout, that trips an ensure, or whose world's session reports
 * a bridge failure (unless AllowsSessionFailure says the test judges that itself).
 */
class OPENSCCT_API IScctTest
{
public:
	virtual ~IScctTest() = default;

	const FString& GetName() const { return Name; }

	virtual void Start(FScctTestContext& Context) {}

	virtual FScctTestResult Step(FScctTestContext& Context) = 0;

	/** True for a test that reads the session's failure itself (the runner then does not fail it for one). */
	virtual bool AllowsSessionFailure() const { return false; }

private:
	friend class FScctTestRegistry;
	FString Name;
};

/** Every named test, registered by SCCT_TEST as the module loads. */
class OPENSCCT_API FScctTestRegistry
{
public:
	using FFactory = TFunction<TUniquePtr<IScctTest>()>;

	static void Register(const TCHAR* Name, FFactory Factory);

	/** A new instance of the named test (case ignored), or null for a name nobody registered. */
	static TUniquePtr<IScctTest> Create(const FString& Name);

	/** The registered names, sorted. */
	static TArray<FString> GetNames();

	struct FAutoRegister
	{
		FAutoRegister(const TCHAR* Name, FFactory Factory) { FScctTestRegistry::Register(Name, MoveTemp(Factory)); }
	};
};

/** Registers Class (an IScctTest with a default constructor) under the name Name ("testbox"). */
#define SCCT_TEST(Name, Class) \
	static const FScctTestRegistry::FAutoRegister UE_JOIN(ScctTestRegistration_, __LINE__)( \
		TEXT(Name), [] { return TUniquePtr<IScctTest>(MakeUnique<Class>()); })
