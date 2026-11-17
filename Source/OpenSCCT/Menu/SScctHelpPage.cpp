#include "Menu/SScctHelpPage.h"

#include "Brushes/SlateColorBrush.h"
#include "Menu/SScctBindingsPanel.h"
#include "Menu/ScctMenuStyle.h"
#include "OpenSCCT.h"
#include "Runtime/ScctMatchMenuApi.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	const FName HelpPageType(TEXT("SScctHelpPage"));

	constexpr float HelpControlsWidth = 560.0f;
	constexpr float HelpGoalsWidth = 380.0f;
	constexpr float HelpHeight = 520.0f;
	constexpr float HelpLabelWidth = 300.0f;

	/** A category id as a heading: "interface" -> "INTERFACE". */
	FString HelpHeading(const FString& Category)
	{
		return Category.IsEmpty() ? FString(TEXT("OTHER")) : Category.ToUpper();
	}
}

TSharedRef<SScctHelpPage> SScctHelpPage::Open(const TSharedRef<SOverlay>& Host, FSimpleDelegate OnClosed)
{
	if (const TSharedPtr<SScctHelpPage> Already = FindOpen(Host))
	{
		return Already.ToSharedRef();
	}
	TSharedRef<SScctHelpPage> Page = SNew(SScctHelpPage).OnClosed(OnClosed);
	Page->Host = Host;
	Host->AddSlot()[ Page ];
	return Page;
}

TSharedPtr<SScctHelpPage> SScctHelpPage::FindOpen(const TSharedRef<SOverlay>& Host)
{
	FChildren* Children = Host->GetChildren();
	for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
	{
		const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
		if (Child->GetType() == HelpPageType)
		{
			return StaticCastSharedRef<SScctHelpPage>(Child);
		}
	}
	return nullptr;
}

void SScctHelpPage::Construct(const FArguments& InArgs)
{
	OnClosed = InArgs._OnClosed;

	// The side played and the round's goals.
	FScctMatchMenuContext Context = {};
	FString Goals;
	if (const FScctMatchMenuApi* Api = ScctFeature<FScctMatchMenuApi>())
	{
		Api->GetContext(&Context);
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(Api->GetGoals(nullptr, 0) + 1);
		Api->GetGoals(Buffer.GetData(), Buffer.Num());
		Goals = FString(Buffer.GetData());
	}
	const bool bMerc = Context.Merc != 0;
	const FLinearColor SideColour = ScctHudStyle::Accent(bMerc ? 1 : 0);

	// The side's actions with the keys in play, under their categories.
	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
	FString Category;
	bool bFirst = true;
	int32 Shown = 0;
	int32 Unbound = 0;
	for (const FScctBindingRow& Row : ScctBindings::Rows(bMerc))
	{
		if (Row.Keys.IsEmpty())
		{
			++Unbound;
			continue;
		}
		if (bFirst || Row.Category != Category)
		{
			Category = Row.Category;
			Controls->AddSlot().AutoHeight().Padding(0.0f, bFirst ? 0.0f : 14.0f, 0.0f, 6.0f)[ Ui::Label(HelpHeading(Category), SideColour) ];
			bFirst = false;
		}
		TArray<FString> Keys;
		for (const FString& Key : Row.Keys)
		{
			Keys.Add(ScctBindings::DisplayName(Key));
		}
		Controls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			.ToolTipText(Ui::ToText(Row.Description))
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(HelpLabelWidth)[ Ui::Text(Ui::ToText(Row.Label), 12.5f) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				Ui::Text(Ui::ToText(FString::Join(Keys, TEXT("  /  "))), 12.5f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Bold)
			]
		];
		++Shown;
	}
	if (Shown == 0)
	{
		Controls->AddSlot().AutoHeight()[ Ui::Text(Ui::ToText(TEXT("The key bindings are not available.")), 12.5f, FSlateColor(ScctHudStyle::TextDim())) ];
	}
	else if (Unbound > 0)
	{
		Controls->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
		[
			Ui::Text(Ui::ToText(FString::Printf(TEXT("%d more action%s without a key: bind them on Options, %s."), Unbound, Unbound == 1 ? TEXT("") : TEXT("s"),
				bMerc ? TEXT("Mercenary") : TEXT("Spy"))), 11.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
		];
	}

	// The goals, a line each: the first two (mode and what it asks) stand out, objectives done are dimmed.
	TSharedRef<SVerticalBox> GoalRows = SNew(SVerticalBox);
	TArray<FString> Lines;
	Goals.ParseIntoArray(Lines, TEXT("\n"));
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		const bool bDone = Line.StartsWith(TEXT("[done]"));
		GoalRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Index == 2 ? 14.0f : 6.0f)
		[
			Ui::Text(Ui::ToText(Line), Index == 0 ? 15.0f : 12.5f,
				FSlateColor(Index == 0 ? SideColour : bDone ? ScctHudStyle::TextFaint() : ScctHudStyle::Text()),
				Index == 0 ? ScctHudStyle::EWeight::Bold : ScctHudStyle::EWeight::Regular, true)
		];
	}
	if (Lines.IsEmpty())
	{
		GoalRows->AddSlot().AutoHeight()[ Ui::Text(Ui::ToText(TEXT("No round is being played.")), 12.5f, FSlateColor(ScctHudStyle::TextDim())) ];
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Help shows the %s controls (%d actions with keys, %d without) and %d goal line(s): %s"),
		bMerc ? TEXT("mercenary") : TEXT("spy"), Shown, Unbound, Lines.Num(), *FString::Join(Lines, TEXT(" | ")));

	static const FSlateColorBrush Dim(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		// What is behind is dimmed and, being covered, takes no clicks while the page is open.
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(&Dim)
			.ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24.0f)
		[
			Ui::Glass(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 24.0f, 0.0f)
					[
						Ui::Title(Ui::ToText(TEXT("HELP")), 26.0f)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
					[
						Ui::Label(bMerc ? TEXT("PLAYING A MERCENARY") : TEXT("PLAYING A SPY"), SideColour)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 16.0f)[ Ui::Rule() ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 24.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(HelpControlsWidth)
						.HeightOverride(HelpHeight)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Ui::Label(TEXT("YOUR CONTROLS")) ]
							+ SVerticalBox::Slot().FillHeight(1.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()[ Controls ]
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(HelpGoalsWidth)
						.HeightOverride(HelpHeight)
						[
							Ui::Glass(
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Ui::Label(TEXT("THE ROUND'S GOALS")) ]
								+ SVerticalBox::Slot().FillHeight(1.0f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()[ GoalRows ]
								], FMargin(18.0f), 0.5f)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 12.0f)[ Ui::Rule() ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateLambda([this] { Close(); return FReply::Handled(); }), false, Ui::Accent(), 150.0f)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
					[
						Ui::Text(Ui::ToText(TEXT("Keys are changed on Options. Point at an action for what it does.")), 11.5f, FSlateColor(ScctHudStyle::TextDim()))
					]
				], FMargin(30.0f, 26.0f), 0.82f)
		]
	];
}

void SScctHelpPage::Close()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	const TSharedRef<SScctHelpPage> Self = SharedThis(this);
	if (const TSharedPtr<SOverlay> Overlay = Host.Pin())
	{
		Overlay->RemoveSlot(Self);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Help closed"));
	OnClosed.ExecuteIfBound();
}
