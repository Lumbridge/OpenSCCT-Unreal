#include "Menu/SScctOptionsPage.h"

#include "Algo/StableSort.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/SScctConfirm.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	/** Where the registry keeps its values (Settings/ScctSettings.cpp). */
	const TCHAR* const SettingsSection = TEXT("OpenSCCT.Settings");

	constexpr float ScreenWidth = 1040.0f;
	constexpr float BodyHeight = 500.0f;
	constexpr float LabelShare = 0.42f;

	TMap<FName, FScctOptionsSection>& Sections()
	{
		static TMap<FName, FScctOptionsSection> Registered;
		return Registered;
	}

	/** The registry's page order (Settings/ScctSettings.h); a page that is not here comes after these. */
	const TArray<FName>& PageOrder()
	{
		static const TArray<FName> Order = {
			TEXT("Spy"), TEXT("Mercenary"), TEXT("Display"), TEXT("Graphics"), TEXT("Performance"),
			TEXT("Audio"), TEXT("Voice"), TEXT("Hud"), TEXT("Controller"), TEXT("Online") };
		return Order;
	}

	FString TabLabel(FName Page)
	{
		return Page == TEXT("Hud") ? FString(TEXT("HUD")) : Page.ToString().ToUpper();
	}

	/** A value as the registry would store it (its own clamp, Settings/ScctSettings.cpp), so an edit compares equal to what applying it keeps. */
	FString Normalize(const FScctSettingDesc& Desc, const FString& Value)
	{
		switch (Desc.Type)
		{
		case EScctSettingType::Bool:
			return Value.ToBool() ? TEXT("true") : TEXT("false");
		case EScctSettingType::Float:
		{
			float Number = FMath::Clamp(FCString::Atof(*Value), Desc.Min, Desc.Max);
			if (Desc.Step > 0.f)
			{
				Number = FMath::Clamp(Desc.Min + FMath::RoundToFloat((Number - Desc.Min) / Desc.Step) * Desc.Step, Desc.Min, Desc.Max);
			}
			return FString::SanitizeFloat(Number);
		}
		case EScctSettingType::Int:
			return FString::FromInt(FMath::Clamp(FCString::Atoi(*Value), FMath::RoundToInt(Desc.Min), FMath::RoundToInt(Desc.Max)));
		case EScctSettingType::Choice:
			return FString::FromInt(FMath::Clamp(FCString::Atoi(*Value), 0, FMath::Max(0, Desc.Choices.Num() - 1)));
		default:
			return Value;
		}
	}

	/** A number as the slider's value text shows it: as many decimals as the step needs. */
	FString FormatNumber(const FScctSettingDesc& Desc, float Value)
	{
		if (Desc.Type == EScctSettingType::Int)
		{
			return FString::FromInt(FMath::RoundToInt(Value));
		}
		const int32 Decimals = Desc.Step > 0.f ? FMath::Clamp(FMath::CeilToInt32(-FMath::LogX(10.f, Desc.Step) - 1e-4f), 0, 3) : 2;
		return FString::Printf(TEXT("%.*f"), Decimals, Value);
	}

	/**
	 * -ScctOptionsDemo: options on a few pages, of every type and in groups, so the screen can be looked at before
	 * the features have registered theirs. Their keys start with "Test." and nothing reads them.
	 */
	void RegisterDemoOptions()
	{
		const auto Add = [](const TCHAR* Key, const TCHAR* Page, const TCHAR* Group, const TCHAR* Label, EScctSettingType Type,
			const TCHAR* Default, float Min = 0.f, float Max = 1.f, float Step = 0.f, TArray<FText> Choices = {})
		{
			FScctSettingDesc Desc;
			Desc.Key = Key;
			Desc.Page = Page;
			Desc.Group = FText::FromString(Group);
			Desc.Label = FText::FromString(Label);
			Desc.Tooltip = FText::FromString(FString::Printf(TEXT("A test option (-ScctOptionsDemo): %s. Nothing reads it."), Label));
			Desc.Type = Type;
			Desc.Default = Default;
			Desc.Min = Min;
			Desc.Max = Max;
			Desc.Step = Step;
			Desc.Choices = MoveTemp(Choices);
			FScctSettings::Register(Desc);
		};
		const TArray<FText> Modes = { FText::FromString(TEXT("Windowed")), FText::FromString(TEXT("Borderless")), FText::FromString(TEXT("Fullscreen")) };
		Add(TEXT("Test.WindowMode"), TEXT("Display"), TEXT("Window"), TEXT("Window mode"), EScctSettingType::Choice, TEXT("1"), 0.f, 2.f, 0.f, Modes);
		Add(TEXT("Test.VSync"), TEXT("Display"), TEXT("Window"), TEXT("Vertical sync"), EScctSettingType::Bool, TEXT("true"));
		Add(TEXT("Test.FrameCap"), TEXT("Display"), TEXT("Window"), TEXT("Frame rate cap"), EScctSettingType::Int, TEXT("120"), 30.f, 240.f, 10.f);
		Add(TEXT("Test.Gamma"), TEXT("Display"), TEXT("Picture"), TEXT("Brightness"), EScctSettingType::Float, TEXT("1.0"), 0.5f, 1.5f, 0.05f);
		Add(TEXT("Test.Fov"), TEXT("Display"), TEXT("Picture"), TEXT("Field of view"), EScctSettingType::Float, TEXT("75"), 60.f, 110.f, 1.f);
		Add(TEXT("Test.HudScale"), TEXT("Hud"), TEXT("Layout"), TEXT("HUD scale"), EScctSettingType::Float, TEXT("1.0"), 0.6f, 1.6f, 0.1f);
		Add(TEXT("Test.ShowPing"), TEXT("Hud"), TEXT("Layout"), TEXT("Show ping"), EScctSettingType::Bool, TEXT("false"));
		Add(TEXT("Test.Crosshair"), TEXT("Hud"), TEXT("Crosshair"), TEXT("Crosshair colour"), EScctSettingType::Choice, TEXT("0"), 0.f, 2.f, 0.f,
			{ FText::FromString(TEXT("Side colour")), FText::FromString(TEXT("White")), FText::FromString(TEXT("Green")) });
		Add(TEXT("Test.ChatTag"), TEXT("Hud"), TEXT("Chat"), TEXT("Chat prefix"), EScctSettingType::Text, TEXT(""));
		Add(TEXT("Test.Master"), TEXT("Audio"), TEXT("Volume"), TEXT("Master volume"), EScctSettingType::Float, TEXT("0.8"), 0.f, 1.f, 0.05f);
		Add(TEXT("Test.Music"), TEXT("Audio"), TEXT("Volume"), TEXT("Music volume"), EScctSettingType::Float, TEXT("0.5"), 0.f, 1.f, 0.05f);
	}
}

