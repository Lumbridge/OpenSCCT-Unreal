#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

template<typename OptionType> class SComboBox;

/** A compact select box for choices outside the full-width settings rows. */
class SScctSelectBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctSelectBox) {}
		SLATE_ARGUMENT(TArray<FString>, Options)
		SLATE_ARGUMENT(TFunction<int32()>, Get)
		SLATE_ARGUMENT(TFunction<void(int32)>, Set)
		SLATE_ARGUMENT(FString, Prefix)
		SLATE_ARGUMENT(FText, FieldLabel)
		SLATE_ARGUMENT(FText, EmptyText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	bool IsOpen() const;

private:
	using FItem = TSharedPtr<int32>;
	void SyncSelection();

	TArray<FString> Options;
	TArray<FItem> Items;
	TFunction<int32()> Get;
	TSharedPtr<SComboBox<FItem>> Combo;
	bool bSyncingSelection = false;
};
