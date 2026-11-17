#include "Play/ScctEquipmentData.h"

#include "OpenSCCT.h"

const FScctWeaponPresentation* UScctWeaponSet::Find(FName SimulationClass) const
{
	return Weapons.FindByPredicate([SimulationClass](const FScctWeaponPresentation& Row) { return Row.SimulationClass == SimulationClass; });
}

const UScctWeaponSet* UScctWeaponSet::Get()
{
	// Kept for the run: the asset is small and every match asks for it.
	static TWeakObjectPtr<const UScctWeaponSet> Kept;
	static bool bAsked = false;
	if (!Kept.IsValid() && !bAsked)
	{
		bAsked = true;
		UScctWeaponSet* Loaded = LoadObject<UScctWeaponSet>(nullptr, TEXT("/Game/OpenSCCT/Weapons/DA_ScctWeapons.DA_ScctWeapons"), nullptr, LOAD_NoWarn);
		if (Loaded != nullptr)
		{
			Loaded->AddToRoot();
			Kept = Loaded;
			UE_LOG(LogOpenSCCT, Log, TEXT("Weapon table: %d rows"), Loaded->Weapons.Num());
		}
		else
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("No weapon table /Game/OpenSCCT/Weapons/DA_ScctWeapons (Tools/Import/Import-Characters.ps1 builds it): every weapon is drawn with the model the simulation names"));
		}
	}
	return Kept.Get();
}