void SScctOptionsPage::RegisterSection(FName Page, const FScctOptionsSection& Section)
{
	Sections().Add(Page, Section);
}

FString SScctOptionsPage::PlayerName(const UScctOnlineSubsystem* Online)
{
	// The subsystem reads its identity when it first connects; until then the stored name is the one it will read.
	FString Name = Online != nullptr ? Online->GetPlayerName() : FString();
	if (Name.IsEmpty() && GConfig != nullptr)
	{
		GConfig->GetString(TEXT("OpenSCCT.Online"), TEXT("PlayerName"), Name, GGameUserSettingsIni);
	}
	return Name.IsEmpty() ? FString(TEXT("Player")) : Name;
}

void SScctOptionsPage::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	OnClose = InArgs._OnClose;
	bTakeKeyboard = InArgs._bTakeKeyboard;
	EditedName = PlayerName(Online.Get());

	if (FParse::Param(FCommandLine::Get(), TEXT("ScctOptionsDemo")))
	{
		RegisterDemoOptions();
	}
	const TArray<FName> Pages = FindPages();
	CurrentPage = Pages.Contains(InArgs._InitialPage) ? InArgs._InitialPage : Pages.Num() > 0 ? Pages[0] : NAME_None;

	NameBox = Ui::TextBox(TEXT("Player"), EditedName, false, FOnTextCommitted(),
		FOnTextChanged::CreateLambda([this](const FText& Text) { EditedName = Text.ToString(); }));
	NameBox->SetMaximumLength(31);
	SearchBox = Ui::TextBox(TEXT("Search every page"), FString(), false, FOnTextCommitted(),
		FOnTextChanged::CreateLambda([this](const FText& Text)
		{
			Search = Text.ToString().TrimStartAndEnd();
			RebuildTabs();
			RebuildBody();
		}));

	const auto Action = [this](const TCHAR* LabelText, TFunction<void()> Do, bool bPrimary = false)
	{
		return Ui::Button(Ui::ToText(LabelText), FOnClicked::CreateLambda([Do] { Do(); return FReply::Handled(); }), bPrimary, Ui::Accent(), 104.0f);
	};

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// The title, with the mark of changes that were not saved.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[ Ui::Title(Ui::ToText(TEXT("OPTIONS")), 24.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(16.0f, 0.0f, 0.0f, 5.0f)
		[
			SNew(STextBlock)
			.Font(ScctHudStyle::Label(10.0f))
			.ColorAndOpacity(ScctHudStyle::Accent(1))
			.Text(TAttribute<FText>::CreateLambda([this] { return HasUnsavedChanges() ? Ui::ToText(TEXT("*  UNSAVED CHANGES")) : FText::GetEmpty(); }))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([this]
			{
				return Ui::ToText(HasEdits() ? TEXT("Changes not applied yet") : HasApplied() ? TEXT("Applied, not saved yet") : TEXT("All changes saved"));
			}), 11.0f, FSlateColor(ScctHudStyle::TextDim()))
		]
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 14.0f)[ Ui::Rule() ];

	// The player's name, and the search.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)[ Ui::Label(TEXT("Player name")) ]
		+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 32.0f, 0.0f)[ NameBox.ToSharedRef() ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)[ Ui::Label(TEXT("Search")) ]
		+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[ SearchBox.ToSharedRef() ]
	];

	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)[ SAssignNew(TabsBox, SBox) ];
	Box->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(BodyHeight)
		[
			SAssignNew(Body, SScrollBox)
		]
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 16.0f)[ Ui::Rule() ];

	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ Action(TEXT("APPLY"), [this] { Apply(); }) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ Action(TEXT("SAVE"), [this] { Save(); }) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 22.0f, 0.0f)[ Action(TEXT("SAVE & CLOSE"), [this] { Save(); Close(); }, true) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ Action(TEXT("RESET PAGE"), [this] { ResetPage(CurrentPage); RebuildBody(); }) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Action(TEXT("RESET ALL"), [this] { ResetAll(); }) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)[ Action(TEXT("BACK"), [this] { RequestBack(); }) ]
	];

	ChildSlot
	[
		SNew(SBox).WidthOverride(ScreenWidth)
		[
			SAssignNew(Root, SOverlay)
			+ SOverlay::Slot()[ Ui::Glass(Box, FMargin(28.0f), 0.8f) ]
		]
	];

	RebuildTabs();
	RebuildBody();
	RunUnattendedSwitches();
	UE_LOG(LogOpenSCCT, Log, TEXT("Options: shown on page %s (%d page(s))"), *CurrentPage.ToString(), Pages.Num());
}

void SScctOptionsPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// The keyboard can only be given to a widget that is on screen, which the page is by its first tick.
	if (bTakeKeyboard && !bKeyboardTaken && FSlateApplication::IsInitialized())
	{
		bKeyboardTaken = true;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

FReply SScctOptionsPage::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		RequestBack();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TArray<FName> SScctOptionsPage::FindPages() const
{
	TArray<FName> Pages = FScctSettings::Pages();
	for (const TPair<FName, FScctOptionsSection>& Section : Sections())
	{
		Pages.AddUnique(Section.Key);
	}
	// The registry's order first, every other page after them as they came.
	Algo::StableSortBy(Pages, [](FName Page)
	{
		const int32 Index = PageOrder().IndexOfByKey(Page);
		return Index == INDEX_NONE ? PageOrder().Num() : Index;
	});
	return Pages;
}

void SScctOptionsPage::ShowPage(FName Page)
{
	CurrentPage = Page;
	if (!Search.IsEmpty())
	{
		Search.Reset();
		SearchBox->SetText(FText::GetEmpty());
	}
	RebuildTabs();
	RebuildBody();
}

void SScctOptionsPage::RebuildTabs()
{
	const TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
	for (const FName Page : FindPages())
	{
		Row->AddSlot()
		[
			Ui::Button(Ui::ToText(TabLabel(Page)), FOnClicked::CreateLambda([this, Page] { ShowPage(Page); return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this, Page] { return Search.IsEmpty() && CurrentPage == Page; }), Ui::Accent(), 86.0f)
		];
	}
	TabsBox->SetContent(Row);
}

void SScctOptionsPage::RebuildBody()
{
	Body->ClearChildren();
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const auto Heading = [&Box](const FString& Text, bool bFirst)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, bFirst ? 0.0f : 18.0f, 0.0f, 8.0f)[ Ui::Label(Text, Ui::Accent()) ];
	};
	const auto Note = [&Box](const FString& Text)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f)[ Ui::Text(Ui::ToText(Text), 12.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true) ];
	};

	if (!Search.IsEmpty())
	{
		// Every page's options whose label holds the text, under the page and the group they are on.
		int32 Found = 0;
		for (const FName Page : FindPages())
		{
			FString LastHeading;
			for (const FScctSettingDesc* Desc : FScctSettings::OptionsOf(Page))
			{
				if (!Desc->Label.ToString().Contains(Search, ESearchCase::IgnoreCase))
				{
					continue;
				}
				const FString Where = Desc->Group.IsEmpty() ? TabLabel(Page) : TabLabel(Page) + TEXT("  /  ") + Desc->Group.ToString();
				if (Where != LastHeading)
				{
					Heading(Where, Found == 0);
					LastHeading = Where;
				}
				AddOptionRow(Box, *Desc);
				++Found;
			}
		}
		if (Found == 0)
		{
			Note(FString::Printf(TEXT("No option is called anything like \"%s\"."), *Search));
		}
		Body->AddSlot()[ Box ];
		return;
	}

	if (CurrentPage.IsNone())
	{
		Note(TEXT("No options have been registered yet."));
		Body->AddSlot()[ Box ];
		return;
	}

	bool bAnything = false;
	if (const FScctOptionsSection* Section = Sections().Find(CurrentPage); Section != nullptr && Section->Build)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Section->Build() ];
		bAnything = true;
	}

	// Options of one group together, the groups in the order their first option comes.
	const TArray<const FScctSettingDesc*> Options = FScctSettings::OptionsOf(CurrentPage);
	TArray<FString> Groups;
	for (const FScctSettingDesc* Desc : Options)
	{
		Groups.AddUnique(Desc->Group.ToString());
	}
	for (const FString& Group : Groups)
	{
		if (!Group.IsEmpty())
		{
			Heading(Group, !bAnything);
		}
		for (const FScctSettingDesc* Desc : Options)
		{
			if (Desc->Group.ToString() == Group)
			{
				AddOptionRow(Box, *Desc);
				bAnything = true;
			}
		}
	}
	if (!bAnything)
	{
		Note(TEXT("Nothing to set on this page yet."));
	}
	Body->AddSlot()[ Box ];
}

