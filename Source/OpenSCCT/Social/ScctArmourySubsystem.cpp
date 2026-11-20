#include "Social/ScctArmourySubsystem.h"

#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"
#include "Social/ScctAccountSubsystem.h"
#include "Social/ScctSocialSubsystem.h"

namespace
{
	const FName ArmourySpySetting(TEXT("Display.CosmeticsSpy"));
	const FName ArmouryMercSetting(TEXT("Display.CosmeticsMerc"));
	const FName ArmouryHideMineSetting(TEXT("Display.HideMyCosmetics"));
	const FName ArmouryOwnLookSetting(TEXT("Display.OwnCosmeticLook"));
}

TWeakObjectPtr<UScctArmourySubsystem> UScctArmourySubsystem::Current;

UScctArmourySubsystem* UScctArmourySubsystem::GetCurrent()
{
	return Current.Get();
}

void UScctArmourySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The account subsystem starts the bridge's account service, which keeps the unlocks and the appearance sync.
	Collection.InitializeDependency<UScctSocialSubsystem>();
	Collection.InitializeDependency<UScctAccountSubsystem>();
	Super::Initialize(Collection);
	Current = this;

	// On the Display page (never synced with the settings profile: the account's appearance has its own sync).
	FScctSettingDesc Desc;
	Desc.Page = TEXT("Display");
	Desc.Group = NSLOCTEXT("OpenSCCT", "CosmeticsGroup", "Cosmetics");
	Desc.Type = EScctSettingType::Text;
	Desc.Default = FString();
	Desc.bVisible = false;
	Desc.Tooltip = FText::GetEmpty();
	Desc.Key = ArmourySpySetting;
	Desc.Label = NSLOCTEXT("OpenSCCT", "CosmeticsSpy", "Spy cosmetics");
	Desc.Order = 83;
	FScctSettings::Register(Desc);
	Desc.Key = ArmouryMercSetting;
	Desc.Label = NSLOCTEXT("OpenSCCT", "CosmeticsMerc", "Mercenary cosmetics");
	Desc.Order = 84;
	FScctSettings::Register(Desc);

	SettingHandle = FScctSettings::OnChanged().AddUObject(this, &UScctArmourySubsystem::HandleSettingChanged);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctArmourySubsystem::Tick));
}

void UScctArmourySubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	FScctSettings::OnChanged().Remove(SettingHandle);
	Api = nullptr;
	if (Current.Get() == this)
	{
		Current.Reset();
	}
	Super::Deinitialize();
}

