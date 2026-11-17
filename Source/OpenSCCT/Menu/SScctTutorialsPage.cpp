// The Tutorials hub, a side's Tutorial Maps and Visit a Map (Menu/SScctTutorialsPage.h).

#include "Menu/SScctTutorialsPage.h"

#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudTextures.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctTutorialPlay.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// ---- words --------------------------------------------------------------------------------------------------------

namespace
{
	FString TutorialLocalizeRaw(const TCHAR* Section, const TCHAR* Key)
	{
		const FScctMenuApi* Menu = ScctFeature<FScctMenuApi>();
		if (Menu == nullptr)
		{
			return FString();
		}
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(4096);
		int32 Length = Menu->Localize(TEXT("GUI"), Section, Key, Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Length = Menu->Localize(TEXT("GUI"), Section, Key, Buffer.GetData(), Buffer.Num());
		}
		return Length > 0 ? FString(Buffer.GetData()) : FString();
	}

	FString TutorialBreakLines(FString Text)
	{
		Text.ReplaceInline(TEXT("|"), TEXT("\n"));
		return Text.TrimStartAndEnd();
	}
}

FString ScctTutorialText::Lines(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback)
{
	const FString Found = TutorialLocalizeRaw(Section, Key);
	return TutorialBreakLines(Found.TrimStartAndEnd().IsEmpty() ? FString(Fallback) : Found);
}

TArray<FString> ScctTutorialText::List(const TCHAR* Section, const TCHAR* Key)
{
	// TutorialText.ParseArray: the quoted items, \\ and \" unescaped.
	const FString Text = TutorialLocalizeRaw(Section, Key);
	TArray<FString> Items;
	int32 At = 0;
	while (At < Text.Len())
	{
		if (Text[At] != TEXT('"'))
		{
			++At;
			continue;
		}
		FString Item;
		++At;
		while (At < Text.Len() && Text[At] != TEXT('"'))
		{
			if (Text[At] == TEXT('\\') && At + 1 < Text.Len() && (Text[At + 1] == TEXT('\\') || Text[At + 1] == TEXT('"')))
			{
				++At;
			}
			Item.AppendChar(Text[At]);
			++At;
		}
		Items.Add(Item);
		++At;
	}
	return Items;
}

FString ScctTutorialText::Item(const TCHAR* Section, const TCHAR* Key, int32 Index)
{
	const TArray<FString> Items = List(Section, Key);
	return Items.IsValidIndex(Index) ? TutorialBreakLines(Items[Index]) : FString();
}

FString ScctTutorialText::WithKeys(const FString& Text, bool bMerc)
{
	const FScctPlayApi* Play = ScctFeature<FScctPlayApi>();
	if (Play == nullptr || !Text.Contains(TEXT("\\")))
	{
		return Text;
	}
	TArray<TCHAR> Buffer;
	Buffer.SetNumZeroed(Text.Len() * 2 + 256);
	int32 Length = Play->ExpandKeys(bMerc ? 1 : 0, *Text, Buffer.GetData(), Buffer.Num());
	if (Length >= Buffer.Num())
	{
		Buffer.SetNumZeroed(Length + 1);
		Length = Play->ExpandKeys(bMerc ? 1 : 0, *Text, Buffer.GetData(), Buffer.Num());
	}
	return Length > 0 ? FString(Buffer.GetData()) : Text;
}

namespace
{
	const TCHAR* const TutorialsSection = TEXT("Tutorials_Page");
	const TCHAR* const TutorialMapsSection = TEXT("Tuto_MapsList");
	const TCHAR* const TutorialVisitSection = TEXT("Visit_Map_Set");

	/** A GUI.int caption on one line (a button's). */
	FString TutorialCaption(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback)
	{
		return ScctTutorialText::Lines(Section, Key, Fallback).Replace(TEXT("\n"), TEXT(" "));
	}

