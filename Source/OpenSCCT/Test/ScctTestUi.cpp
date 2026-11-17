#include "Test/ScctTestUi.h"

#include "Layout/Children.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FName ButtonType(TEXT("SButton"));
	const FName TextType(TEXT("STextBlock"));

	bool IsShown(const SWidget& Widget)
	{
		return Widget.GetVisibility().IsVisible();
	}

	/** The text a player reads on a button: every shown text block inside it, joined by spaces. */
	void CollectLabel(const TSharedRef<SWidget>& Widget, TArray<FString>& Parts)
	{
		if (!IsShown(*Widget))
		{
			return;
		}
		if (Widget->GetType() == TextType)
		{
			const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().TrimStartAndEnd();
			if (!Text.IsEmpty())
			{
				Parts.Add(Text);
			}
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			CollectLabel(Children->GetChildAt(Index), Parts);
		}
	}

	/** Calls Visit on every shown, enabled button under Widget with its label; stops when Visit returns true. */
	bool VisitButtons(const TSharedRef<SWidget>& Widget, const TFunctionRef<bool(const TSharedRef<SButton>&, const FString&)>& Visit)
	{
		if (!IsShown(*Widget))
		{
			return false;
		}
		if (Widget->GetType() == ButtonType)
		{
			if (!Widget->IsEnabled())
			{
				return false;
			}
			TArray<FString> Parts;
			CollectLabel(Widget, Parts);
			return Visit(StaticCastSharedRef<SButton>(Widget), FString::Join(Parts, TEXT(" ")));
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			if (VisitButtons(Children->GetChildAt(Index), Visit))
			{
				return true;
			}
		}
		return false;
	}
}

TSharedPtr<SButton> ScctTestUi::FindButton(const TSharedRef<SWidget>& Root, const FString& Label)
{
	TSharedPtr<SButton> Found;
	VisitButtons(Root, [&](const TSharedRef<SButton>& Button, const FString& Text)
	{
		if (Text.Equals(Label, ESearchCase::IgnoreCase))
		{
			Found = Button;
			return true;
		}
		return false;
	});
	return Found;
}

TArray<FString> ScctTestUi::ListButtons(const TSharedRef<SWidget>& Root)
{
	TArray<FString> Labels;
	VisitButtons(Root, [&](const TSharedRef<SButton>&, const FString& Text)
	{
		Labels.Add(Text.IsEmpty() ? TEXT("(no label)") : Text);
		return false;
	});
	return Labels;
}

bool ScctTestUi::PressButton(UUserWidget* Widget, const FString& Label, FString* OutError)
{
	const TSharedPtr<SWidget> Root = IsValid(Widget) ? Widget->GetCachedWidget() : nullptr;
	if (!Root.IsValid())
	{
		if (OutError != nullptr)
		{
			*OutError = FString::Printf(TEXT("there is no menu on the screen to press %s in"), *Label);
		}
		return false;
	}

	const TSharedPtr<SButton> Button = FindButton(Root.ToSharedRef(), Label);
	if (!Button.IsValid())
	{
		if (OutError != nullptr)
		{
			*OutError = FString::Printf(TEXT("%s shows no button %s (it shows: %s)"), *Widget->GetClass()->GetName(), *Label,
				*FString::Join(ListButtons(Root.ToSharedRef()), TEXT(", ")));
		}
		return false;
	}

#if UE_BUILD_SHIPPING
	if (OutError != nullptr)
	{
		*OutError = TEXT("a Shipping build cannot press menu buttons for a test");
	}
	return false;
#else
	Button->SimulateClick();
	return true;
#endif
}
