#pragma once

#include "CoreMinimal.h"
#include "Online/ScctHostSetup.h"
#include "Widgets/SWidget.h"

/**
 * The rows of a match setup as Create Session and the lobby's Session Options draw them (the Godot client's
 * SessionRowsView): a number with - and +, a check as YES/NO, a choice as a row of buttons under its caption, and the
 * Alternate rules page. Editable rows send their change ("TimeLimit 12", "Revenge 1", "Mode 2") to OnEdit, which asks
 * the bridge's model (ScctHostSetup::Text Edit / EditAlternate); read-only rows show the same values as text, for a
 * player who may not change the setup.
 */
namespace ScctSetupRowsView
{
	using FOnEdit = TFunction<void(const FString& /*Change*/)>;

	/** One row, or nothing when Row is null or hidden in this mode. */
	TSharedRef<SWidget> Row(const FScctSetupRow* Row, const FOnEdit& OnEdit, bool bEditable);

	/** The Alternate rules: each side's abilities, gadgets and gadget count in two columns, the cinematic camera under them. */
	TSharedRef<SWidget> Alternate(const TArray<FScctAlternateRow>& Rows, const FOnEdit& OnEditAlternate, bool bEditable);

	/** A group's heading in the accent. */
	TSharedRef<SWidget> Heading(const FString& Text);

	/** A dim note under rows. */
	TSharedRef<SWidget> Note(const TAttribute<FText>& Text);
}
