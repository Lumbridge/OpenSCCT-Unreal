#include "Test/ScctTestRunnerSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"

namespace
{
	FString TestLine(const FString& Name, const TCHAR* Verdict, const FString& Detail)
	{
		return Detail.IsEmpty()
			? FString::Printf(TEXT("SCCT_TEST %s %s"), *Name, Verdict)
			: FString::Printf(TEXT("SCCT_TEST %s %s %s"), *Name, Verdict, *Detail);
	}

	/** One line of the output: a failure is written as an error, so it stands out in the log as well. */
	void WriteTestLine(const FString& Text, bool bError)
	{
		if (bError)
		{
			UE_LOG(LogScctTest, Error, TEXT("%s"), *Text);
		}
		else
		{
			UE_LOG(LogScctTest, Display, TEXT("%s"), *Text);
		}
	}

	/** The reason on one line: the bridge's failures are whole exception texts. */
	FString TestFirstLine(const FString& Text)
	{
		FString Result = Text;
		int32 End = INDEX_NONE;
		if (Result.FindChar(TEXT('\n'), End))
		{
			Result.LeftInline(End);
		}
		Result.TrimStartAndEndInline();
		return Result;
	}
}

bool UScctTestRunnerSubsystem::IsTestRun()
{
	FString Names;
	return FParse::Value(FCommandLine::Get(), TEXT("ScctTest="), Names, false) || FParse::Param(FCommandLine::Get(), TEXT("ScctTestList"));
}

bool UScctTestRunnerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Only in a run that asked for tests, and only for a real game instance (not the editor's class defaults).
	const UGameInstance* GameInstance = Cast<UGameInstance>(Outer);
	return GameInstance != nullptr && !GameInstance->IsTemplate() && IsTestRun();
}

void UScctTestRunnerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (FParse::Param(FCommandLine::Get(), TEXT("ScctTestList")))
	{
		const TArray<FString> Names = FScctTestRegistry::GetNames();
		WriteTestLine(FString::Printf(TEXT("SCCT_TEST list: %s"), *FString::Join(Names, TEXT(", "))), false);
		Quit(ExitPassed);
		return;
	}

	FString Names;
	FParse::Value(FCommandLine::Get(), TEXT("ScctTest="), Names, false);
	Names.ParseIntoArray(Queue, TEXT(","));
	for (FString& Name : Queue)
	{
		Name.TrimStartAndEndInline();
	}
	Queue.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });

	float Timeout = 120.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctTestTimeout="), Timeout) && Timeout > 0.0f)
	{
		TimeoutSeconds = Timeout;
	}

	// Every name is checked before anything runs: a typo in the last one must not cost a long run first.
	for (const FString& Name : Queue)
	{
		if (!FScctTestRegistry::Create(Name).IsValid())
		{
			WriteTestLine(TestLine(Name, TEXT("FAIL"), FString::Printf(TEXT("unknown test (known: %s)"),
				*FString::Join(FScctTestRegistry::GetNames(), TEXT(", ")))), true);
			Quit(ExitUnknownTest);
			return;
		}
	}
	if (Queue.Num() == 0)
	{
		WriteTestLine(FString::Printf(TEXT("SCCT_TEST FAIL no test named (known: %s)"), *FString::Join(FScctTestRegistry::GetNames(), TEXT(", "))), true);
		Quit(ExitUnknownTest);
		return;
	}

	UE_LOG(LogScctTest, Log, TEXT("Running %d test(s): %s (timeout %.0f s each)"), Queue.Num(), *FString::Join(Queue, TEXT(", ")), TimeoutSeconds);
	EnsureHandle = FCoreDelegates::OnEnsureFailed.AddUObject(this, &UScctTestRunnerSubsystem::OnEnsure);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctTestRunnerSubsystem::Tick));
}

void UScctTestRunnerSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	FCoreDelegates::OnEnsureFailed.Remove(EnsureHandle);
	if (Current.IsValid() && !bDone)
	{
		// The game is going away under a running test (the window was closed, or something else asked to quit).
		Finish(EScctTestState::Failed, TEXT("the game shut down while the test ran"));
	}
	Current.Reset();
	Super::Deinitialize();
}

