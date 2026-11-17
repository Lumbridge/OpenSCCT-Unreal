#pragma once

#include "CoreMinimal.h"

class SButton;
class SWidget;
class UUserWidget;
class UWorld;

// Driving the game's own menus from a named test (Test/ScctTest.h), the way a player does: a button is found by the
// label the player reads on it and pressed only while it is shown and enabled, so a test that goes through the
// menus fails when a page does not offer what a player would click (the Godot client's MenuPilot).
namespace ScctTestUi
{
	/**
	 * The first enabled button under Root whose label is Label (case ignored), searching only what is shown: a
	 * collapsed or hidden branch is skipped, as a player cannot click into it. Null when none is.
	 */
	OPENSCCT_API TSharedPtr<SButton> FindButton(const TSharedRef<SWidget>& Root, const FString& Label);

	/** Every label of the shown, enabled buttons under Root, for a failure's reason ("RESUME, MAIN MENU"). */
	OPENSCCT_API TArray<FString> ListButtons(const TSharedRef<SWidget>& Root);

	/**
	 * Presses the shown button labelled Label in Widget, as a click on it does (SButton::SimulateClick). False,
	 * with the labels on offer in OutError, when the widget shows no such button (or has no Slate widget yet).
	 * Not available in a Shipping build, where it always fails.
	 */
	OPENSCCT_API bool PressButton(UUserWidget* Widget, const FString& Label, FString* OutError = nullptr);

	/** The first widget of class T that belongs to World and is in its viewport (a menu the player sees), or null. */
	template <typename T>
	T* FindShownWidget(const UWorld* World);
}

#include "Blueprint/UserWidget.h"
#include "UObject/UObjectIterator.h"

template <typename T>
T* ScctTestUi::FindShownWidget(const UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	for (TObjectIterator<T> It; It; ++It)
	{
		T* Widget = *It;
		if (IsValid(Widget) && !Widget->IsTemplate() && Widget->GetWorld() == World && Widget->IsInViewport())
		{
			return Widget;
		}
	}
	return nullptr;
}
