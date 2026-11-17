#include "Menu/ScctMouseAimTest.h"

#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "Menu/SScctMouseAimPanel.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Play/ScctPlayerController.h"

namespace
{
	/** The turn sent each time: 20 counts a frame for 10 frames, sideways to the right. */
	constexpr int32 LookTestCountsPerFrame = 20;
	constexpr int32 LookTestFrames = 10;
	/** Seconds after the last count before the turn is read (the view's easing has caught up long before). */
	constexpr float LookTestMeasureAfter = 1.0f;
	/** Seconds the local player's view is up before the first turn, so the spawn has settled. */
	constexpr float LookTestSettle = 4.0f;

	struct FLookTestArgs
	{
		bool bMerc = false;
		FString Field;
		FString Value;
	};

	bool ReadLookTestArgs(FLookTestArgs& Out)
	{
		FString Text;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ScctLookTest="), Text, false))
		{
			return false;
		}
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), false);
		if (Parts.Num() < 3)
		{
			return false;
		}
		Out.bMerc = Parts[0].TrimStartAndEnd().Equals(TEXT("merc"), ESearchCase::IgnoreCase);
		Out.Field = Parts[1].TrimStartAndEnd();
		Out.Value = Parts[2].TrimStartAndEnd();
		return true;
	}

	/** The JSON object of Key in Json ("controls_merc_kbm": { ... }), on one line; empty when it is not there. */
	FString JsonBlock(const FString& Json, const FString& Key)
	{
		const int32 At = Json.Find(FString::Printf(TEXT("\"%s\""), *Key));
		const int32 Open = At == INDEX_NONE ? INDEX_NONE : Json.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
		if (Open == INDEX_NONE)
		{
			return FString();
		}
		int32 Depth = 0;
		for (int32 Index = Open; Index < Json.Len(); ++Index)
		{
			Depth += Json[Index] == TEXT('{') ? 1 : Json[Index] == TEXT('}') ? -1 : 0;
			if (Depth == 0)
			{
				FString Block = Json.Mid(Open, Index - Open + 1);
				Block.ReplaceInline(TEXT("\r"), TEXT(""));
				Block.ReplaceInline(TEXT("\n"), TEXT(" "));
				while (Block.ReplaceInline(TEXT("  "), TEXT(" ")) > 0)
				{
				}
				return Block;
			}
		}
		return FString();
	}

	/** A top-level value of the settings file ("invert_y": false), for the fields kept outside the side's block. */
	FString JsonLine(const FString& Json, const FString& Key)
	{
		const int32 At = Json.Find(FString::Printf(TEXT("\"%s\""), *Key));
		if (At == INDEX_NONE)
		{
			return FString();
		}
		const int32 End = Json.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
		return Json.Mid(At, End == INDEX_NONE ? MAX_int32 : End - At).TrimStartAndEnd();
	}

	AScctPlayerController* FindLocalController(UWorld* World)
	{
		return World != nullptr ? Cast<AScctPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}
}

bool UScctMouseAimTest::ShouldCreateSubsystem(UObject* Outer) const
{
	FLookTestArgs Args;
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld() && ReadLookTestArgs(Args) && Super::ShouldCreateSubsystem(Outer);
}

TStatId UScctMouseAimTest::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctMouseAimTest, STATGROUP_Tickables);
}

void UScctMouseAimTest::BeginTurn(const AScctPlayerController& Controller)
{
	StartYaw = Controller.GetLookView().AimYaw;
	FramesSent = 0;
	Clock = 0.0f;
}

float UScctMouseAimTest::TurnedSince(const AScctPlayerController& Controller) const
{
	return FRotator::NormalizeAxis(Controller.GetLookView().AimYaw - StartYaw);
}

