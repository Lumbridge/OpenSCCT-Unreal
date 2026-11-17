#include "Menu/ScctSetupRowsView.h"

#include "Menu/ScctMenuStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"

namespace Ui = ScctMenuStyle;

namespace
{
	/** A value as a read-only row shows it, where the buttons would be. */
	TSharedRef<SWidget> ShownValue(const FString& Shown, const FLinearColor& Colour = ScctHudStyle::Text())
	{
		return Ui::Text(Ui::ToText(Shown), 12.5f, FSlateColor(Colour), ScctHudStyle::EWeight::Bold);
	}

	/** A number with - and + (editable), or its value. */
	TSharedRef<SWidget> Stepper(const FString& Shown, int32 Value, int32 Min, int32 Max, float Width, const TFunction<void(int32)>& Set, bool bEditable)
	{
		if (!bEditable)
		{
			return ShownValue(Shown);
		}
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).IsEnabled(Value > Min)
				[
					Ui::Button(Ui::ToText(TEXT("-")), FOnClicked::CreateLambda([Set, Value] { Set(Value - 1); return FReply::Handled(); }))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(Width).HAlign(HAlign_Center)[ Ui::Text(Ui::ToText(Shown), 13.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Bold) ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).IsEnabled(Value < Max)
				[
					Ui::Button(Ui::ToText(TEXT("+")), FOnClicked::CreateLambda([Set, Value] { Set(Value + 1); return FReply::Handled(); }))
				]
			];
	}

	/** An on/off switch (editable), or its state. */
	TSharedRef<SWidget> Switch(bool bOn, const TCHAR* OnText, const TCHAR* OffText, const FLinearColor& Colour, float Width, const TFunction<void()>& Toggle, bool bEditable)
	{
		if (!bEditable)
		{
			return ShownValue(bOn ? OnText : OffText, bOn ? Colour : ScctHudStyle::TextFaint());
		}
		return Ui::Button(Ui::ToText(bOn ? OnText : OffText), FOnClicked::CreateLambda([Toggle] { Toggle(); return FReply::Handled(); }), bOn, Colour, Width);
	}

	TSharedRef<SWidget> Line(const TSharedRef<SWidget>& Caption, const TSharedRef<SWidget>& Control, float Top = 2.0f, float Bottom = 5.0f)
	{
		return SNew(SBox).Padding(0.0f, Top, 0.0f, Bottom)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Caption ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Control ]
		];
	}
}

TSharedRef<SWidget> ScctSetupRowsView::Heading(const FString& Text)
{
	return SNew(SBox).Padding(0.0f, 10.0f, 0.0f, 8.0f)[ Ui::Label(Text, Ui::Accent()) ];
}

TSharedRef<SWidget> ScctSetupRowsView::Note(const TAttribute<FText>& Text)
{
	return SNew(SBox).Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Ui::Text(Text, 10.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true) ];
}

TSharedRef<SWidget> ScctSetupRowsView::Row(const FScctSetupRow* Found, const FOnEdit& OnEdit, bool bEditable)
{
	if (Found == nullptr || !Found->bVisible)
	{
		return SNullWidget::NullWidget;
	}

	const FScctSetupRow Row = *Found;
	const TSharedRef<SWidget> Caption = Ui::Text(Ui::ToText(Row.Caption), 12.0f);
	switch (Row.Kind)
	{
	case FScctSetupRow::EKind::Number:
		return Line(Caption, Stepper(Row.Shown, Row.Value, Row.Min, Row.Max, 58.0f,
			[OnEdit, Name = Row.Row](int32 Number) { OnEdit(FString::Printf(TEXT("%s %d"), *Name, Number)); }, bEditable));

	case FScctSetupRow::EKind::Check:
		return Line(Caption, Switch(Row.Value != 0, TEXT("YES"), TEXT("NO"), Ui::Accent(), 70.0f,
			[OnEdit, Name = Row.Row, bOn = Row.Value != 0] { OnEdit(FString::Printf(TEXT("%s %d"), *Name, bOn ? 0 : 1)); }, bEditable));

	case FScctSetupRow::EKind::Choice:
	default:
	{
		if (!bEditable)
		{
			const FString Shown = Row.Choices.IsValidIndex(Row.Value) ? Row.Choices[Row.Value] : Row.Shown;
			return Line(Caption, ShownValue(Shown));
		}

		// A choice is a caption over its buttons; the pages are too narrow to put them beside it.
		const TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
		for (int32 Index = 0; Index < Row.Choices.Num(); ++Index)
		{
			Buttons->AddSlot()
			[
				Ui::Button(Ui::ToText(Row.Choices[Index].ToUpper()),
					FOnClicked::CreateLambda([OnEdit, Name = Row.Row, Index] { OnEdit(FString::Printf(TEXT("%s %d"), *Name, Index)); return FReply::Handled(); }),
					Index == Row.Value)
			];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 6.0f)[ Caption ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Buttons ];
	}
	}
}

