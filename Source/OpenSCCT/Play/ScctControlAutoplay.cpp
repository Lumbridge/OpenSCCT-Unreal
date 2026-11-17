#include "Play/ScctControlAutoplay.h"

#include "Algo/Find.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

namespace
{
	struct FNamedKey
	{
		const TCHAR* Name;
		const TCHAR* Key; // an Unreal key name: the key the default bindings put this action on
	};

	// The physical keys of the default bindings. A player who rebinds changes what these steps do, as it would
	// for fingers on the same keys.
	const FNamedKey NamedKeys[] = {
		{ TEXT("forward"), TEXT("W") },
		{ TEXT("back"), TEXT("S") },
		{ TEXT("left"), TEXT("A") },
		{ TEXT("right"), TEXT("D") },
		{ TEXT("fire"), TEXT("LeftMouseButton") },
		{ TEXT("altfire"), TEXT("RightMouseButton") },
		{ TEXT("jump"), TEXT("SpaceBar") },
		{ TEXT("crouch"), TEXT("C") },
		{ TEXT("use"), TEXT("LeftShift") },
		{ TEXT("roll"), TEXT("Q") },
		{ TEXT("reload"), TEXT("R") },
		{ TEXT("weapon"), TEXT("E") },
		{ TEXT("walk"), TEXT("MiddleMouseButton") },
		{ TEXT("firstperson"), TEXT("X") },
	};

	constexpr float TapSeconds = 0.1f;
	constexpr float MouseSeconds = 0.5f;
	constexpr float WaitSeconds = 1.0f;

	void SendKey(APlayerController& Controller, const FKey& Key, EInputEvent Event)
	{
		Controller.InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.0f : 1.0f));
	}

	void SendAxis(APlayerController& Controller, const FKey& Key, float Delta)
	{
		// One sample: the mouse keys are only read as axes when an event says it carries samples.
		Controller.InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Axis, Delta, 1));
	}
}

bool FScctControlAutoplay::Parse(const FString& Script, FString& OutError)
{
	TArray<FStep> Read;
	TArray<FString> Entries;
	Script.ParseIntoArray(Entries, TEXT(";"));
	for (const FString& Entry : Entries)
	{
		TArray<FString> Words;
		Entry.TrimStartAndEnd().ParseIntoArrayWS(Words);
		if (Words.Num() == 0)
		{
			continue;
		}

		FStep Step;
		Step.Text = FString::Join(Words, TEXT(" "));
		const FString Action = Words[0].ToLower();
		const auto Number = [&Words](int32 At, float Default) { return Words.IsValidIndex(At) ? FCString::Atof(*Words[At]) : Default; };

		// One or more named actions held together: "forward+left".
		const auto AddNamedKeys = [&Step, &OutError](const FString& Joined)
		{
			TArray<FString> Names;
			Joined.ParseIntoArray(Names, TEXT("+"));
			for (const FString& Name : Names)
			{
				const FNamedKey* Found = Algo::FindByPredicate(NamedKeys, [&Name](const FNamedKey& Named) { return Name == Named.Name; });
				if (Found == nullptr)
				{
					OutError = FString::Printf(TEXT("'%s' is not an autoplay action (in '%s')"), *Name, *Step.Text);
					return false;
				}
				Step.Keys.Add(FKey(FName(Found->Key)));
			}
			return true;
		};

		if (Action == TEXT("wait"))
		{
			Step.Seconds = Number(1, WaitSeconds);
		}
		else if (Action == TEXT("turn") || Action == TEXT("look"))
		{
			if (Words.Num() < 2)
			{
				OutError = FString::Printf(TEXT("'%s' needs degrees"), *Step.Text);
				return false;
			}
			(Action == TEXT("turn") ? Step.TurnDegrees : Step.LookDegrees) = Number(1, 0.0f);
			Step.Seconds = Number(2, MouseSeconds);

			// "turn 90 1 forward": the mouse moves while keys are held, as a player turns a corner.
			if (Words.IsValidIndex(3) && !AddNamedKeys(Words[3].ToLower()))
			{
				return false;
			}
		}
		else if (Action == TEXT("wheel"))
		{
			Step.WheelNotches = FMath::RoundToInt(Number(1, 1.0f));
			Step.Seconds = TapSeconds;
		}
		else if (Action == TEXT("key"))
		{
			const FKey Key = Words.IsValidIndex(1) ? FKey(FName(*Words[1])) : FKey();
			if (!Key.IsValid())
			{
				OutError = FString::Printf(TEXT("'%s' does not name an Unreal key"), *Step.Text);
				return false;
			}
			Step.Keys.Add(Key);
			Step.Seconds = Number(2, TapSeconds);
		}
		else
		{
			if (!AddNamedKeys(Action))
			{
				return false;
			}
			Step.Seconds = Number(1, TapSeconds);
		}

		Step.Seconds = FMath::Max(Step.Seconds, 0.0f);
		Read.Add(MoveTemp(Step));
	}

	Steps = MoveTemp(Read);
	Index = 0;
	bBegun = false;
	bFinalReportDue = Steps.Num() > 0;
	return true;
}