void UScctMouseAimTest::ChangeField(const FString& Value, const TCHAR* Why)
{
	FLookTestArgs Args;
	ReadLookTestArgs(Args);
	const FScctMouseAimField* Field = ScctMouseAim::FindField(Args.Field);

	// What the Options screen does: the row's edit, then APPLY and SAVE.
	ScctMouseAim::Begin();
	FString Error;
	if (!ScctMouseAim::SetField(Args.bMerc, Args.Field, Value, Error))
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("ScctLookTest: %s"), *Error);
		return;
	}
	ScctMouseAim::Apply();
	ScctMouseAim::Save();

	// What the bridge holds now, read back from it rather than from the draft.
	UScctControlSettings* Settings = UScctControlSettings::Get();
	Settings->Refresh();
	const FScctMouseAimValues Now = FScctMouseAimValues::From(*Settings);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookTest: %s: %s %s set to %s; the bridge reports %s"), Why, Args.bMerc ? TEXT("merc") : TEXT("spy"), *Args.Field, *Value,
		Field != nullptr ? *Field->Show(Now, Args.bMerc) : TEXT("?"));

	FString Json;
	const FString File = UScctControlSettings::GetSettingsFile();
	if (FFileHelper::LoadFileToString(Json, *File))
	{
		const FString Block = Args.bMerc ? TEXT("controls_merc_kbm") : TEXT("controls_spy_kbm");
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookTest: %s has \"%s\": %s | %s | %s | %s"), *File, *Block, *JsonBlock(Json, Block),
			*JsonLine(Json, TEXT("invert_y")), *JsonLine(Json, TEXT("mouse_scale")), *JsonLine(Json, TEXT("mouse_wheel")));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctLookTest: %s could not be read"), *File);
	}
}

void UScctMouseAimTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Stage == EStage::Done)
	{
		return;
	}
	AScctPlayerController* Controller = FindLocalController(GetWorld());
	if (Controller == nullptr || Controller->GetLookView().Valid == 0)
	{
		Clock = 0.0f;
		return;
	}
	Clock += DeltaTime;

	FLookTestArgs Args;
	ReadLookTestArgs(Args);
	switch (Stage)
	{
	case EStage::WaitView:
		if (const FScctMouseAimField* Field = ScctMouseAim::FindField(Args.Field))
		{
			ScctMouseAim::Begin();
			Original = Field->Show(ScctMouseAim::InPlay(), Args.bMerc);
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookTest: the view is up; %s %s is %s now. In play: %s"), Args.bMerc ? TEXT("merc") : TEXT("spy"), *Args.Field, *Original,
				*ScctMouseAim::Describe(Args.bMerc));
			Stage = EStage::Settle;
		}
		else
		{
			UE_LOG(LogOpenSCCT, Error, TEXT("ScctLookTest: there is no mouse and aim setting '%s'"), *Args.Field);
			Stage = EStage::Done;
		}
		break;

	case EStage::Settle:
		if (Clock >= (bAfter ? 0.5f : LookTestSettle))
		{
			BeginTurn(*Controller);
			Stage = EStage::Turn;
		}
		break;

	case EStage::Turn:
		// The counts go in as Unreal's mouse axis events, as a mouse raises them; the controller reads them in its
		// next PlayerTick and hands them to the bridge.
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, static_cast<float>(LookTestCountsPerFrame), 1));
		if (++FramesSent >= LookTestFrames)
		{
			Clock = 0.0f;
			Stage = EStage::Measure;
		}
		break;

	case EStage::Measure:
		if (Clock >= LookTestMeasureAfter)
		{
			const float Turned = TurnedSince(*Controller);
			const int32 Counts = LookTestCountsPerFrame * LookTestFrames;
			if (!bAfter)
			{
				TurnBefore = Turned;
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookTest: before: %d mouse counts turned the view %.3f deg (%.5f deg a count)"), Counts, Turned, Turned / Counts);
				Stage = EStage::Change;
			}
			else
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctLookTest: after: %d mouse counts turned the view %.3f deg (%.5f deg a count); after / before = %.3f"), Counts, Turned, Turned / Counts,
					FMath::Abs(TurnBefore) > KINDA_SMALL_NUMBER ? Turned / TurnBefore : 0.0f);
				ChangeField(Original, TEXT("put back"));
				Stage = EStage::Done;
			}
		}
		break;

	case EStage::Change:
		ChangeField(Args.Value, TEXT("through the mouse and aim section"));
		bAfter = true;
		Clock = 0.0f;
		Stage = EStage::Settle;
		break;

	default:
		break;
	}
}