bool UScctArmourySubsystem::Tick(float DeltaSeconds)
{
	if (Api == nullptr)
	{
		const UScctAccountSubsystem* Account = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UScctAccountSubsystem>() : nullptr;
		if (Account == nullptr || !Account->IsReady() || (Api = ScctFeature<FScctArmouryApi>()) == nullptr)
		{
			return true;
		}
		Api->SetChosen(0, *FScctSettings::GetString(ArmourySpySetting));
		Api->SetChosen(1, *FScctSettings::GetString(ArmouryMercSetting));
		Api->Command(static_cast<int32>(EScctArmouryCommand::HideMine), FScctSettings::GetBool(ArmouryHideMineSetting) ? 1 : 0);

		FString Forced;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctCosmetic="), Forced))
		{
			TArray<FString> Ids;
			Forced.ParseIntoArray(Ids, TEXT("+"));
			for (const FString& Id : Ids)
			{
				bool bTaken = false;
				for (int32 Index = 0; !bTaken; ++Index)
				{
					FScctArmouryItem Item = {};
					TCHAR ItemId[96];
					ItemId[0] = 0;
					if (Api->GetItem(Index, &Item, ItemId, UE_ARRAY_COUNT(ItemId), nullptr, 0, nullptr, 0) == 0)
					{
						break;
					}
					if (Id.Equals(ItemId, ESearchCase::IgnoreCase))
					{
						bTaken = Api->Choose(Item.Side, Item.Slot, ItemId, 1) != 0;
					}
				}
				UE_LOG(LogOpenSCCT, Log, TEXT("Armoury: -ScctCosmetic %s %s"), *Id, bTaken ? TEXT("put on") : TEXT("is no item of the catalogue"));
			}
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Armoury: ready (spy look %s, merc look %s)"), *GetText(EScctArmouryText::SharedLook, 0), *GetText(EScctArmouryText::SharedLook, 1));
	}

	const FScctArmouryInfo Info = GetInfo();
	if (Info.Revision != SeenRevision)
	{
		SeenRevision = Info.Revision;
		StoreChoice();
		if (Info.TitleSerial != SeenTitleSerial)
		{
			SeenTitleSerial = Info.TitleSerial;
			if (UScctAccountSubsystem* Account = GetGameInstance()->GetSubsystem<UScctAccountSubsystem>())
			{
				Account->SetTitle(GetText(EScctArmouryText::Title));
			}
		}
	}
	return true;
}

void UScctArmourySubsystem::StoreChoice()
{
	const FString Spy = GetText(EScctArmouryText::Chosen, 0);
	const FString Merc = GetText(EScctArmouryText::Chosen, 1);
	const FString Own = GetText(EScctArmouryText::OwnLook);
	const bool bOwnKnown = FScctSettings::Find(ArmouryOwnLookSetting) != nullptr;
	if (Spy == FScctSettings::GetString(ArmourySpySetting) && Merc == FScctSettings::GetString(ArmouryMercSetting) && (!bOwnKnown || Own == FScctSettings::GetString(ArmouryOwnLookSetting)))
	{
		return;
	}
	FScctSettings::SetString(ArmourySpySetting, Spy);
	FScctSettings::SetString(ArmouryMercSetting, Merc);
	// The look "Hide my cosmetics" draws on the player's own pawn (Play/ScctPawnLookSubsystem.h).
	if (bOwnKnown)
	{
		FScctSettings::SetString(ArmouryOwnLookSetting, Own);
	}
	FScctSettings::Save();
	UE_LOG(LogOpenSCCT, Log, TEXT("Armoury: chosen look saved (spy %s; merc %s)"), Spy.IsEmpty() ? TEXT("retail") : *Spy, Merc.IsEmpty() ? TEXT("retail") : *Merc);
}

void UScctArmourySubsystem::HandleSettingChanged(FName Key)
{
	if (Api != nullptr && Key == ArmouryHideMineSetting)
	{
		Api->Command(static_cast<int32>(EScctArmouryCommand::HideMine), FScctSettings::GetBool(ArmouryHideMineSetting) ? 1 : 0);
	}
}

FScctArmouryInfo UScctArmourySubsystem::GetInfo() const
{
	FScctArmouryInfo Info = {};
	if (Api != nullptr)
	{
		Api->GetInfo(&Info);
	}
	return Info;
}

FString UScctArmourySubsystem::GetText(EScctArmouryText Kind, int32 A, int32 B) const
{
	if (Api == nullptr)
	{
		return FString();
	}
	TCHAR Buffer[1024];
	Buffer[0] = 0;
	Api->GetText(static_cast<int32>(Kind), A, B, Buffer, UE_ARRAY_COUNT(Buffer));
	return FString(Buffer);
}

TArray<int32> UScctArmourySubsystem::SlotsOf(int32 Side) const
{
	TArray<int32> Slots;
	TArray<FString> Parts;
	GetText(EScctArmouryText::Slots, Side).ParseIntoArray(Parts, TEXT(","));
	for (const FString& Part : Parts)
	{
		Slots.Add(FCString::Atoi(*Part));
	}
	return Slots;
}

TArray<FScctArmouryRow> UScctArmourySubsystem::ItemsOf(int32 Side, int32 Slot) const
{
	if (Api == nullptr)
	{
		return {};
	}
	const FScctArmouryInfo Info = GetInfo();
	if (!CatalogueRevision.IsSet() || CatalogueRevision.GetValue() != Info.Revision)
	{
		Catalogue.Reset(Info.Items);
		for (int32 Index = 0; Index < Info.Items; ++Index)
		{
			FScctArmouryRow Row;
			TCHAR Id[96] = {}, Name[96] = {}, Tip[512] = {};
			if (!Api->GetItem(Index, &Row.Item, Id, UE_ARRAY_COUNT(Id), Name, UE_ARRAY_COUNT(Name), Tip, UE_ARRAY_COUNT(Tip))) { break; }
			Row.Index = Index;
			Row.Id = Id;
			Row.Name = Name;
			Row.Tip = Tip;
			Catalogue.Add(MoveTemp(Row));
		}
		CatalogueRevision = Info.Revision;
	}
	return Catalogue.FilterByPredicate([Side, Slot](const FScctArmouryRow& Row) { return Row.Item.Side == Side && Row.Item.Slot == Slot; });
}

bool UScctArmourySubsystem::Choose(int32 Side, int32 Slot, const FString& Id, bool bForce)
{
	return Api != nullptr && Api->Choose(Side, Slot, *Id, bForce ? 1 : 0) != 0;
}

void UScctArmourySubsystem::Command(EScctArmouryCommand Kind, int32 Value)
{
	if (Api != nullptr)
	{
		Api->Command(static_cast<int32>(Kind), Value);
	}
}
