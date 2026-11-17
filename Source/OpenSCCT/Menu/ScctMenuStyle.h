#pragma once

#include "CoreMinimal.h"
#include "Hud/ScctHudStyle.h"
#include "Framework/SlateDelegates.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWidget.h"

class SEditableTextBox;

/**
 * A plain Slate button the controller can reach: it takes the focus (D-pad, stick) and shows where the focus is with
 * a ring of the accent instead of Slate's dotted rectangle. OnFocused hears it take the focus (a tile shows its
 * details as it does under the pointer). Built as SNew(SScctRingButton) with SButton's arguments.
 */
class SScctRingButton : public SButton
{
public:
	virtual const FSlateBrush* GetFocusBrush() const override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;

	TFunction<void()> OnFocused;
};

// The menus' widgets, in the HUD's visual language (Hud/ScctHudStyle.h): glass panels that blur what is behind
// them, thin type, small spaced capitals for labels, cyan for the spies and amber for the mercenaries. Everything
// a menu page is made of comes from here, so the pages only say what is on them.
namespace ScctMenuStyle
{
	/** The accent of the menus themselves (no side picked): the spies' cyan. */
	FLinearColor Accent();

	/** A glass panel around Content: blurred background, dark tint, hairline edge. */
	TSharedRef<SWidget> Glass(const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(28.0f), float Tint = 0.66f);

	/** A small label in spaced capitals. */
	TSharedRef<SWidget> Label(const FString& Text, const FLinearColor& Colour = ScctHudStyle::TextDim());

	/** A line of text; Size in Slate units. */
	TSharedRef<SWidget> Text(const TAttribute<FText>& Text, float Size = 13.0f, const TAttribute<FSlateColor>& Colour = FSlateColor(ScctHudStyle::Text()),
		ScctHudStyle::EWeight Weight = ScctHudStyle::EWeight::Regular, bool bWrap = false);

	/** A page's title: large, light, spaced. */
	TSharedRef<SWidget> Title(const TAttribute<FText>& Text, float Size = 30.0f);

	/**
	 * A button. Selected (optional) lights it in Colour: the picked map, side or mode. The label may change
	 * (READY / NOT READY).
	 */
	TSharedRef<SButton> Button(const TAttribute<FText>& Label, FOnClicked OnClicked, const TAttribute<bool>& Selected = false,
		const FLinearColor& Colour = Accent(), float MinWidth = 0.0f);

	/**
	 * A one-line text field; Hint is shown while it is empty. OnCommitted hears Enter and the loss of focus,
	 * OnChanged every edit as it is typed.
	 */
	TSharedRef<SEditableTextBox> TextBox(const FString& Hint, const FString& Initial = FString(), bool bPassword = false,
		FOnTextCommitted OnCommitted = FOnTextCommitted(), FOnTextChanged OnChanged = FOnTextChanged());

	/** A hairline across a panel. */
	TSharedRef<SWidget> Rule();

	inline FText ToText(const FString& String) { return FText::FromString(String); }
}
