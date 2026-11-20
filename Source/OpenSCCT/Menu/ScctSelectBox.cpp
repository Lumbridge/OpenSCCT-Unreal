#include "Menu/ScctSelectBox.h"

#include "Menu/ScctMenuStyle.h"
#include "Templates/UnrealTemplate.h"
#include "Widgets/Input/SComboBox.h"

void SScctSelectBox::Construct(const FArguments& InArgs)
{
	Options = InArgs._Options;
	Get = InArgs._Get;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		Items.Add(MakeShared<int32>(Index));
	}

	const int32 Initial = Get ? Get() : INDEX_NONE;
	ChildSlot
	[
		SAssignNew(Combo, SComboBox<FItem>)
		.OptionsSource(&Items)
		.AccessibleText(InArgs._FieldLabel)
		.InitiallySelectedItem(Items.IsValidIndex(Initial) ? Items[Initial] : FItem())
		.EnableGamepadNavigationMode(true)
		.MaxListHeight(300.0f)
		.IsEnabled(Items.Num() > 1)
		.OnComboBoxOpening(FSimpleDelegate::CreateSP(this, &SScctSelectBox::SyncSelection))
		.OnGenerateWidget_Lambda([this](FItem Choice) -> TSharedRef<SWidget>
		{
			return ScctMenuStyle::Text(FText::FromString(Options[*Choice]), 12.5f);
		})
		.OnSelectionChanged_Lambda([this, Set = InArgs._Set](FItem Choice, ESelectInfo::Type)
		{
			if (!bSyncingSelection && Choice.IsValid() && Set && (!Get || *Choice != Get()))
			{
				Set(*Choice);
			}
		})
		[
			ScctMenuStyle::Text(TAttribute<FText>::CreateLambda([this, Prefix = InArgs._Prefix, Empty = InArgs._EmptyText]
			{
				const int32 Index = Get ? Get() : INDEX_NONE;
				return Options.IsValidIndex(Index) ? FText::FromString(Prefix + Options[Index]) : Empty;
			}), 12.5f)
		]
	];
}

bool SScctSelectBox::IsOpen() const
{
	return Combo.IsValid() && Combo->IsOpen();
}

void SScctSelectBox::SyncSelection()
{
	const TGuardValue<bool> Syncing(bSyncingSelection, true);
	const int32 Index = Get ? Get() : INDEX_NONE;
	Combo->SetSelectedItem(Items.IsValidIndex(Index) ? Items[Index] : FItem());
}