	TSharedRef<SWidget> TutorialMenuEntry(TSharedPtr<SScctButton>& Out, const FString& Caption, const FString& Sub, const FString& Description, TFunction<void()> OnPressed)
	{
		return SAssignNew(Out, SScctButton)
			.Look(EScctButtonLook::Menu)
			.Text(FText::FromString(Caption.ToUpper()))
			.SubText(FText::FromString(Sub))
			.Description(FText::FromString(Description))
			.OnPressed(FSimpleDelegate::CreateLambda([OnPressed] { OnPressed(); }));
	}

	/** Gives the keyboard focus to Widget (after a page rebuilt its content). */
	void TutorialFocus(const TSharedPtr<SWidget>& Widget)
	{
		if (Widget.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocus(Widget, EFocusCause::SetDirectly);
		}
	}

	// ---- the hub ------------------------------------------------------------------------------------------------

	class SScctTutorialsHub : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctTutorialsHub) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			UScctTutorialSubsystem* Tutorials = UScctTutorialSubsystem::GetCurrent();
			const auto Column = [](const TArray<TSharedRef<SWidget>>& Entries)
			{
				const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
				for (const TSharedRef<SWidget>& Entry : Entries)
				{
					Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Entry ];
				}
				return Box;
			};

			TSharedPtr<SScctButton> First, Unused;
			const TSharedRef<SWidget> Reference = Column({
				TutorialMenuEntry(First, TutorialCaption(TutorialsSection, TEXT("Tuto_Hud.Caption"), TEXT("View HUD")), TEXT("What each part of the screen tells you"),
					TEXT("The spy's and the mercenary's HUD with every part named, and the two default keyboard layouts."), [this] { Host.Open(TEXT("hudlegend/spy")); }),
				TutorialMenuEntry(Unused, TutorialCaption(TutorialsSection, TEXT("Tuto_Tips.Caption"), TEXT("Tips")), TEXT("How each move and trick works, side by side"),
					TEXT("The tips of each side, each with a picture, its key points and how to do it with your own keys."), [this] { Host.Open(TEXT("tips/spy")); }),
				TutorialMenuEntry(Unused, TutorialCaption(TutorialsSection, TEXT("Tuto_Gadgets.Caption"), TEXT("Gadget Details")), TEXT("Every gadget of both sides"),
					TEXT("Each side's gadgets with their HUD icon, their specifications (format, range, duration) and how to use them."), [this] { Host.Open(TEXT("gadgets/spy")); }),
			});
			const TSharedRef<SWidget> Training = Column({
				TutorialMenuEntry(Unused, TutorialCaption(TutorialsSection, TEXT("Tuto_Maps_Spy.Caption"), TEXT("Spy Tutorial Maps")), TEXT("Four spy tutorials and the exam"),
					TEXT("Play the spy tutorial maps alone. A won tutorial is marked done and the next one starts."), [this] { Host.Open(TEXT("tutorialmaps/spy")); }),
				TutorialMenuEntry(Unused, TutorialCaption(TutorialsSection, TEXT("Tuto_Maps_Merc.Caption"), TEXT("Mercenary Tutorial Maps")), TEXT("Three mercenary tutorials"),
					TEXT("Play the mercenary tutorial maps alone. A won tutorial is marked done and the next one starts."), [this] { Host.Open(TEXT("tutorialmaps/merc")); }),
				TutorialMenuEntry(Unused, TutorialCaption(TutorialsSection, TEXT("Tuto_VisitMap.Caption"), TEXT("Visit a Map")), TEXT("Explore any map alone with help paths"),
					TEXT("Walk a map alone as either side, with the help paths shown and unlimited equipment, to learn its routes."), [this] { Host.Open(TEXT("visitmap")); }),
			});
			DefaultFocus = First;

			const FString Progress = Tutorials != nullptr ? Tutorials->GetProgressLine() : FString(TEXT("The tutorial progress needs the OpenSCCT bridge."));
			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(1200.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 14.0f, 0.0f)[ ScctUi::Card(TEXT("Reference · learn the essentials"), Reference) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f)[ ScctUi::Card(TEXT("Training · practise in the field"), Training) ]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
					[
						ScctUi::Card(TEXT("Your progress"),
							SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true).Text(FText::FromString(Progress)))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(28.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(360.0f)[ ScctUi::Card(FString(), ScctUi::DescriptionPane()) ]
				]
			];
		}

		virtual FString GetTitle() const override { return TutorialCaption(TutorialsSection, TEXT("TitlePage"), TEXT("Tutorials")); }
		virtual FString GetKicker() const override { return TEXT("Offline · learn the game"); }
	};

	// ---- a side's tutorial maps -------------------------------------------------------------------------------------

	class SScctTutorialMapsPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctTutorialMapsPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			Rebuild();
		}

		virtual FString GetTitle() const override
		{
			return Team == 0 ? TutorialCaption(TutorialMapsSection, TEXT("TitleSpy"), TEXT("Spy Tutorial Maps"))
				: TutorialCaption(TutorialMapsSection, TEXT("TitleMerc"), TEXT("Mercenary Tutorial Maps"));
		}

		virtual FString GetKicker() const override { return TEXT("Tutorials · played alone, offline"); }

		virtual void ShowSub(const FString& Sub) override
		{
			const int32 Wanted = Sub.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : 0;
			if (Wanted != Team)
			{
				Team = Wanted;
				Rebuild();
				TutorialFocus(DefaultFocus.Pin());
			}
		}

	private:
		void Rebuild()
		{
			// Tuto_MapsList.RefreshPage: the merc page shows rows 1, 3 and 4 of the page for its three maps.
			static const int32 RowOfMap[2][5] = { { 0, 1, 2, 3, 4 }, { 0, 2, 3, -1, -1 } };
			static const TCHAR* const RowFallback[] = { TEXT("Movements and Basic Actions"), TEXT("Enhanced Reality"), TEXT("Objectives"), TEXT("Alarms, Isolations, and Gadgets"), TEXT("Exam") };

			UScctTutorialSubsystem* Tutorials = UScctTutorialSubsystem::GetCurrent();
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const TArray<FString> Maps = Tutorials != nullptr ? Tutorials->GetMaps(Team) : TArray<FString>();
			const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
			TSharedPtr<SScctButton> First;
			for (int32 Index = 0; Index < Maps.Num(); ++Index)
			{
				const FString Map = Maps[Index];
				const int32 Row = Index < 5 ? RowOfMap[Team][Index] : -1;
				const FString Title = Row >= 0
					? TutorialCaption(TutorialMapsSection, *FString::Printf(TEXT("TutoMap%d.Caption"), Row + 1), RowFallback[Row])
					: (Menu != nullptr ? Menu->GetMapTitle(Map) : Map);
				const bool bDone = Tutorials != nullptr && Tutorials->IsDone(Team, Index);
				const bool bPlayable = UScctTutorialSubsystem::CanPlay(Map);
				const FString Sub = !bPlayable
					? FString::Printf(TEXT("%s · not imported (Tools\\Import\\Import-Map.ps1 -Map %s)"), *Map, *Map)
					: FString::Printf(TEXT("%s · %s"), *Map, bDone ? TEXT("DONE") : TEXT("not done yet"));
				FString Briefing = Menu != nullptr ? Menu->GetModeText(Map, 0, Team == 1, 1) : FString();
				if (Briefing.TrimStartAndEnd().IsEmpty() && Menu != nullptr)
				{
					Briefing = Menu->GetMapText(Map, EScctMenuMapText::Description);
				}
				Briefing = Briefing.Replace(TEXT("|"), TEXT(" ")).TrimStartAndEnd();
				const FString Description = FString::Printf(TEXT("%s%s%s"), *Briefing, Briefing.IsEmpty() ? TEXT("") : TEXT("\n\n"),
					bPlayable ? (bDone ? TEXT("Done. Play it again at any time.") : TEXT("Win it to mark it done; the next tutorial then starts by itself."))
					: TEXT("This map's level is not imported into the project, so it cannot be played here yet."));

				TSharedPtr<SScctButton> Made;
				const int32 TeamOfRow = Team;
				Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						// The original's tick: done or not.
						SNew(SBox).WidthOverride(26.0f).HAlign(HAlign_Center)
						[
							SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(bDone ? ScctUi::Accent() : ScctHudStyle::Fade(ScctHudStyle::TextDim(), 0.5f))
							.Text(FText::FromString(bDone ? TEXT("✓") : TEXT("•")))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SAssignNew(Made, SScctButton)
						.Look(EScctButtonLook::Menu)
						.IsEnabled(bPlayable)
						.Text(FText::FromString(Title.ToUpper()))
						.SubText(FText::FromString(Sub))
						.Description(FText::FromString(Description))
						.OnPressed(FSimpleDelegate::CreateLambda([Map, TeamOfRow]
						{
							if (UScctTutorialSubsystem* Play = UScctTutorialSubsystem::GetCurrent())
							{
								Play->PlayTutorial(Map, TeamOfRow);
							}
						}))
					]
				];
				if (!First.IsValid() && bPlayable)
				{
					First = Made;
				}
			}
			if (Maps.IsEmpty())
			{
				Rows->AddSlot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text(FText::FromString(TEXT("The tutorial maps need the OpenSCCT bridge.")))
				];
			}

			TSharedPtr<SScctButton> Back, Other;
			const TSharedRef<SWidget> Buttons = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					SAssignNew(Back, SScctButton)
					.Text(FText::FromString(TEXT("BACK")))
					.Description(FText::FromString(TEXT("Back to the Tutorials page.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { if (Host.Back) { Host.Back(); } }))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(Other, SScctButton)
					.Text(FText::FromString((Team == 0 ? TutorialCaption(TutorialMapsSection, TEXT("TitleMerc"), TEXT("Mercenary Tutorial Maps")) : TutorialCaption(TutorialMapsSection, TEXT("TitleSpy"), TEXT("Spy Tutorial Maps"))).ToUpper()))
					.Description(FText::FromString(TEXT("The other side's tutorial maps.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { ShowSub(Team == 0 ? TEXT("merc") : TEXT("spy")); }))
				];
			DefaultFocus = First.IsValid() ? First : Back;

			UScctTutorialSubsystem* Progress = UScctTutorialSubsystem::GetCurrent();
			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(760.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						ScctUi::Card(Team == 0 ? TEXT("Spy training") : TEXT("Mercenary training"), SNew(SScrollBox) + SScrollBox::Slot()[ Rows ])
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
						.Text(FText::FromString(Progress != nullptr ? Progress->GetProgressLine() : FString()))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 16.0f, 0.0f, 0.0f)[ Buttons ]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(28.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(420.0f)[ ScctUi::Card(FString(), ScctUi::DescriptionPane()) ]
				]
			];
		}

		int32 Team = 0;
	};

	// ---- Visit a Map --------------------------------------------------------------------------------------------------

	/** The last map and side the page launched, for the next time it opens (the page's own memory in Godot). */
	FString TutorialVisitLastMap;
	bool bTutorialVisitLastMerc = false;

	class SScctVisitMapPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctVisitMapPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			bMerc = bTutorialVisitLastMerc;
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();

			// The host list's maps that play Story and can be played here; AquaE first selected, as the page does.
			int32 StoryIndex = 0;
			for (int32 Mode = 0; Mode < 4; ++Mode)
			{
				if (FCString::Stricmp(UScctMenuSubsystem::ModeKey(Mode), TEXT("Story")) == 0)
				{
					StoryIndex = Mode;
				}
			}
			if (Menu != nullptr)
			{
				for (const FScctMenuMapInfo& Info : Menu->GetMaps())
				{
					if (Info.Plays(StoryIndex))
					{
						Maps.Add(Info.Id);
					}
				}
			}
			const auto Has = [this](const FString& Id) { return Maps.ContainsByPredicate([&Id](const FString& Map) { return Map.Equals(Id, ESearchCase::IgnoreCase); }); };
			Selected = Has(TutorialVisitLastMap) ? TutorialVisitLastMap : Has(TEXT("AquaE")) ? FString(TEXT("AquaE")) : Maps.Num() > 0 ? Maps[0] : FString();

			const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
			for (const FString& Map : Maps)
			{
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SScctButton)
					.Look(EScctButtonLook::Chip)
					.FillWidth(true)
					.Selected(TAttribute<bool>::CreateLambda([this, Map] { return Selected.Equals(Map, ESearchCase::IgnoreCase); }))
					.Text(FText::FromString(Menu != nullptr ? Menu->GetMapTitle(Map) : Map))
					.Description(FText::FromString(FString::Printf(TEXT("Visit %s alone with the help paths shown."), Menu != nullptr ? *Menu->GetMapTitle(Map) : *Map)))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Map] { Selected = Map; }))
				];
			}
			if (Maps.IsEmpty())
			{
				List->AddSlot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text(FText::FromString(TEXT("No map to visit: convert your game with OpenSCCT, then import a map (Tools\\Import\\Import-Map.ps1).")))
				];
			}

			TSharedPtr<SScctButton> Launch;
			ChildSlot
			[
				SNew(SHorizontalBox)
				// The side: its picture (TeamInfo.MaterialOfMesh[0], the equipment page's portrait) and Switch Team.
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 14.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(280.0f)
					[
						ScctUi::Card(TEXT("Your role"),
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
							[
								SNew(SBox).WidthOverride(192.0f).HeightOverride(240.0f)
								[
									SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
									[
										SNew(SImage).Image(TAttribute<const FSlateBrush*>::CreateLambda([this]
										{
											const UScctHudTextures::FEntry* Portrait = UScctHudTextures::Find(TEXT("GUIContent"), bMerc ? TEXT("MN_DEFperso") : TEXT("MN_ATTperso"));
											return Portrait != nullptr ? &Portrait->Brush : nullptr;
										}))
									]
								]
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 12.0f, 0.0f, 6.0f)
							[
								SNew(STextBlock).Font(ScctUi::H2())
								.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(ScctHudStyle::Accent(bMerc ? 1 : 0)); }))
								.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(bMerc ? TEXT("MERCENARY") : TEXT("SPY")); }))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
							[
								SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Justification(ETextJustify::Center)
								.Text(FText::FromString(TEXT("Explore alone with help paths and unlimited equipment.")))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SScctButton)
								.FillWidth(true)
								.Text(FText::FromString(TutorialCaption(TutorialVisitSection, TEXT("ButtonY"), TEXT("Switch Team")).ToUpper()))
								.Description(FText::FromString(TEXT("Visit as the other side.")))
								.OnPressed(FSimpleDelegate::CreateLambda([this] { SwitchTeam(); }))
							])
					]
				]
				// The maps.
				+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(420.0f).Padding(0.0f, 0.0f, 14.0f, 0.0f)
				[
					ScctUi::Card(TutorialCaption(TutorialVisitSection, TEXT("ImageMap.Caption"), TEXT("Choose Map")),
						SNew(SBox).MaxDesiredHeight(560.0f)[ SNew(SScrollBox) + SScrollBox::Slot().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ List ] ])
				]
				// The map picked and LAUNCH.
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(440.0f)
					[
						ScctUi::Card(TEXT("Map"),
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SBox).WidthOverride(392.0f).HeightOverride(196.0f)
								[
									SNew(SScaleBox).Stretch(EStretch::ScaleToFill)
									[
										SNew(SImage).Image(TAttribute<const FSlateBrush*>::CreateLambda([this]
										{
											UScctMenuSubsystem* Pictures = UScctMenuSubsystem::GetCurrent();
											return Pictures != nullptr && !Selected.IsEmpty() ? Pictures->GetMapPicture(Selected, bMerc) : nullptr;
										}))
									]
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 4.0f)
							[
								SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
								.Text(TAttribute<FText>::CreateLambda([this]
								{
									UScctMenuSubsystem* Titles = UScctMenuSubsystem::GetCurrent();
									return FText::FromString(Selected.IsEmpty() ? FString() : Titles != nullptr ? Titles->GetMapTitle(Selected) : Selected);
								}))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
							[
								SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
								.Text(FText::FromString(TEXT("Story mode, no bots, no time limit, unlimited lives. The help paths show where to go and what can be done.")))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
								[
									SNew(SScctButton)
									.Text(FText::FromString(TEXT("BACK")))
									.Description(FText::FromString(TEXT("Back to the Tutorials page.")))
									.OnPressed(FSimpleDelegate::CreateLambda([this] { if (Host.Back) { Host.Back(); } }))
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SAssignNew(Launch, SScctButton)
									.Look(EScctButtonLook::Primary)
									.FillWidth(true)
									.IsEnabled(TAttribute<bool>::CreateLambda([this] { return !Selected.IsEmpty(); }))
									.Text(FText::FromString(TutorialCaption(TutorialVisitSection, TEXT("ButtonA"), TEXT("Launch")).ToUpper()))
									.Description(FText::FromString(TEXT("Start the visit of the map picked, as the side shown.")))
									.OnPressed(FSimpleDelegate::CreateLambda([this] { LaunchVisit(); }))
								]
							])
					]
				]
			];
			DefaultFocus = Launch;
		}

		virtual FString GetTitle() const override { return TutorialCaption(TutorialVisitSection, TEXT("TitlePage"), TEXT("Visit a Map")); }
		virtual FString GetKicker() const override { return TEXT("Tutorials · help paths on"); }

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			OutKeyAndWhat.Add({ TEXT("T"), TutorialCaption(TutorialVisitSection, TEXT("ButtonY"), TEXT("Switch Team")) });
			OutPadKeys.Add(TEXT("Y"));
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (Key == EKeys::T || Key == EKeys::Gamepad_FaceButton_Top)
			{
				SwitchTeam();
				return true;
			}
			return false;
		}

	private:
		void SwitchTeam()
		{
			bMerc = !bMerc;
			bTutorialVisitLastMerc = bMerc;
			ScctUi::Play(EScctUiSound::Accept);
		}

		void LaunchVisit()
		{
			if (Selected.IsEmpty())
			{
				return;
			}
			TutorialVisitLastMap = Selected;
			bTutorialVisitLastMerc = bMerc;
			if (UScctTutorialSubsystem* Play = UScctTutorialSubsystem::GetCurrent())
			{
				Play->Visit(Selected, bMerc);
			}
		}

		TArray<FString> Maps;
		FString Selected;
		bool bMerc = false;
	};
}