void SScctOptionsPage::AddOptionRow(const TSharedRef<SVerticalBox>& Box, const FScctSettingDesc& Desc)
{
	const FName Key = Desc.Key;
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 14.0f, 6.0f)
	[
		SNew(SBox)
		.MinDesiredHeight(34.0f)
		.ToolTipText(Desc.Tooltip)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(LabelShare).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
			[
				// An edited option's label is lit until the edit is applied.
				Ui::Text(Desc.Label, 13.0f, TAttribute<FSlateColor>::CreateLambda([this, Key]
				{
					return FSlateColor(Edits.Contains(Key) ? ScctHudStyle::Accent(1) : ScctHudStyle::Text());
				}))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f - LabelShare).VAlign(VAlign_Center)[ MakeControl(Desc) ]
		]
	];
}

TSharedRef<SWidget> SScctOptionsPage::MakeControl(const FScctSettingDesc& Desc)
{
	// The description lives in the registry for the whole run; the controls look it up by key so a replaced
	// description is never read through a stale pointer.
	const FName Key = Desc.Key;
	switch (Desc.Type)
	{
	case EScctSettingType::Bool:
		return SNew(SCheckBox)
			.IsChecked(TAttribute<ECheckBoxState>::CreateLambda([this, Key]
			{
				const FScctSettingDesc* Found = FScctSettings::Find(Key);
				return Found && ValueOf(*Found).ToBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}))
			.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda([this, Key](ECheckBoxState State)
			{
				if (const FScctSettingDesc* Found = FScctSettings::Find(Key))
				{
					Edit(*Found, State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false"));
				}
			}));

	case EScctSettingType::Float:
	case EScctSettingType::Int:
	{
		const bool bInt = Desc.Type == EScctSettingType::Int;
		const float Step = Desc.Step > 0.f ? Desc.Step : bInt ? 1.f : (Desc.Max - Desc.Min) / 100.f;
		const auto Current = [this, Key]
		{
			const FScctSettingDesc* Found = FScctSettings::Find(Key);
			return Found ? FCString::Atof(*ValueOf(*Found)) : 0.f;
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.MinValue(Desc.Min)
				.MaxValue(Desc.Max)
				.StepSize(Step)
				.MouseUsesStep(true)
				.IsFocusable(false)
				.SliderBarColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.25f))
				.SliderHandleColor(Ui::Accent())
				.Value(TAttribute<float>::CreateLambda(Current))
				.OnValueChanged(FOnFloatValueChanged::CreateLambda([this, Key, bInt](float Value)
				{
					if (const FScctSettingDesc* Found = FScctSettings::Find(Key))
					{
						Edit(*Found, bInt ? FString::FromInt(FMath::RoundToInt(Value)) : FString::SanitizeFloat(Value));
					}
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(64.0f).HAlign(HAlign_Right)
				[
					Ui::Text(TAttribute<FText>::CreateLambda([Key, Current]
					{
						const FScctSettingDesc* Found = FScctSettings::Find(Key);
						return Found ? Ui::ToText(FormatNumber(*Found, Current())) : FText::GetEmpty();
					}), 12.5f)
				]
			];
	}

	case EScctSettingType::Choice:
	{
		const auto Step = [this, Key](int32 By)
		{
			const FScctSettingDesc* Found = FScctSettings::Find(Key);
			if (Found && Found->Choices.Num() > 0)
			{
				const int32 Count = Found->Choices.Num();
				Edit(*Found, FString::FromInt(((FCString::Atoi(*ValueOf(*Found)) + By) % Count + Count) % Count));
			}
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("<")), FOnClicked::CreateLambda([Step] { Step(-1); return FReply::Handled(); }))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(10.0f, 0.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([this, Key]
				{
					const FScctSettingDesc* Found = FScctSettings::Find(Key);
					if (Found == nullptr)
					{
						return FText::GetEmpty();
					}
					const int32 Index = FCString::Atoi(*ValueOf(*Found));
					return Found->Choices.IsValidIndex(Index) ? Found->Choices[Index] : FText::GetEmpty();
				}), 13.0f)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT(">")), FOnClicked::CreateLambda([Step] { Step(1); return FReply::Handled(); }))
			];
	}

	case EScctSettingType::Text:
	default:
		return Ui::TextBox(FString(), ValueOf(Desc), false, FOnTextCommitted(), FOnTextChanged::CreateLambda([this, Key](const FText& Text)
		{
			if (const FScctSettingDesc* Found = FScctSettings::Find(Key))
			{
				Edit(*Found, Text.ToString());
			}
		}));
	}
}

