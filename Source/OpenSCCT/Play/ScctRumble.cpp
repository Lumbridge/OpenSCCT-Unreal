#include "Play/ScctRumble.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctPadApi.h"

namespace
{
	const TCHAR* RumbleResultText(int32 Result)
	{
		switch (Result)
		{
		case EScctRumbleResult::Played: return TEXT("played");
		case EScctRumbleResult::VibrationOff: return TEXT("not played: vibration is off");
		case EScctRumbleResult::ControllerOff: return TEXT("not played: the controller is off");
		case EScctRumbleResult::Outranked: return TEXT("not played: a stronger rumble is running");
		case EScctRumbleResult::RoundEnded: return TEXT("not played: the round has ended");
		default: return TEXT("?");
		}
	}

	FString RumbleNameOf(const FScctPadRumble& Rumble)
	{
		TCHAR Name[UE_ARRAY_COUNT(Rumble.Name) + 1] = {};
		FMemory::Memcpy(Name, Rumble.Name, sizeof(Rumble.Name));
		return FString(Name);
	}
}

bool UScctRumbleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld() && !IsRunningDedicatedServer();
}

TStatId UScctRumbleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctRumbleSubsystem, STATGROUP_Tickables);
}

void UScctRumbleSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const FScctPadApi* Pad = ScctFeature<FScctPadApi>();
	if (Pad == nullptr || Pad->Begin() == 0)
	{
		return;
	}
	if (!bSessionSeen)
	{
		bSessionSeen = true;
		if (FParse::Param(FCommandLine::Get(), TEXT("ScctRumbleOn")))
		{
			// For a check: on for this run only. The settings object follows, so a menu shows it, but nothing is saved.
			UScctControlSettings* Controls = UScctControlSettings::Get();
			Controls->EnsureLoaded();
			Controls->bPadRumble = true;
			Controls->Apply();
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctRumble: vibration on for this run (-ScctRumbleOn), strength %.2f"), Controls->PadRumbleStrength);
		}
	}
	Seconds += DeltaTime;
	RunChecks();

	FScctPadRumble Rumbles[16];
	for (;;)
	{
		const int32 Count = Pad->DrainRumbles(Rumbles, UE_ARRAY_COUNT(Rumbles));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FScctPadRumble& Rumble = Rumbles[Index];
			++RumblesSeen;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctRumble: %s large=%.2f small=%.2f %.2f s priority %d strength %.2f (%s)"),
				*RumbleNameOf(Rumble), Rumble.Low, Rumble.High, Rumble.Seconds, Rumble.Priority, Rumble.Strength, RumbleResultText(Rumble.Result));
			if (Rumble.Play != 0)
			{
				PlayOn(GetWorld()->GetFirstPlayerController(), Rumble, TEXT("match"));
			}
		}
		if (Count < UE_ARRAY_COUNT(Rumbles))
		{
			break;
		}
	}
}

void UScctRumbleSubsystem::RunChecks()
{
	if (!bChecksRead)
	{
		bChecksRead = true;
		FString Text;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctRumbleTest="), Text, false))
		{
			TArray<FString> Items;
			Text.ParseIntoArray(Items, TEXT(";"));
			for (FString& Item : Items)
			{
				FString Name = Item.TrimStartAndEnd(), At;
				float When = 6.0f;
				if (Item.Split(TEXT("@"), &Name, &At))
				{
					When = FCString::Atof(*At);
				}
				Checks.Add({ Name.TrimStartAndEnd(), When });
			}
		}
	}
	const FScctPadApi* Pad = ScctFeature<FScctPadApi>();
	for (int32 Index = Checks.Num() - 1; Index >= 0; --Index)
	{
		if (Seconds < Checks[Index].Value)
		{
			continue;
		}
		const FString Name = Checks[Index].Key;
		Checks.RemoveAt(Index);
		if (Pad->RaiseForCheck(*Name, 0.5f) != 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctRumble: check raised %s on the local player's controller at %.1f s"), *Name, Seconds);
		}
		else
		{
			TCHAR Error[256] = {};
			Pad->LastError(Error, UE_ARRAY_COUNT(Error));
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctRumble: check could not raise %s: %s"), *Name, Error);
		}
	}
}

void UScctRumbleSubsystem::PlayOn(APlayerController* Player, const FScctPadRumble& Rumble, const TCHAR* Why)
{
	if (Player == nullptr || !Player->IsLocalController())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctRumble: %s %s has no local player controller to play on"), Why, *RumbleNameOf(Rumble));
		return;
	}
	UScctRumbleSubsystem* Self = Player->GetWorld() != nullptr ? Player->GetWorld()->GetSubsystem<UScctRumbleSubsystem>() : nullptr;
	uint64 LocalLow = 0, LocalHigh = 0;
	uint64& Low = Self != nullptr ? Self->LowHandle : LocalLow;
	uint64& High = Self != nullptr ? Self->HighHandle : LocalHigh;

	// NativeRumblePadStart sets both motors for the length given, over whatever ran before: the two entries are
	// restarted (a motor at 0 stops), never stacked. XInput drives its left motor from the large channels and its
	// right motor from the small ones.
	Low = Player->PlayDynamicForceFeedback(FMath::Clamp(Rumble.Low, 0.0f, 1.0f), Rumble.Seconds, true, false, false, false, EDynamicForceFeedbackAction::Start, Low);
	High = Player->PlayDynamicForceFeedback(FMath::Clamp(Rumble.High, 0.0f, 1.0f), Rumble.Seconds, false, false, false, true, EDynamicForceFeedbackAction::Start, High);
	if (Self != nullptr)
	{
		++Self->RumblesPlayed;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctRumble: PlayDynamicForceFeedback (%s) %s large %.2f small %.2f for %.2f s on %s (entries %llu, %llu)"),
		Why, *RumbleNameOf(Rumble), Rumble.Low, Rumble.High, Rumble.Seconds, *Player->GetName(), Low, High);
}

void UScctRumbleSubsystem::PlayTest(UWorld* World)
{
	const FScctPadApi* Pad = ScctFeature<FScctPadApi>();
	if (World == nullptr || Pad == nullptr)
	{
		return;
	}
	FScctPadRumble Rumble = {};
	Pad->TestRumble(&Rumble);
	PlayOn(World->GetFirstPlayerController(), Rumble, TEXT("test"));
}