void UScctTestRunnerSubsystem::OnEnsure(const ANSICHAR* Expression, const ANSICHAR* File, int32 LineNumber, const TCHAR* Message, const TCHAR* Combined)
{
	FScopeLock Lock(&EnsureLock);
	if (EnsureText.IsEmpty())
	{
		EnsureText = FString::Printf(TEXT("ensure failed: %hs (%hs:%d) %s"), Expression != nullptr ? Expression : "?",
			File != nullptr ? File : "?", LineNumber, Message != nullptr ? Message : TEXT(""));
	}
}

bool UScctTestRunnerSubsystem::BeginNext()
{
	if (Queue.Num() == 0)
	{
		return false;
	}

	const FString Name = Queue[0];
	Queue.RemoveAt(0);
	Current = FScctTestRegistry::Create(Name);
	Context = FScctTestContext();
	Context.GameInstance = GetGameInstance();
	StartSeconds = FPlatformTime::Seconds();
	{
		FScopeLock Lock(&EnsureLock);
		EnsureText.Reset();
	}

	WriteTestLine(TestLine(Current->GetName(), TEXT("START"), FString()), false);
	Current->Start(Context);
	return true;
}

bool UScctTestRunnerSubsystem::Tick(float DeltaSeconds)
{
	if (bDone)
	{
		return false;
	}

	if (!Current.IsValid() && !BeginNext())
	{
		Quit(Failed > 0 ? ExitFailed : ExitPassed);
		return false;
	}

	++Context.Frame;
	Context.ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
	FScctTestResult Result = Current->Step(Context);

	if (Result.State == EScctTestState::Running)
	{
		FString Ensure;
		{
			FScopeLock Lock(&EnsureLock);
			Ensure = EnsureText;
		}
		const FString SessionFailure = Current->AllowsSessionFailure() ? FString() : Context.GetSessionFailure();
		if (!Ensure.IsEmpty())
		{
			Result = FScctTestResult::Fail(Ensure);
		}
		else if (!SessionFailure.IsEmpty())
		{
			Result = FScctTestResult::Fail(FString::Printf(TEXT("bridge: %s"), *SessionFailure));
		}
		else if (Context.ElapsedSeconds > TimeoutSeconds)
		{
			Result = FScctTestResult::Fail(FString::Printf(TEXT("timed out after %.0f s"), TimeoutSeconds));
		}
	}

	if (Result.State != EScctTestState::Running)
	{
		Finish(Result.State, Result.Detail);
	}
	return true;
}

void UScctTestRunnerSubsystem::Finish(EScctTestState State, const FString& Detail)
{
	const bool bPassed = State == EScctTestState::Passed;
	WriteTestLine(TestLine(Current->GetName(), bPassed ? TEXT("OK") : TEXT("FAIL"), bPassed ? Detail : TestFirstLine(Detail)), !bPassed);
	if (!bPassed && Detail.Contains(TEXT("\n")))
	{
		UE_LOG(LogScctTest, Log, TEXT("%s"), *Detail);
	}
	++(bPassed ? Passed : Failed);
	Current.Reset();
}

void UScctTestRunnerSubsystem::Quit(uint8 ExitCode)
{
	bDone = true;
	if (Passed + Failed > 0)
	{
		WriteTestLine(FString::Printf(TEXT("SCCT_TEST done: %d passed, %d failed (exit %d)"), Passed, Failed, ExitCode), Failed > 0);
	}
	GLog->Flush();
	FPlatformMisc::RequestExitWithStatus(false, ExitCode);

	// On Windows a clean exit's code is lost (the main loop returns 0 whatever the request said), so a failing run
	// ends itself with its code once the engine has shut down, just before the modules are let go. A passing run
	// goes all the way through the normal shutdown, so a crash there still shows as a failure.
	if (ExitCode != ExitPassed)
	{
		static uint8 FinalCode = 0;
		FinalCode = ExitCode;
		FCoreDelegates::OnExit.AddLambda([]
		{
			UE_LOG(LogScctTest, Log, TEXT("Ending the process with exit code %d"), FinalCode);
			FPlatformMisc::RequestExitWithStatus(true, FinalCode);
		});
	}
}