FString SScctOptionsPage::ValueOf(const FScctSettingDesc& Desc) const
{
	if (const FString* Edited = Edits.Find(Desc.Key))
	{
		return *Edited;
	}
	return FScctSettings::GetString(Desc.Key);
}

void SScctOptionsPage::Edit(const FScctSettingDesc& Desc, const FString& Value)
{
	const FString Normal = Normalize(Desc, Value);
	if (Normal == Normalize(Desc, FScctSettings::GetString(Desc.Key)))
	{
		Edits.Remove(Desc.Key);
	}
	else
	{
		Edits.Add(Desc.Key, Normal);
	}
}

bool SScctOptionsPage::HasNameEdit() const
{
	const FString Name = EditedName.TrimStartAndEnd();
	return Online.IsValid() && !Name.IsEmpty() && Name != PlayerName(Online.Get());
}

bool SScctOptionsPage::HasEdits() const
{
	if (Edits.Num() > 0 || HasNameEdit())
	{
		return true;
	}
	for (const TPair<FName, FScctOptionsSection>& Section : Sections())
	{
		if (Section.Value.HasEdits && Section.Value.HasEdits())
		{
			return true;
		}
	}
	return false;
}

bool SScctOptionsPage::HasApplied() const
{
	if (AppliedFrom.Num() > 0)
	{
		return true;
	}
	for (const TPair<FName, FScctOptionsSection>& Section : Sections())
	{
		if (Section.Value.HasUnsaved && Section.Value.HasUnsaved())
		{
			return true;
		}
	}
	return false;
}

