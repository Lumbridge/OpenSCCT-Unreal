#include "Settings/ScctProfileOptions.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctHudApi.h"
#include "Runtime/ScctLoadoutApi.h"
#include "Settings/ScctSettings.h"

namespace
{
	const FName ProfileObjectiveSoundsKey(TEXT("Audio.ObjectiveSounds"));

	/** A kit change waits until the pawn's kit has stayed the same this long, so a kit arriving item by item is logged once. */
	constexpr float ProfileKitSettleSeconds = 0.5f;

	FString ProfileLoadoutError(const FScctLoadoutApi* Api)
	{
		TCHAR Buffer[1024];
		Buffer[0] = 0;
		Api->LastError(Buffer, UE_ARRAY_COUNT(Buffer));
		FString Error(Buffer);
		int32 LineEnd = INDEX_NONE;
		if (Error.FindChar(TEXT('\n'), LineEnd))
		{
			Error.LeftInline(LineEnd);
		}
		return Error.TrimStartAndEnd();
	}

	void ProfileCopyPath(TCHAR (&Field)[FScctLoadout::PathCapacity], const FString& Text)
	{
		FCString::Strncpy(Field, *Text, FScctLoadout::PathCapacity);
	}

	FString ProfileKitText(const TCHAR (&Slots)[FScctLoadout::Slots][FScctLoadout::PathCapacity])
	{
		TArray<FString> Parts;
		for (int32 Slot = 0; Slot < FScctLoadout::Slots; ++Slot)
		{
			Parts.Add(Slots[Slot][0] != 0 ? FString(Slots[Slot]) : FString(TEXT("-")));
		}
		return FString::Join(Parts, TEXT(", "));
	}

	/** A URL yes/no option: on when present, unless its value says otherwise. */
	bool ProfileUrlSwitch(const FURL& Url, const TCHAR* Key)
	{
		const TCHAR* Value = Url.GetOption(*FString::Printf(TEXT("%s="), Key), nullptr);
		if (Value == nullptr)
		{
			return Url.HasOption(Key);
		}
		const FString Text(Value);
		return !(Text == TEXT("0") || Text.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("off"), ESearchCase::IgnoreCase)
			|| Text.Equals(TEXT("no"), ESearchCase::IgnoreCase));
	}
}

bool UScctProfileOptions::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

FString UScctProfileOptions::GetLoadoutFile()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Controls") / TEXT("loadout.json"));
}

void UScctProfileOptions::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>();

	{
		FScctSettingDesc Desc;
		Desc.Key = ProfileObjectiveSoundsKey;
		Desc.Page = TEXT("Audio");
		Desc.Group = FText::FromString(TEXT("Game"));
		Desc.Label = FText::FromString(TEXT("Objective sounds"));
		Desc.Tooltip = FText::FromString(TEXT("The short music stings when an objective is reached, lost or changes hands."));
		Desc.Type = EScctSettingType::Bool;
		Desc.Default = TEXT("true");
		Desc.Order = 100;
		FScctSettings::Register(Desc);
	}
	SettingChangedHandle = FScctSettings::OnChanged().AddUObject(this, &UScctProfileOptions::OnSettingChanged);

	if (Session != nullptr)
	{
		FrameHandle = Session->OnFrame.AddUObject(this, &UScctProfileOptions::OnFrame);
		SessionChangedHandle = Session->OnSessionChanged.AddUObject(this, &UScctProfileOptions::OnSessionChanged);
	}

	// The saved kit is read once a run; the bridge keeps it from one level to the next.
	static bool bLoaded = false;
	const FScctLoadoutApi* Api = ScctFeature<FScctLoadoutApi>();
	if (!bLoaded && Api != nullptr)
	{
		bLoaded = true;
		const FString File = GetLoadoutFile();
		const int32 Read = Api->Load(*File);
		FScctLoadout Kit = {};
		Api->GetLoadout(&Kit);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: %s %s: spy [%s], merc [%s] with %s and %s"),
			Read == 1 ? TEXT("read") : Read == 2 ? TEXT("no saved kit yet, the fixed kits from") : TEXT("could not read"), *File,
			*ProfileKitText(Kit.SpyGadgets), *ProfileKitText(Kit.MercGadgets), Kit.MercWeapon, Kit.MercSidearm);
		if (Read == 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctLoadout: %s"), *ProfileLoadoutError(Api));
		}

		FString Forced;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctLoadout="), Forced, false))
		{
			ApplyLoadoutText(Forced);
		}
	}

	FString GearChange;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctGearChange="), GearChange, false))
	{
		FString Seconds = GearChange;
		GearChange.Split(TEXT(","), &Seconds, &GearChangeKit);
		GearChangeAt = FMath::Max(FCString::Atof(*Seconds), 0.0f);
	}
	PushOptions();
}