TSharedPtr<SScctMenuPage> ScctTutorialPages::Make(const FString& Name, const FScctMenuHost& Host)
{
	if (Name.Equals(TEXT("tutorials"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("tutorial"), ESearchCase::IgnoreCase)) { return MakeTutorials(Host); }
	if (Name.Equals(TEXT("tutorialmaps"), ESearchCase::IgnoreCase)) { return MakeTutorialMaps(Host); }
	if (Name.Equals(TEXT("visitmap"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("visit"), ESearchCase::IgnoreCase)) { return MakeVisitMap(Host); }
	if (Name.Equals(TEXT("hudlegend"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("viewhud"), ESearchCase::IgnoreCase)) { return MakeHudLegend(Host); }
	if (Name.Equals(TEXT("tips"), ESearchCase::IgnoreCase)) { return MakeTips(Host); }
	if (Name.Equals(TEXT("gadgets"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("gadgetdetails"), ESearchCase::IgnoreCase)) { return MakeGadgets(Host); }
	return nullptr;
}

TSharedRef<SScctMenuPage> ScctTutorialPages::MakeTutorials(const FScctMenuHost& Host)
{
	return SNew(SScctTutorialsHub, Host);
}

TSharedRef<SScctMenuPage> ScctTutorialPages::MakeTutorialMaps(const FScctMenuHost& Host)
{
	return SNew(SScctTutorialMapsPage, Host);
}

TSharedRef<SScctMenuPage> ScctTutorialPages::MakeVisitMap(const FScctMenuHost& Host)
{
	return SNew(SScctVisitMapPage, Host);
}
