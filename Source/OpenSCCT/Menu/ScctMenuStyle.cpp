#include "Menu/ScctMenuStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace ScctMenuStyle
{
	namespace
	{
		constexpr float ButtonCorner = 4.0f;

		/** The panel's fill: the HUD's glass tint, with its hairline edge. The widget's colour sets the alpha. */
		const FSlateBrush* GlassBrush()
		{
			static const FSlateRoundedBoxBrush Brush(FLinearColor::White, ScctHudStyle::Corner, FLinearColor(1.0f, 1.0f, 1.0f, 0.09f), 1.0f);
			return &Brush;
		}

		/** White boxes: the button's own colour attribute decides what they look like (see Button). */
		const FButtonStyle& ButtonStyle()
		{
			static const FButtonStyle Style = FButtonStyle()
				.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, ButtonCorner))
				.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, ButtonCorner))
				.SetPressed(FSlateRoundedBoxBrush(FLinearColor::White, ButtonCorner))
				.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), ButtonCorner))
				.SetNormalPadding(FMargin(18.0f, 9.0f))
				.SetPressedPadding(FMargin(18.0f, 10.0f, 18.0f, 8.0f));
			return Style;
		}

		const FEditableTextBoxStyle& TextBoxStyle()
		{
			static const FEditableTextBoxStyle Style = []
			{
				FEditableTextBoxStyle Made = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
				const FLinearColor Fill(0.0f, 0.0f, 0.0f, 0.35f);
				Made.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Fill, ButtonCorner, FLinearColor(1.0f, 1.0f, 1.0f, 0.14f), 1.0f));
				Made.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Fill, ButtonCorner, FLinearColor(1.0f, 1.0f, 1.0f, 0.3f), 1.0f));
				Made.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Fill, ButtonCorner, ScctHudStyle::Fade(Accent(), 0.8f), 1.0f));
				Made.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Fill, ButtonCorner));
				Made.SetBackgroundColor(FLinearColor::White);
				Made.SetForegroundColor(ScctHudStyle::Text());
				Made.SetFocusedForegroundColor(ScctHudStyle::Text());
				Made.SetPadding(FMargin(12.0f, 8.0f));
				return Made;
			}();
			return Style;
		}
	}

	FLinearColor Accent()
	{
		return ScctHudStyle::Accent(0);
	}

	TSharedRef<SWidget> Glass(const TSharedRef<SWidget>& Content, const FMargin& Padding, float Tint)
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBackgroundBlur)
				.BlurStrength(16.0f)
				.CornerRadius(FVector4(ScctHudStyle::Corner, ScctHudStyle::Corner, ScctHudStyle::Corner, ScctHudStyle::Corner))
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(GlassBrush())
				.BorderBackgroundColor(FLinearColor(0.004f, 0.007f, 0.011f, Tint))
				.Padding(Padding)
				[
					Content
				]
			];
	}

	TSharedRef<SWidget> Label(const FString& Text, const FLinearColor& Colour)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Text.ToUpper()))
			.Font(ScctHudStyle::Label(9.0f))
			.ColorAndOpacity(Colour);
	}

	TSharedRef<SWidget> Text(const TAttribute<FText>& Text, float Size, const TAttribute<FSlateColor>& Colour, ScctHudStyle::EWeight Weight, bool bWrap)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(ScctHudStyle::Font(Weight, Size))
			.ColorAndOpacity(Colour)
			.AutoWrapText(bWrap);
	}

	TSharedRef<SWidget> Title(const TAttribute<FText>& Text, float Size)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, Size, 220))
			.ColorAndOpacity(ScctHudStyle::Text());
	}

	TSharedRef<SButton> Button(const TAttribute<FText>& Label, FOnClicked OnClicked, const TAttribute<bool>& Selected, const FLinearColor& Colour, float MinWidth)
	{
		TSharedRef<SButton> Made = SNew(SButton)
			.ButtonStyle(&ButtonStyle())
			.OnClicked(OnClicked)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.IsFocusable(false)
			[
				SNew(SBox)
				.MinDesiredWidth(MinWidth)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 11.0f, 140))
					.ColorAndOpacity(ScctHudStyle::Text())
				]
			];

		// The style's boxes are white; this is the whole colour of the button, by what it is doing.
		const TWeakPtr<SButton> Weak = Made;
		Made->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([Weak, Selected, Colour]
		{
			const TSharedPtr<SButton> Pinned = Weak.Pin();
			const bool bHovered = Pinned.IsValid() && Pinned->IsHovered();
			const bool bPressed = Pinned.IsValid() && Pinned->IsPressed();
			if (Selected.Get())
			{
				return FSlateColor(ScctHudStyle::Fade(Colour, bHovered ? 0.5f : 0.38f));
			}
			return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, bPressed ? 0.26f : bHovered ? 0.16f : 0.07f));
		}));
		return Made;
	}

	TSharedRef<SEditableTextBox> TextBox(const FString& Hint, const FString& Initial, bool bPassword, FOnTextCommitted OnCommitted)
	{
		return SNew(SEditableTextBox)
			.Style(&TextBoxStyle())
			.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 12.0f))
			.HintText(FText::FromString(Hint))
			.Text(FText::FromString(Initial))
			.IsPassword(bPassword)
			.OnTextCommitted(OnCommitted)
			.MinDesiredWidth(260.0f);
	}

	TSharedRef<SWidget> Rule()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return SNew(SBox)
			.HeightOverride(1.0f)
			[
				SNew(SImage)
				.Image(&Brush)
				.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.1f))
			];
	}
}