void UScctProfileOptions::Deinitialize()
{
	FScctSettings::OnChanged().Remove(SettingChangedHandle);
	if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->OnFrame.Remove(FrameHandle);
		Session->OnSessionChanged.Remove(SessionChangedHandle);
	}
	Super::Deinitialize();
}

void UScctProfileOptions::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Before the game mode starts the match in its StartPlay: help paths take DisplayHelpInGame as the level loads.
	bOffline = !ProfileUrlSwitch(InWorld.URL, UScctOnlineSubsystem::OnlineOption);
	bHelpInGame = ProfileUrlSwitch(InWorld.URL, TEXT("Help")) || FParse::Param(FCommandLine::Get(), TEXT("ScctHelpInGame"));
	PushOptions();
}

void UScctProfileOptions::PushOptions()
{
	const FScctLoadoutApi* Api = ScctFeature<FScctLoadoutApi>();
	if (Api == nullptr)
	{
		return;
	}

	FScctProfileOptions Options = {};
	Options.ObjectiveSounds = FScctSettings::GetBool(ProfileObjectiveSoundsKey) ? 1 : 0;
	// The wheel's job is the controls' (UScctControlSettings::bMouseWheelSpeed): the bridge's controls hold it and
	// the profile follows them, so the Spy and Mercenary pages' wheel setting reaches the simulation at once.
	Options.SelectedMouse = -1;
	Options.DisplayHelpInGame = bHelpInGame ? 1 : 0;
	Options.OfflineSession = bOffline ? 1 : 0;
	Api->SetProfileOptions(&Options);
}

void UScctProfileOptions::OnSettingChanged(FName Key)
{
	if (Key == ProfileObjectiveSoundsKey)
	{
		PushOptions();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: objective sounds %s"), FScctSettings::GetBool(Key) ? TEXT("on") : TEXT("off"));
	}
}

void UScctProfileOptions::OnSessionChanged(int32 OldId, int32 NewId)
{
	LoggedKit.Reset();
	SettlingKit.Reset();
	SettlingFor = 0.0f;
	MatchSeconds = 0.0f;
	bLoggedOptions = false;
	LoggedTipId = 0;
	if (NewId != 0)
	{
		// The session says for certain which kind it is; the URL said what was meant.
		const UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>();
		bOffline = Session == nullptr || !Session->IsRemoteSession();
		PushOptions();
	}
}

bool UScctProfileOptions::ApplyLoadoutText(const FString& Text)
{
	const FScctLoadoutApi* Api = ScctFeature<FScctLoadoutApi>();
	if (Api == nullptr)
	{
		return false;
	}

	FScctLoadout Kit = {};
	Api->GetLoadout(&Kit);
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(";"));
	for (const FString& Part : Parts)
	{
		FString Name, Value;
		if (!Part.Split(TEXT(":"), &Name, &Value))
		{
			continue;
		}
		Name.TrimStartAndEndInline();
		TArray<FString> Paths;
		Value.ParseIntoArray(Paths, TEXT(","), false);
		if (Name.Equals(TEXT("spy"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("merc"), ESearchCase::IgnoreCase))
		{
			TCHAR (&Slots)[FScctLoadout::Slots][FScctLoadout::PathCapacity] =
				Name.Equals(TEXT("spy"), ESearchCase::IgnoreCase) ? Kit.SpyGadgets : Kit.MercGadgets;
			for (int32 Slot = 0; Slot < FScctLoadout::Slots; ++Slot)
			{
				ProfileCopyPath(Slots[Slot], Paths.IsValidIndex(Slot) ? Paths[Slot].TrimStartAndEnd() : FString());
			}
		}
		else if (Name.Equals(TEXT("weapon"), ESearchCase::IgnoreCase))
		{
			ProfileCopyPath(Kit.MercWeapon, Value.TrimStartAndEnd());
		}
		else if (Name.Equals(TEXT("sidearm"), ESearchCase::IgnoreCase))
		{
			ProfileCopyPath(Kit.MercSidearm, Value.TrimStartAndEnd());
		}
	}

	if (Api->SetLoadout(&Kit) == 0 || Api->Save(nullptr) == 0)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctLoadout: the kit '%s' was refused: %s"), *Text, *ProfileLoadoutError(Api));
		return false;
	}

	// What the bridge kept (it cleans a kit as it does a read file).
	Api->GetLoadout(&Kit);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: saved kit: spy [%s], merc [%s] with %s and %s"),
		*ProfileKitText(Kit.SpyGadgets), *ProfileKitText(Kit.MercGadgets), Kit.MercWeapon, Kit.MercSidearm);
	return true;
}

