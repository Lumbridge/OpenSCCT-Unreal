#include "Menu/ScctMatchCommands.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Menu/SScctGearPage.h"
#include "Menu/SScctStrategicMapPage.h"
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

	/** -ScctStrategicMap acted on in a match of this run. */
	bool bMapDoneThisRun = false;
}

bool UScctMatchCommandsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UScctMatchCommandsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>())
	{
		FrameHandle = Session->OnFrame.AddUObject(this, &UScctMatchCommandsSubsystem::OnFrame);
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
	if (!bMapDoneThisRun)
	{
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctStrategicMap="), MapOpenAt) || FParse::Param(FCommandLine::Get(), TEXT("ScctStrategicMap")))
		{
			MapOpenAt = MapOpenAt >= 0.0f ? MapOpenAt : 3.0f;
		}
		FParse::Value(FCommandLine::Get(), TEXT("ScctStrategicMapFloorAt="), MapFloorAt);
		FParse::Value(FCommandLine::Get(), TEXT("ScctStrategicMapCloseAt="), MapCloseAt);
	}
}

void UScctMatchCommandsSubsystem::Deinitialize()
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

void UScctMatchCommandsSubsystem::OnFrame(float DeltaSeconds)
{
	if (const UScctSessionSubsystem* BackdropSession = GetWorld()->GetSubsystem<UScctSessionSubsystem>(); BackdropSession != nullptr && BackdropSession->IsBackdrop())
	{
		// The match behind the main menu: nobody at this machine plays it.
		return;
	}
	// The bindings' commands come from the local controller, which is there by the first frame of a match.
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player != nullptr && CommandSource.Get() != Player)
	{
		if (CommandSource.IsValid())
		{
			CommandSource->OnLocalCommand.Remove(CommandHandle);
		}
		CommandSource = Player;
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctMatchCommandsSubsystem::HandleLocalCommand);
	}

	TickStrategicMap(Player, DeltaSeconds);

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

void UScctMatchCommandsSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	if (bPressed && Name == TEXT("ChangeGear"))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: ChangeGear pressed"));
		OpenGear(-1);
	}
	else if (bPressed && Name == TEXT("DisplayStrategicMap"))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: DisplayStrategicMap pressed"));
		OpenStrategicMap();
	}
}

UScctMatchMenuWidget* UScctMatchCommandsSubsystem::FindMatchMenu() const
{
	// The in-match menu is the game mode's widget in this world's viewport.
	for (TObjectIterator<UScctMatchMenuWidget> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->IsInViewport())
		{
			return *It;
		}
	}
	return nullptr;
}

bool UScctMatchCommandsSubsystem::OpenGear(int32 Side)
{
	UScctMatchMenuWidget* Menu = FindMatchMenu();
	return Menu != nullptr && Menu->ShowGear(Side);
}

bool UScctMatchCommandsSubsystem::OpenStrategicMap()
{
	// SPlayerController.DisplayStrategicMap: not on a tutorial map, not while working an object.
	const FScctMapApi* Api = ScctFeature<FScctMapApi>();
	if (Api == nullptr || Api->CanDisplay() == 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: the strategic map cannot open now (%s)"),
			Api == nullptr ? TEXT("the bridge has no map table") : TEXT("the player's state does not allow it"));
		return false;
	}
	UScctMatchMenuWidget* Menu = FindMatchMenu();
	return Menu != nullptr && Menu->ShowStrategicMap();
}

void UScctMatchCommandsSubsystem::TickStrategicMap(AScctPlayerController* Player, float DeltaSeconds)
{
	UScctMatchMenuWidget* Menu = FindMatchMenu();
	const TSharedPtr<SScctStrategicMapPage> Page = Menu != nullptr ? Menu->GetStrategicMapPage() : nullptr;
	const FScctMapApi* Api = ScctFeature<FScctMapApi>();

	// -ScctStrategicMap: once the key may open it, wait the time asked for, then raise the command as the binding does.
	if (MapOpenAt >= 0.0f && Player != nullptr && Api != nullptr)
	{
		MapAllowedSeconds = Api->CanDisplay() != 0 ? MapAllowedSeconds + DeltaSeconds : 0.0f;
		if (MapAllowedSeconds >= MapOpenAt)
		{
			MapOpenAt = -1.0f;
			bMapDoneThisRun = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: -ScctStrategicMap raises the DisplayStrategicMap command"));
			Player->OnLocalCommand.Broadcast(TEXT("DisplayStrategicMap"), 0, true);
			Player->OnLocalCommand.Broadcast(TEXT("DisplayStrategicMap"), 0, false);
		}
	}

	if (!Page.IsValid())
	{
		bMapWasOpen = false;
		return;
	}
	if (!bMapWasOpen)
	{
		// Just opened: the press that opened it must be let go before the same key closes it.
		bMapWasOpen = true;
		bMapKeysArmed = false;
		MapOpenSeconds = 0.0f;
		MapLogTimer = 0.0f;
		MapKeys = Player != nullptr ? Player->FindKeysBoundTo(TEXT("DisplayStrategicMap")) : TArray<FKey>();
	}
	MapOpenSeconds += DeltaSeconds;

	// StrategicMap_InGame: its exit key is the key that opened it.
	bool bAnyDown = false;
	bool bAnyPressed = false;
	for (const FKey& Key : MapKeys)
	{
		bAnyDown |= Player != nullptr && Player->IsInputKeyDown(Key);
		bAnyPressed |= Player != nullptr && Player->WasInputKeyJustPressed(Key);
	}
	const bool bCloseAsked = MapCloseAt >= 0.0f && MapOpenSeconds >= MapCloseAt;
	if ((bMapKeysArmed && bAnyPressed) || bCloseAsked)
	{
		if (bCloseAsked)
		{
			MapCloseAt = -1.0f;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: -ScctStrategicMapCloseAt closes it as its key does"));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: its key closes it"));
		Page->Close();
		return;
	}
	if (!bAnyDown && !bAnyPressed)
	{
		bMapKeysArmed = true;
	}

	if (MapFloorAt >= 0.0f && MapOpenSeconds >= MapFloorAt)
	{
		MapFloorAt = -1.0f;
		const FScctMapState& State = Page->GetState();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: -ScctStrategicMapFloorAt presses %s"), State.Floor < State.FloorCount - 1 ? TEXT("UP") : TEXT("DOWN"));
		if (State.Floor < State.FloorCount - 1)
		{
			Page->FloorUp();
		}
		else
		{
			Page->FloorDown();
		}
	}

	// The marker against where the player stands, every 2 s while it is open.
	MapLogTimer -= DeltaSeconds;
	if (MapLogTimer <= 0.0f)
	{
		MapLogTimer = 2.0f;
		const APawn* Pawn = Player != nullptr ? Player->GetPawn() : nullptr;
		const FVector At = Pawn != nullptr ? Pawn->GetActorLocation() : FVector::ZeroVector;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: %s; Unreal pawn at (%.0f, %.0f, %.0f)%s"), *Page->Describe(), At.X, At.Y, At.Z,
			Pawn != nullptr ? TEXT("") : TEXT(" (no pawn)"));
	}
}
