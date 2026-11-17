#include "Menu/ScctMenuSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Menu/SScctGearPage.h"
#include "Menu/ScctMatchMenuWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctLoadoutApi.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** "spy" or "merc" as a side; anything else is "the side played". */
	int32 MenuSideOf(const FString& Text)
	{
		return Text.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : Text.Equals(TEXT("spy"), ESearchCase::IgnoreCase) ? 0 : -1;
	}

	/** -ScctGear / -ScctGearApply acted on in a match of this run. */
	bool bGearDoneThisRun = false;
}

bool UScctMenuSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UScctMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>())
	{
		FrameHandle = Session->OnFrame.AddUObject(this, &UScctMenuSubsystem::OnFrame);
	}

	// Once a run (OnFrame marks it done): a match that follows does not open it again.
	if (!bGearDoneThisRun)
	{
		FString Side;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctGearApply="), GearScript, false))
		{
			bGearPending = true;
			FString First;
			GearScript.Split(TEXT(","), &First, nullptr);
			GearSide = MenuSideOf(First.IsEmpty() ? GearScript : First);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("ScctGear")) || FParse::Value(FCommandLine::Get(), TEXT("ScctGear="), Side))
		{
			bGearPending = true;
			GearSide = MenuSideOf(Side);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("ScctGearKey")))
		{
			bGearPending = true;
			bGearByCommand = true;
		}
		FParse::Value(FCommandLine::Get(), TEXT("ScctGearAt="), GearDelay);
	}
}

void UScctMenuSubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->OnFrame.Remove(FrameHandle);
	}
	if (CommandSource.IsValid())
	{
		CommandSource->OnLocalCommand.Remove(CommandHandle);
	}
	Super::Deinitialize();
}

void UScctMenuSubsystem::OnFrame(float DeltaSeconds)
{
	// The bindings' commands come from the local controller, which is there by the first frame of a match.
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player != nullptr && CommandSource.Get() != Player)
	{
		if (CommandSource.IsValid())
		{
			CommandSource->OnLocalCommand.Remove(CommandHandle);
		}
		CommandSource = Player;
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctMenuSubsystem::HandleLocalCommand);
	}

	if (!bGearPending)
	{
		return;
	}
	// Wait for the kit to be on the pawn (a spawn has happened), then the delay asked for.
	const FScctLoadoutApi* Loadout = ScctFeature<FScctLoadoutApi>();
	TCHAR Kit[512];
	Kit[0] = 0;
	if (Loadout != nullptr)
	{
		Loadout->GetLocalInventory(Kit, UE_ARRAY_COUNT(Kit));
	}
	KitSeconds = Kit[0] != 0 ? KitSeconds + DeltaSeconds : 0.0f;
	if (KitSeconds < GearDelay)
	{
		return;
	}

	bGearPending = false;
	bGearDoneThisRun = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: the kit before: %s"), Kit);
	if (bGearByCommand && Player != nullptr)
	{
		// As the binding raises it: through the controller's interface commands, to every listener.
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: -ScctGearKey raises the ChangeGear command"));
		Player->OnLocalCommand.Broadcast(TEXT("ChangeGear"), 0, true);
		Player->OnLocalCommand.Broadcast(TEXT("ChangeGear"), 0, false);
		return;
	}
	if (!OpenGear(GearSide))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctGear: -ScctGear could not open the Equipment page in this level"));
		return;
	}
	if (!GearScript.IsEmpty())
	{
		for (TObjectIterator<UScctMatchMenuWidget> It; It; ++It)
		{
			if (It->GetWorld() == GetWorld())
			{
				if (const TSharedPtr<SScctGearPage> Page = It->GetGearPage())
				{
					Page->RunScript(GearScript);
				}
				break;
			}
		}
	}
}

void UScctMenuSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	if (bPressed && Name == TEXT("ChangeGear"))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: ChangeGear pressed"));
		OpenGear(-1);
	}
}

bool UScctMenuSubsystem::OpenGear(int32 Side)
{
	// The in-match menu is the game mode's widget in this world's viewport.
	for (TObjectIterator<UScctMatchMenuWidget> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->IsInViewport())
		{
			return It->ShowGear(Side);
		}
	}
	return false;
}