bool SScctOptionsPage::HasUnsavedChanges() const
{
	return HasEdits() || HasApplied();
}

void SScctOptionsPage::Apply()
{
	const int32 Count = Edits.Num();
	for (const TPair<FName, FString>& Edited : Edits)
	{
		// What the option was at the last save, so leaving without saving can put it back.
		if (!AppliedFrom.Contains(Edited.Key))
		{
			FString Stored;
			AppliedFrom.Add(Edited.Key, GConfig != nullptr && GConfig->GetString(SettingsSection, *Edited.Key.ToString(), Stored, GGameUserSettingsIni)
				? TOptional<FString>(Stored) : TOptional<FString>());
		}
		FScctSettings::SetString(Edited.Key, Edited.Value);
	}
	Edits.Reset();

	// The name is kept by the online subsystem, which writes it at once.
	if (HasNameEdit())
	{
		Online->SetPlayerName(EditedName);
		UE_LOG(LogOpenSCCT, Log, TEXT("Options: player name is now '%s'"), *PlayerName(Online.Get()));
	}
	for (const TPair<FName, FScctOptionsSection>& Section : Sections())
	{
		if (Section.Value.Apply)
		{
			Section.Value.Apply();
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Options: applied %d change(s)"), Count);
}

void SScctOptionsPage::Save()
{
	Apply();
	FString Written;
	for (const TPair<FName, TOptional<FString>>& Applied : AppliedFrom)
	{
		Written += FString::Printf(TEXT("%s%s=%s"), Written.IsEmpty() ? TEXT("") : TEXT(", "), *Applied.Key.ToString(), *FScctSettings::GetString(Applied.Key));
	}
	FScctSettings::Save();
	for (const TPair<FName, FScctOptionsSection>& Section : Sections())
	{
		if (Section.Value.Save)
		{
			Section.Value.Save();
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Options: saved [%s] to %s (%d changed since the last save%s%s)"), SettingsSection, *GGameUserSettingsIni,
		AppliedFrom.Num(), Written.IsEmpty() ? TEXT("") : TEXT(": "), *Written);
	AppliedFrom.Reset();
}

void SScctOptionsPage::Discard()
{
	Edits.Reset();
	for (const TPair<FName, TOptional<FString>>& Applied : AppliedFrom)
	{
		if (Applied.Value.IsSet())
		{
			FScctSettings::SetString(Applied.Key, Applied.Value.GetValue());
		}
		else
		{
			FScctSettings::Reset(Applied.Key);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Options: changes thrown away (%d applied option(s) put back)"), AppliedFrom.Num());
	AppliedFrom.Reset();
	for (const TPair<FName, FScctOptionsSection>& Section : Sections())
	{
		if (Section.Value.Discard)
		{
			Section.Value.Discard();
		}
	}
	EditedName = PlayerName(Online.Get());
	NameBox->SetText(Ui::ToText(EditedName));
}

void SScctOptionsPage::ResetPage(FName Page)
{
	for (const FScctSettingDesc* Desc : FScctSettings::OptionsOf(Page))
	{
		Edit(*Desc, Desc->Default);
	}
	if (const FScctOptionsSection* Section = Sections().Find(Page); Section != nullptr && Section->ResetToDefaults)
	{
		Section->ResetToDefaults();
	}
}

void SScctOptionsPage::ResetAll()
{
	SScctConfirm::Open(Root.ToSharedRef(), TEXT("Put every option on every page back to its default? You can still leave without saving afterwards."),
		TEXT("RESET ALL"), TEXT("CANCEL"), FSimpleDelegate::CreateLambda([this]
		{
			for (const FName Page : FindPages())
			{
				ResetPage(Page);
			}
			RebuildBody();
		}));
}

void SScctOptionsPage::RequestBack()
{
	if (const TSharedPtr<SScctConfirm> Open = SScctConfirm::FindOpen(Root.ToSharedRef()))
	{
		Open->AnswerNo();
		return;
	}
	if (!HasUnsavedChanges())
	{
		Close();
		return;
	}
	SScctConfirm::Open(Root.ToSharedRef(), TEXT("Leave without saving? What you changed here goes back to how it was."),
		TEXT("LEAVE"), TEXT("STAY"), FSimpleDelegate::CreateLambda([this]
		{
			Discard();
			Close();
		}));
}

void SScctOptionsPage::Close()
{
	OnClose.ExecuteIfBound();
}

void SScctOptionsPage::RunUnattendedSwitches()
{
	FString Sets;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctSettingSet="), Sets, false))
	{
		TArray<FString> Pairs;
		Sets.ParseIntoArray(Pairs, TEXT(";"));
		for (const FString& Pair : Pairs)
		{
			FString Key, Value;
			if (Pair.Split(TEXT("="), &Key, &Value) && Key.TrimStartAndEnd() == TEXT("PlayerName"))
			{
				EditedName = Value.TrimStartAndEnd();
				NameBox->SetText(Ui::ToText(EditedName));
				UE_LOG(LogOpenSCCT, Log, TEXT("-ScctSettingSet: player name edited to '%s' (stored '%s')"), *EditedName, *PlayerName(Online.Get()));
				continue;
			}
			const FScctSettingDesc* Desc = Pair.Split(TEXT("="), &Key, &Value) ? FScctSettings::Find(FName(*Key.TrimStartAndEnd())) : nullptr;
			if (Desc == nullptr)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctSettingSet: no option '%s'"), *Pair);
				continue;
			}
			Edit(*Desc, Value.TrimStartAndEnd());
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctSettingSet: %s edited to %s (stored %s)"), *Desc->Key.ToString(), *ValueOf(*Desc), *FScctSettings::GetString(Desc->Key));
		}
	}

	FString Typed;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctOptionsSearch="), Typed))
	{
		SearchBox->SetText(Ui::ToText(Typed));
		Search = Typed.TrimStartAndEnd();
		RebuildTabs();
		RebuildBody();
	}

	static bool bSaved = false;
	if (!bSaved && FParse::Param(FCommandLine::Get(), TEXT("ScctOptionsSave")))
	{
		bSaved = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctOptionsSave: pressing SAVE"));
		Save();
	}
}
