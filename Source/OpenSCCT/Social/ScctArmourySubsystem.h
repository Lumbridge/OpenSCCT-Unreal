#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Runtime/ScctArmouryApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctArmourySubsystem.generated.h"

struct FScctArmouryRow
{
	FScctArmouryItem Item = {};
	int32 Index = 0;
	FString Id;
	FString Name;
	FString Tip;
	bool IsUnlocked() const { return (Item.Flags & EScctArmouryItemFlags::Unlocked) != 0; }
	bool IsChosen() const { return (Item.Flags & EScctArmouryItemFlags::Chosen) != 0; }
};

/**
 * The Armoury for the whole run over Runtime/ScctArmouryApi.h (the Godot client's cosmetics settings and the model of
 * its ArmouryScreen): keeps the chosen cosmetics of each side in the settings registry ("Display.CosmeticsSpy" and
 * "Display.CosmeticsMerc", hidden: ids joined by commas) and hands them to the bridge at start, writes them back
 * whenever the bridge's choice changed (a pick on the page, the account's appearance taken), keeps the own look that
 * "Hide my cosmetics" draws (Display.OwnCosmeticLook) in step, and stores the title the account's appearance names.
 * The bridge sends the look to the servers (each join, and a change while in a lobby or a hub).
 *
 * Unattended: -ScctCosmetic=<id>[+<id>...] puts those items on at start whatever the account unlocked (the servers
 * still check the look against the account).
 */
UCLASS()
class OPENSCCT_API UScctArmourySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UScctArmourySubsystem* GetCurrent();

	bool IsReady() const { return Api != nullptr; }
	FScctArmouryInfo GetInfo() const;
	bool HasFlag(int32 Flag) const { return (GetInfo().Flags & Flag) != 0; }
	FString GetText(EScctArmouryText Kind, int32 A = 0, int32 B = 0) const;

	/** The slots of a side (0 spy, 1 merc) in the page's order. */
	TArray<int32> SlotsOf(int32 Side) const;

	/** The catalogue's items of a slot of a side. */
	TArray<FScctArmouryRow> ItemsOf(int32 Side, int32 Slot) const;

	/** Puts an item (empty: retail) on a slot. False for a locked item. */
	bool Choose(int32 Side, int32 Slot, const FString& Id, bool bForce = false);

	void Command(EScctArmouryCommand Kind, int32 Value = 0);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaSeconds);
	void HandleSettingChanged(FName Key);
	void StoreChoice();

	static TWeakObjectPtr<UScctArmourySubsystem> Current;

	const FScctArmouryApi* Api = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle SettingHandle;
	int32 SeenRevision = MIN_int32;
	int32 SeenTitleSerial = 0;
};
