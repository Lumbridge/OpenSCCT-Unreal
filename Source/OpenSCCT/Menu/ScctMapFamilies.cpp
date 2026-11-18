#include "Menu/ScctMapFamilies.h"

#include "Menu/ScctMenuSubsystem.h"

TArray<FScctMapFamily> ScctMapFamilies::Build(const TArray<FScctMenuMapInfo>& Maps)
{
	UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
	TArray<FScctMapFamily> Families;
	TMap<FString, int32> ByName;
	for (int32 Index = 0; Index < Maps.Num(); ++Index)
	{
		const FScctMenuMapInfo& Map = Maps[Index];
		FString Title = Menu ? Menu->GetMapText(Map.Id, EScctMenuMapText::Family) : FString();
		if (Title.IsEmpty())
		{
			Title = Map.Title;
		}
		const int32 Family = ByName.FindOrAdd(Title.ToLower(), Families.Num());
		if (Family == Families.Num())
		{
			Families.Add({ MoveTemp(Title), {} });
		}
		FString Label = Menu ? Menu->GetMapText(Map.Id, EScctMenuMapText::Variant) : FString();
		Families[Family].Variants.Add({ Index, MoveTemp(Label) });
	}

	for (FScctMapFamily& Family : Families)
	{
		if (Family.Variants.Num() == 1)
		{
			Family.Title = Maps[Family.Variants[0].MapIndex].Title;
			continue;
		}
		// Empty (base) first, then editions in name order. Some packs contain two originals both called Station.
		Family.Variants.StableSort([](const FScctMapVariant& A, const FScctMapVariant& B) { return A.Label < B.Label; });
		TMap<FString, int32> Counts;
		for (const FScctMapVariant& Variant : Family.Variants)
		{
			++Counts.FindOrAdd(Variant.Label.ToLower());
		}
		for (FScctMapVariant& Variant : Family.Variants)
		{
			const bool bDuplicate = Counts.FindChecked(Variant.Label.ToLower()) > 1;
			if (Variant.Label.IsEmpty())
			{
				Variant.Label = TEXT("Original");
			}
			if (bDuplicate)
			{
				Variant.Label += FString::Printf(TEXT(" (%s)"), *Maps[Variant.MapIndex].Id);
			}
		}
	}
	Families.StableSort([](const FScctMapFamily& A, const FScctMapFamily& B) { return A.Title < B.Title; });
	return Families;
}