TSharedRef<SWidget> ScctSetupRowsView::Alternate(const TArray<FScctAlternateRow>& Rows, const FOnEdit& OnEditAlternate, bool bEditable)
{
	const auto Side = [&Rows, &OnEditAlternate, bEditable](int32 Team)
	{
		const TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
		Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Ui::Label(Team == 0 ? TEXT("Spies") : TEXT("Mercenaries"), ScctHudStyle::Accent(Team)) ];
		for (const FScctAlternateRow::EKind Kind : { FScctAlternateRow::EKind::Ability, FScctAlternateRow::EKind::Gadget, FScctAlternateRow::EKind::Slots })
		{
			if (Kind != FScctAlternateRow::EKind::Slots)
			{
				Column->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)[ Ui::Label(Kind == FScctAlternateRow::EKind::Ability ? TEXT("Abilities") : TEXT("Gadgets")) ];
			}
			for (const FScctAlternateRow& Row : Rows)
			{
				if (Row.Kind != Kind || Row.Side != Team)
				{
					continue;
				}

				TSharedRef<SWidget> Control = SNullWidget::NullWidget;
				FString Caption = Row.Caption;
				if (Kind == FScctAlternateRow::EKind::Slots)
				{
					Caption = TEXT("Number of gadgets");
					Control = Stepper(FString::FromInt(Row.Value), Row.Value, 0, Row.Most, 34.0f,
						[OnEditAlternate, RowSide = Row.Side](int32 Count) { OnEditAlternate(FString::Printf(TEXT("slots %d %d"), RowSide, Count)); }, bEditable);
				}
				else
				{
					const FString Change = Kind == FScctAlternateRow::EKind::Ability
						? FString::Printf(TEXT("ability %d %d"), Row.Id, Row.Value != 0 ? 0 : 1)
						: FString::Printf(TEXT("gadget %d %d %d"), Row.Side, Row.Id, Row.Value != 0 ? 0 : 1);
					Control = Switch(Row.Value != 0, TEXT("ON"), TEXT("OFF"), ScctHudStyle::Accent(Team), 56.0f, [OnEditAlternate, Change] { OnEditAlternate(Change); }, bEditable);
				}
				Column->AddSlot().AutoHeight()[ Line(Ui::Text(Ui::ToText(Caption), 11.0f), Control, 1.0f, 3.0f) ];
			}
		}
		return Column;
	};

	const FScctAlternateRow* Cinematic = Rows.FindByPredicate([](const FScctAlternateRow& Row) { return Row.Kind == FScctAlternateRow::EKind::Ability && Row.Side == 2; });
	TSharedRef<SWidget> CinematicRow = SNullWidget::NullWidget;
	if (Cinematic != nullptr)
	{
		const FString Change = FString::Printf(TEXT("ability %d %d"), Cinematic->Id, Cinematic->Value != 0 ? 0 : 1);
		CinematicRow = Line(Ui::Text(Ui::ToText(Cinematic->Caption), 12.0f),
			Switch(Cinematic->Value != 0, TEXT("ON"), TEXT("OFF"), Ui::Accent(), 56.0f, [OnEditAlternate, Change] { OnEditAlternate(Change); }, bEditable));
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 18.0f, 0.0f)[ Side(0) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)[ Side(1) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ CinematicRow ];
}