void UScctProfileOptions::OnFrame(float DeltaSeconds)
{
	const FScctLoadoutApi* Api = ScctFeature<FScctLoadoutApi>();
	if (Api == nullptr)
	{
		return;
	}
	MatchSeconds += DeltaSeconds;

	// The server's answers (the HUD line is already up; this is for the log and a picker that waits on it).
	FScctGearAnswer Answer;
	while (Api->PollGearAnswer(&Answer) == 1)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: the server answered the gear change: %s (result %d, wait %.1f s)"),
			Answer.Text, Answer.Result, Answer.WaitSeconds);
	}

	// The kit the local pawn carries, once it has settled after a spawn or a change.
	TCHAR Buffer[512];
	Buffer[0] = 0;
	Api->GetLocalInventory(Buffer, UE_ARRAY_COUNT(Buffer));
	const FString Kit(Buffer);
	if (Kit != SettlingKit)
	{
		SettlingKit = Kit;
		SettlingFor = 0.0f;
	}
	else
	{
		SettlingFor += DeltaSeconds;
	}
	if (!Kit.IsEmpty() && Kit != LoggedKit && SettlingFor >= ProfileKitSettleSeconds)
	{
		LoggedKit = Kit;
		FString Side, Items;
		Kit.Split(TEXT(" "), &Side, &Items);
		int32 SpyReads = 0, MercReads = 0;
		Api->GetKitRequests(&SpyReads, &MercReads);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: installed %s kit [%s] (the saved kit was read %d times for the spy, %d for the merc)"),
			*Side, *Items, SpyReads, MercReads);
		if (!bLoggedOptions)
		{
			bLoggedOptions = true;
			FScctProfileOptions Read = {};
			Api->GetProfileOptions(&Read);
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: the simulation reads ObjectiveSounds=%s SelectedMouse=%d DisplayHelpInGame=%s OfflineSession=%s"),
				Read.ObjectiveSounds != 0 ? TEXT("true") : TEXT("false"), Read.SelectedMouse,
				Read.DisplayHelpInGame != 0 ? TEXT("true") : TEXT("false"), Read.OfflineSession != 0 ? TEXT("true") : TEXT("false"));
		}
	}

	// The zone tip the simulation has up (SZoneTips shows them only in an offline session): logged as it changes,
	// so the offline flag can be checked unattended.
	if (const FScctHudApi* Hud = ScctFeature<FScctHudApi>())
	{
		FScctHudState State = {};
		const int32 TipId = Hud->GetState(&State) != 0 && State.Valid != 0 ? State.TutorialId : 0;
		if (TipId != LoggedTipId)
		{
			LoggedTipId = TipId;
			TCHAR Tip[512];
			Tip[0] = 0;
			if (TipId != 0)
			{
				Hud->GetText(TipId, Tip, UE_ARRAY_COUNT(Tip));
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: zone tip %s%s%s"), TipId != 0 ? TEXT("shown: \"") : TEXT("cleared"), Tip, TipId != 0 ? TEXT("\"") : TEXT(""));
		}
	}

	// -ScctGearChange: the gear picker's OK, unattended.
	if (GearChangeAt >= 0.0f && MatchSeconds >= GearChangeAt && !LoggedKit.IsEmpty())
	{
		GearChangeAt = -1.0f;
		if (!GearChangeKit.IsEmpty())
		{
			ApplyLoadoutText(GearChangeKit);
		}
		const int32 Asked = Api->RequestGearChange();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadout: -ScctGearChange asked the server for the saved kit: %s"),
			Asked != 0 ? TEXT("sent") : *ProfileLoadoutError(Api));
	}
}