void FScctControlAutoplay::Tick(float DeltaSeconds, APlayerController& Controller, float MouseCountsPerDegree)
{
	if (Index >= Steps.Num())
	{
		if (bFinalReportDue)
		{
			// One frame after the last keys came up, so the line shows the last step's result.
			bFinalReportDue = false;
			if (OnReport)
			{
				OnReport(TEXT("finished"));
			}
		}
		return;
	}

	const FStep& Step = Steps[Index];
	if (!bBegun)
	{
		if (OnReport)
		{
			OnReport(FString::Printf(TEXT("step %d of %d '%s'"), Index + 1, Steps.Num(), *Step.Text));
		}
		Begin(Step, Controller);
		MoveMouse(Step, Controller, MouseCountsPerDegree);
		// A step lasts at least this frame: a key that went down now comes up in a later one, as a real key does.
		return;
	}

	Elapsed += DeltaSeconds;
	MoveMouse(Step, Controller, MouseCountsPerDegree);
	if (Elapsed >= Step.Seconds)
	{
		for (const FKey& Key : Step.Keys)
		{
			SendKey(Controller, Key, IE_Released);
		}
		++Index;
		bBegun = false;
	}
}

void FScctControlAutoplay::Begin(const FStep& Step, APlayerController& Controller)
{
	bBegun = true;
	Elapsed = 0.0f;
	SentX = 0;
	SentY = 0;

	for (const FKey& Key : Step.Keys)
	{
		SendKey(Controller, Key, IE_Pressed);
	}

	// A wheel notch arrives from Unreal's viewport as a press and release of the scroll key plus the wheel axis.
	const FKey ScrollKey = Step.WheelNotches > 0 ? EKeys::MouseScrollUp : EKeys::MouseScrollDown;
	for (int32 Notch = 0; Notch < FMath::Abs(Step.WheelNotches); ++Notch)
	{
		SendKey(Controller, ScrollKey, IE_Pressed);
		SendKey(Controller, ScrollKey, IE_Released);
		SendAxis(Controller, EKeys::MouseWheelAxis, Step.WheelNotches > 0 ? 1.0f : -1.0f);
	}
}

void FScctControlAutoplay::MoveMouse(const FStep& Step, APlayerController& Controller, float CountsPerDegree)
{
	if (Step.TurnDegrees == 0.0f && Step.LookDegrees == 0.0f)
	{
		return;
	}

	const float Part = Step.Seconds > 0.0f ? FMath::Clamp(Elapsed / Step.Seconds, 0.0f, 1.0f) : 1.0f;
	const int32 WantedX = FMath::RoundToInt(Step.TurnDegrees * CountsPerDegree * Part);
	const int32 WantedY = FMath::RoundToInt(Step.LookDegrees * CountsPerDegree * Part);
	if (WantedX != SentX)
	{
		SendAxis(Controller, EKeys::MouseX, static_cast<float>(WantedX - SentX));
		SentX = WantedX;
	}
	if (WantedY != SentY)
	{
		// Unreal's mouse Y is positive when the mouse is pushed away (it flips the screen's direction itself).
		SendAxis(Controller, EKeys::MouseY, static_cast<float>(WantedY - SentY));
		SentY = WantedY;
	}
}
