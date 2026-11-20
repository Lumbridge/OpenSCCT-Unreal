// Play offline: the map list on the left, the chosen map in the middle, the match's settings on the right.

#include "Menu/ScctMenuPage.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctBotNavPrompt.h"
#include "Menu/ScctMapFamilies.h"
#include "Menu/ScctMenuNav.h"
#include "Menu/SScctGearPage.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const PlaySection = TEXT("OpenSCCT.Play");
	const TCHAR* const PlaySkillKeys[] = { TEXT("Easy"), TEXT("Normal"), TEXT("Hard"), TEXT("Expert") };
	const int32 PlayTimeChoices[] = { -1, 0, 3, 5, 8, 10, 15, 20, 30, 45, 60 };
	const int32 PlayLivesChoices[] = { -1, 0, 1, 2, 3, 4, 5, 6, 8, 10 };
	const int32 PlayRespawnChoices[] = { -1, 1, 2, 3, 5, 8, 10, 15 };
	// The match-size rules' range (MatchRuleSet.MostPlayers is 24); 0 is the match's own count.
	constexpr int32 PlayMatchPlayersMin = 2;
	constexpr int32 PlayMatchPlayersMax = 24;

	class SScctMenuPlay : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuPlay) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			Load();

			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const TArray<FScctMenuMapInfo> Maps = Menu != nullptr ? Menu->GetMaps() : TArray<FScctMenuMapInfo>();
			for (const FScctMenuMapInfo& Map : Maps)
			{
				MapIds.Add(Map.Id);
			}
			MapIndex = FMath::Max(MapIds.IndexOfByKey(SavedMap), 0);
			if (MapIds.IsValidIndex(MapIndex) && SavedMap.IsEmpty())
			{
				// AquaE first when it is there: the map everything was first seen on.
				MapIndex = FMath::Max(MapIds.IndexOfByPredicate([](const FString& Id) { return Id.Equals(TEXT("AquaE"), ESearchCase::IgnoreCase); }), 0);
			}
			SelectDefaultMode();

			if (MapIds.IsEmpty())
			{
				TSharedPtr<SScctButton> BackButton;
				ChildSlot.HAlign(HAlign_Left).VAlign(VAlign_Top)
				[
					SNew(SBox).WidthOverride(640.0f)
					[
						ScctUi::Card(TEXT("No map to play"),
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 22.0f)
							[
								SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).LineHeightPercentage(1.2f)
								.Text(FText::FromString(TEXT("No map can be played here yet. Either the OpenSCCT content folder was not found, or no map has been imported into this game.\n\nConvert your copy of the game with OpenSCCT, then import a map: Tools\\Import\\Import-Map.ps1 -Map AquaE -Content <content folder> (see the README).")))
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
							[
								SAssignNew(BackButton, SScctButton).Text(FText::FromString(TEXT("BACK"))).MinWidth(140.0f).OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Back(); }))
							])
					]
				];
				DefaultFocus = BackButton;
				return;
			}

			const TArray<FScctMapFamily> Families = ScctMapFamilies::Build(Maps);
			TSharedPtr<SScctButton> StartButton;
			ChildSlot
			[
				SNew(SHorizontalBox)
				// The maps.
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(390.0f)
					[
						ScctUi::Card(FString::Printf(TEXT("Maps · %d"), Families.Num()), BuildMapList(Families), FMargin(12.0f, 18.0f, 6.0f, 12.0f))
					]
				]
				// The chosen map.
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					SAssignNew(HeroBox, SBox)
				]
				// The match's settings.
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(480.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							ScctUi::Panel(
								SNew(SScrollBox)
								.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
								+ SScrollBox::Slot()[ BuildOptions() ],
								FMargin(12.0f, 6.0f, 6.0f, 12.0f), 0.7f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
							[
								SNew(SScctButton).Text(FText::FromString(TEXT("BACK"))).MinWidth(120.0f).OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Back(); }))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SAssignNew(StartButton, SScctButton)
								.Look(EScctButtonLook::Primary)
								.FillWidth(true)
								.Text(FText::FromString(TEXT("START MATCH")))
								.Description(FText::FromString(TEXT("Load the map and play with these settings.")))
								.OnPressed(FSimpleDelegate::CreateLambda([this] { StartMatch(); }))
							]
						]
					]
				]
			];
			RefreshHero();
		}

		virtual FString GetTitle() const override { return TEXT("Play offline"); }
		virtual FString GetKicker() const override { return TEXT("Skirmish · against bots"); }

		virtual TSharedPtr<SWidget> GetDefaultFocus() const override
		{
			if (!MapRows.IsValidIndex(MapIndex))
			{
				return SScctMenuPage::GetDefaultFocus();
			}
			const TSharedPtr<SExpandableArea> Family = MapRows[MapIndex].Family.Pin();
			return Family.IsValid() && !Family->IsExpanded() ? ScctMenuNav::FindFirstFocusable(Family) : MapRows[MapIndex].Item.Pin();
		}

		virtual void ShowSub(const FString& Sub) override
		{
			if (Sub.Equals(TEXT("start"), ESearchCase::IgnoreCase))
			{
				StartMatch();
			}
			else
			{
				// A map by its folder name.
				const int32 Found = MapIds.IndexOfByPredicate([&Sub](const FString& Id) { return Id.Equals(Sub, ESearchCase::IgnoreCase); });
				if (Found != INDEX_NONE)
				{
					if (const TSharedPtr<SExpandableArea> Family = MapRows[Found].Family.Pin())
					{
						Family->SetExpanded(true);
					}
					SelectMap(Found);
					if (HasFocusedDescendants())
					{
						FSlateApplication::Get().SetKeyboardFocus(GetDefaultFocus(), EFocusCause::SetDirectly);
					}
				}
			}
		}

		virtual void OnClosing() override { Save(); }

		/**
		 * Across the page: Right from the map list goes to the match settings, Left from the settings (a control that
		 * does not take Left itself, as a stepper does) back to the chosen map. Up and down stay in their column.
		 */
		virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override
		{
			const EUINavigation Type = InNavigationEvent.GetNavigationType();
			const TSharedPtr<SWidget> List = MapList.Pin();
			const bool bInList = List.IsValid() && List->HasFocusedDescendants();
			if (Type == EUINavigation::Right && bInList && FirstOption.IsValid())
			{
				return FNavigationReply::Explicit(FirstOption.Pin());
			}
			if (Type == EUINavigation::Left && !bInList && MapRows.IsValidIndex(MapIndex))
			{
				return FNavigationReply::Explicit(GetDefaultFocus());
			}
			return SScctMenuPage::OnNavigation(MyGeometry, InNavigationEvent);
		}

	private:
		// ---- what is chosen ---------------------------------------------------------------------------------------

		const FScctMenuMapInfo* GetMap() const
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			return Menu != nullptr && MapIds.IsValidIndex(MapIndex) ? Menu->FindMap(MapIds[MapIndex]) : nullptr;
		}

		/** The modes the chosen map plays, by index of the original's mode box. */
		TArray<int32> GetModes() const
		{
			TArray<int32> Modes;
			const FScctMenuMapInfo* Map = GetMap();
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (Map == nullptr ? Index == 0 : Map->Plays(Index))
				{
					Modes.Add(Index);
				}
			}
			if (Modes.IsEmpty())
			{
				Modes.Add(0);
			}
			return Modes;
		}

		void SelectDefaultMode()
		{
			const FScctMenuMapInfo* Map = GetMap();
			ModeIndex = Map != nullptr ? Map->DefaultModeIndex() : 0;
		}

		const FScctMenuMode* GetModeRules() const
		{
			const FScctMenuMapInfo* Map = GetMap();
			return Map != nullptr ? &Map->Data.Modes[FMath::Clamp(ModeIndex, 0, 3)] : nullptr;
		}

		bool IsMercDeathmatch() const { return ModeIndex == 3; }

		void SelectMap(int32 Index)
		{
			if (Index != MapIndex && MapIds.IsValidIndex(Index))
			{
				MapIndex = Index;
				SelectDefaultMode();
				RefreshHero();
			}
		}

		// ---- the map list -----------------------------------------------------------------------------------------

		TSharedRef<SWidget> MapRowContent(int32 Index, const FString& Title, const FString& Detail, bool bShowPicture) const
		{
			const TSharedRef<SVerticalBox> Label = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Text(FText::FromString(Title), ScctUi::Body(true), FSlateColor(ScctHudStyle::Text()), true) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[ ScctUi::Text(FText::FromString(Detail), ScctUi::Small(), FSlateColor(ScctHudStyle::TextDim()), true) ];
			if (!bShowPicture)
			{
				return Label;
			}
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const FSlateBrush* Thumb = Menu->GetPicture(Menu->GetMapText(MapIds[Index], EScctMenuMapText::PreviewImage));
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[ ScctUi::Picture(Thumb != nullptr ? Thumb : Menu->GetMapPicture(MapIds[Index], false), 88.0f, 50.0f) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Label ];
		}

		TSharedRef<SWidget> BuildMapItem(int32 Index, const FString& Title, bool bShowPicture)
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const FScctMenuMapInfo* Map = Menu->FindMap(MapIds[Index]);
			TArray<FString> PlayableModeNames;
			for (int32 Mode = 0; Map != nullptr && Mode < 4; ++Mode)
			{
				if (Map->Plays(Mode))
				{
					PlayableModeNames.Add(Menu->GetModeName(Mode));
				}
			}
			FString Modes = PlayableModeNames.Num() == 4 ? FString(TEXT("Every mode")) : FString::Join(PlayableModeNames, TEXT(" · "));
			if (Map != nullptr && Map->Data.RecommendedPlayers > 0)
			{
				Modes += FString::Printf(TEXT("%s%d players"), Modes.IsEmpty() ? TEXT("") : TEXT("  ·  "), Map->Data.RecommendedPlayers);
			}
			const TSharedRef<SScctListItem> Item = SNew(SScctListItem)
				.Padding(FMargin(8.0f, 6.0f))
				.Selected_Lambda([this, Index] { return MapIndex == Index; })
				.OnFocused(FSimpleDelegate::CreateLambda([this, Index]
				{
					SelectMap(Index);
					ScctUi::Describe(FText::FromString(MapTitle()), FText::FromString(TEXT("Enter or A: on to the match settings.")));
				}))
				.OnActivated(FSimpleDelegate::CreateLambda([this] { if (FirstOption.IsValid()) { FSlateApplication::Get().SetKeyboardFocus(FirstOption.Pin(), EFocusCause::Navigation); } }))
				[ MapRowContent(Index, Title, Modes, bShowPicture) ];
			MapRows[Index].Item = Item;
			return Item;
		}

		TSharedRef<SWidget> BuildMapList(const TArray<FScctMapFamily>& Families)
		{
			MapRows.SetNum(MapIds.Num());
			const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
			for (const FScctMapFamily& Family : Families)
			{
				const int32 First = Family.Variants[0].MapIndex;
				const FMargin Spacing(0.0f, 0.0f, 6.0f, 4.0f);
				if (Family.Variants.Num() == 1)
				{
					List->AddSlot().AutoHeight().Padding(Spacing)[ BuildMapItem(First, Family.Title, true) ];
					continue;
				}

				const TSharedRef<SVerticalBox> Variants = SNew(SVerticalBox);
				for (const FScctMapVariant& Variant : Family.Variants)
				{
					Variants->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[ BuildMapItem(Variant.MapIndex, Variant.Label, false) ];
				}
				const TSharedRef<SExpandableArea> Area = SNew(SExpandableArea)
					.InitiallyCollapsed(true)
					.AllowAnimatedTransition(false)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor_Lambda([this, First]
					{
						return FSlateColor(MapRows[MapIndex].Family == MapRows[First].Family
							? ScctHudStyle::Fade(ScctUi::Accent(), 0.16f) : ScctUi::White(0.035f));
					})
					.BodyBorderImage(FStyleDefaults::GetNoBrush())
					.HeaderPadding(FMargin(8.0f, 6.0f))
					.AreaTitlePadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
					.Padding(FMargin(28.0f, 0.0f, 0.0f, 2.0f))
					.HeaderContent()[ MapRowContent(First, Family.Title, FString::Printf(TEXT("%d variants"), Family.Variants.Num()), true) ]
					.BodyContent()[ Variants ];
				for (const FScctMapVariant& Variant : Family.Variants)
				{
					MapRows[Variant.MapIndex].Family = Area;
				}
				List->AddSlot().AutoHeight().Padding(Spacing)[ Area ];
			}
			const TSharedRef<SScrollBox> Scroll = SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[ List ];
			MapList = Scroll;
			return Scroll;
		}

		FString MapTitle() const
		{
			const FScctMenuMapInfo* Map = GetMap();
			return Map != nullptr ? Map->Title : FString();
		}

		// ---- the chosen map ---------------------------------------------------------------------------------------

		void RefreshHero()
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			if (!HeroBox.IsValid() || Menu == nullptr || !MapIds.IsValidIndex(MapIndex))
			{
				return;
			}
			const FString Id = MapIds[MapIndex];
			// The legend's lines ("11 March 2008, 9:58 PM", "France, ...") with the empty ones left out.
			TArray<FString> LegendLines;
			Menu->GetMapText(Id, EScctMenuMapText::Legend).ParseIntoArray(LegendLines, TEXT("\n"));
			for (FString& Line : LegendLines) { Line.TrimStartAndEndInline(); }
			// A legend line is a date, a place or a building; a longer one is a sentence of the briefing below, said twice.
			LegendLines.RemoveAll([](const FString& Line) { return Line.IsEmpty() || Line.Len() > 56; });
			const FString Legend = FString::Join(LegendLines, TEXT("   ·   "));
			// Most maps have no list description: the side's briefing says what the map is.
			FString Description = Menu->GetMapText(Id, EScctMenuMapText::Description);
			if (Description.TrimStartAndEnd().IsEmpty())
			{
				Description = Menu->GetMapText(Id, bMerc ? EScctMenuMapText::BriefMerc : EScctMenuMapText::BriefSpy);
			}
			Description = Description.Replace(TEXT("|"), TEXT(" ")).TrimStartAndEnd();
			const FScctMenuMapInfo* Map = GetMap();
			const FString Players = Map != nullptr && Map->Data.RecommendedPlayers > 0 ? FString::Printf(TEXT("%d PLAYERS RECOMMENDED"), Map->Data.RecommendedPlayers) : FString();

			HeroBox->SetContent(
				ScctUi::Panel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[ ScctUi::Picture(Menu->GetMapPicture(Id, bMerc), 0.0f, 300.0f) ]
						+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Left).Padding(18.0f, 0.0f, 0.0f, 14.0f)
						[
							ScctUi::Label(Players, ScctHudStyle::Text(), 11.0f)
						]
					]
					// Below the picture the text scrolls, so a long briefing on a small screen is never cut off.
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(28.0f, 22.0f, 28.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 32.0f, 120)).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).Text(FText::FromString(MapTitle()))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(28.0f, 6.0f, 28.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctUi::Caps(11.5f)).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true).Text(FText::FromString(Legend.ToUpper()))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(28.0f, 16.0f, 28.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).LineHeightPercentage(1.18f).Text(FText::FromString(Description))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(28.0f, 20.0f, 28.0f, 0.0f)[ ScctUi::Rule(0.1f) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(28.0f, 16.0f, 28.0f, 24.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock).Font(ScctUi::Caps(11.5f)).ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(ScctHudStyle::Accent(bMerc ? 1 : 0)); }))
							.Text(TAttribute<FText>::CreateLambda([this]
							{
								UScctMenuSubsystem* M = UScctMenuSubsystem::GetCurrent();
								return FText::FromString(FString::Printf(TEXT("%s · %s"), M != nullptr ? *M->GetModeName(ModeIndex).ToUpper() : TEXT(""),
									*ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), bMerc ? 1 : 0, bMerc ? TEXT("Mercenary") : TEXT("Spy")).ToUpper()));
							}))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
							.Text(TAttribute<FText>::CreateLambda([this]
							{
								UScctMenuSubsystem* M = UScctMenuSubsystem::GetCurrent();
								const FString Objective = M != nullptr && MapIds.IsValidIndex(MapIndex) ? M->GetModeText(MapIds[MapIndex], ModeIndex, bMerc, 0).Replace(TEXT("|"), TEXT(" ")) : FString();
								return FText::FromString(Objective.IsEmpty() ? TEXT("The map gives no objective text for this mode.") : Objective);
							}))
						]
					]
					]
					],
					FMargin(0.0f), 0.74f));
		}

		// ---- the settings -----------------------------------------------------------------------------------------

		static FString Choice(int32 Value, int32 MapValue, const TCHAR* Unit, const TCHAR* Zero)
		{
			const auto Shown = [Unit, Zero](int32 V) { return V == 0 ? FString(Zero) : FString::Printf(TEXT("%d%s"), V, Unit); };
			return Value < 0 ? FString::Printf(TEXT("Map's (%s)"), *Shown(MapValue)) : Shown(Value);
		}

		template <int32 N>
		static int32 IndexIn(const int32 (&Values)[N], int32 Value)
		{
			for (int32 Index = 0; Index < N; ++Index)
			{
				if (Values[Index] == Value) { return Index; }
			}
			return 0;
		}

		TSharedRef<SWidget> BuildOptions()
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
			const auto Add = [&Rows](const TSharedRef<SWidget>& Row) { Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row ]; };

			Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Match")) ];
			TSharedPtr<SWidget> First;
			Add(SAssignNew(First, SScctStepper)
				.Label(FText::FromString(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("Mode.Caption"), TEXT("Game Mode"))))
				.Description(FText::FromString(TEXT("Story: the spies take the map's objectives. Disk Hunt: the spies carry disks out. Deathmatch: the sides fight. Merc Deathmatch: every player is a mercenary. Only the modes this map has are offered.")))
				.GetOptions([this] { TArray<FString> Names; UScctMenuSubsystem* M = UScctMenuSubsystem::GetCurrent(); for (const int32 Mode : GetModes()) { Names.Add(M != nullptr ? M->GetModeName(Mode) : FString()); } return Names; })
				.Get([this] { return GetModes().IndexOfByKey(ModeIndex); })
				.Set([this](int32 Value) { const TArray<int32> Modes = GetModes(); if (Modes.IsValidIndex(Value)) { ModeIndex = Modes[Value]; if (IsMercDeathmatch()) { bMerc = true; } } }));
			FirstOption = First;
			Add(SNew(SScctStepper)
				.Label(FText::FromString(TEXT("Side")))
				.Description(FText::FromString(TEXT("Play a spy (third person, stealth, gadgets) or a mercenary (first person, rifle, defence). Merc Deathmatch is mercenaries only.")))
				.Options({ ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 0, TEXT("Spy")), ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 1, TEXT("Mercenary")) })
				.Get([this] { return bMerc ? 1 : 0; })
				.Set([this](int32 Value) { bMerc = Value == 1 || IsMercDeathmatch(); RefreshHero(); }));
			// The side's saved kit, and the Equipment page to change it (Menu/ScctOverlayPages.h).
			Add(SNew(SScctButton)
				.Look(EScctButtonLook::Menu)
				.Text(FText::FromString(TEXT("EQUIPMENT")))
				.SubText_Lambda([this] { return FText::FromString(SScctGearPage::SummaryLine(bMerc ? 1 : 0)); })
				.Description(FText::FromString(TEXT("Choose the gadgets and weapons you take into the match.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(bMerc ? TEXT("gear/merc") : TEXT("gear/spy")); })));

			Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Bots")) ];
			Add(SNew(SScctSlider)
				.Label(FText::FromString(TEXT("Bots")))
				.Description(FText::FromString(TEXT("How many bots play with you, shared out between the two sides. 0 plays alone.")))
				.Min(0.0f).Max(15.0f).Step(1.0f)
				.Get([this] { return static_cast<float>(Bots); })
				.Set([this](float Value) { Bots = FMath::RoundToInt32(Value); })
				.Format([](float Value) { return Value <= 0.0f ? FString(TEXT("NONE")) : FString::FromInt(FMath::RoundToInt32(Value)); }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(TEXT("Bot skill")))
				.Description(FText::FromString(TEXT("Easy bots are slow to notice and miss often; Expert bots hunt, flank and aim like good players.")))
				.Options({ TEXT("Easy"), TEXT("Normal"), TEXT("Hard"), TEXT("Expert") })
				.Wrap(false)
				.Get([this] { return Skill; })
				.Set([this](int32 Value) { Skill = Value; }));

			Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Rules")) ];
			Add(SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("TimeLimitCaption"), TEXT("Game Length (min)"))))
				.Description(FText::FromString(TEXT("The round's length. \"Map's\" keeps what the map sets for this mode.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayTimeChoices) { Out.Add(Choice(V, R != nullptr ? R->TimeLimit : 0, TEXT(" min"), TEXT("No limit"))); } return Out; })
				.Get([this] { return IndexIn(PlayTimeChoices, TimeLimit); })
				.Set([this](int32 Value) { TimeLimit = PlayTimeChoices[Value]; }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("SpyLivesCaption"), TEXT("Spy Lives"))))
				.Description(FText::FromString(TEXT("Lives of each spy. Not used in Merc Deathmatch.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayLivesChoices) { Out.Add(Choice(V, R != nullptr ? R->SpyLives : 0, TEXT(""), TEXT("Unlimited"))); } return Out; })
				.Get([this] { return IndexIn(PlayLivesChoices, SpyLives); })
				.Set([this](int32 Value) { SpyLives = PlayLivesChoices[Value]; }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("MercLivesCaption"), TEXT("Mercenary Lives"))))
				.Description(FText::FromString(TEXT("Lives of each mercenary.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayLivesChoices) { Out.Add(Choice(V, R != nullptr ? R->MercLives : 0, TEXT(""), TEXT("Unlimited"))); } return Out; })
				.Get([this] { return IndexIn(PlayLivesChoices, MercLives); })
				.Set([this](int32 Value) { MercLives = PlayLivesChoices[Value]; }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("RespawnCaption"), TEXT("Respawn Length (sec)"))))
				.Description(FText::FromString(TEXT("Seconds between a death and the next life.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayRespawnChoices) { Out.Add(Choice(V, R != nullptr ? R->RespawnSeconds : 0, TEXT(" s"), TEXT("0 s"))); } return Out; })
				.Get([this] { return IndexIn(PlayRespawnChoices, Respawn); })
				.Set([this](int32 Value) { Respawn = PlayRespawnChoices[Value]; }));
			Add(SNew(SScctToggle)
				.Label(FText::FromString(TEXT("Practice rules")))
				.Description(FText::FromString(TEXT("No time limit, unlimited lives and a 5 second respawn, unless set above, with unlimited ammunition, gadgets and mask air, instant gear changes and Switch Team in the match menu. For learning a map.")))
				.Get([this] { return bPractice; })
				.Set([this](bool bValue) { bPractice = bValue; }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(TEXT("Match size")))
				.Description(FText::FromString(TEXT("The player count the map lays its objectives, doors and starts out for (its match-size rules). Auto: the bots and you. A larger count plays a small match on the full map.")))
				.Wrap(false)
				.GetOptions([this]
				{
					TArray<FString> Out;
					Out.Add(FString::Printf(TEXT("Auto (%d)"), FMath::Max(Bots, 0) + 1));
					for (int32 Count = PlayMatchPlayersMin; Count <= PlayMatchPlayersMax; ++Count) { Out.Add(FString::Printf(TEXT("%d players"), Count)); }
					return Out;
				})
				.Get([this] { return MatchPlayers <= 0 ? 0 : MatchPlayers - PlayMatchPlayersMin + 1; })
				.Set([this](int32 Value) { MatchPlayers = Value <= 0 ? 0 : Value + PlayMatchPlayersMin - 1; }));
			return Rows;
		}

		// ---- starting ---------------------------------------------------------------------------------------------

		/** START MATCH: with bots, the map's prebuilt bot navigation is offered first (Menu/ScctBotNavPrompt.h). */
		void StartMatch()
		{
			if (!MapIds.IsValidIndex(MapIndex))
			{
				return;
			}
			if (Bots <= 0)
			{
				LaunchMatch();
				return;
			}
			const TWeakPtr<SWidget> WeakPage = AsShared();
			ScctBotNavPrompt::OfferThenLaunch(Host.Dialogs, MapIds[MapIndex],
				[WeakPage] { return WeakPage.IsValid(); },
				[WeakPage, this] { if (WeakPage.IsValid()) { LaunchMatch(); } });
		}

		void LaunchMatch()
		{
			UScctOnlineSubsystem* Visit = Host.Online.Get();
			UWorld* World = Visit != nullptr ? Visit->GetWorld() : nullptr;
			if (World == nullptr || GEngine == nullptr || !MapIds.IsValidIndex(MapIndex))
			{
				return;
			}
			Save();

			// AScctGameMode reads these (Play/ScctGameMode.h). A name with the URL's own separator would cut the options.
			FString Url = FString::Printf(TEXT("%s?Mode=%s?Side=%s?Bots=%d?Difficulty=%d"), *UScctOnlineSubsystem::LevelOf(MapIds[MapIndex]),
				UScctMenuSubsystem::ModeKey(ModeIndex), bMerc || IsMercDeathmatch() ? TEXT("merc") : TEXT("spy"), Bots, Skill + 1);
			// Practice is the Godot client's --practice (PlayConfig.FromOptions): the offline rules and unlimited gear.
			if (bPractice) { Url += TEXT("?Practice=1?UnlimitedGear=1"); }
			if (MatchPlayers > 0) { Url += FString::Printf(TEXT("?MatchPlayers=%d"), MatchPlayers); }
			if (TimeLimit >= 0) { Url += FString::Printf(TEXT("?TimeLimit=%d"), TimeLimit); }
			if (SpyLives >= 0) { Url += FString::Printf(TEXT("?SpyLives=%d"), SpyLives); }
			if (MercLives >= 0) { Url += FString::Printf(TEXT("?MercLives=%d"), MercLives); }
			if (Respawn >= 0) { Url += FString::Printf(TEXT("?Respawn=%d"), Respawn); }
			const FString Name = Visit->GetPlayerName().Replace(TEXT("?"), TEXT("")).Replace(TEXT("#"), TEXT(""));
			if (!Name.IsEmpty())
			{
				Url += TEXT("?Name=") + Name;
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: starting an offline match: %s"), *Url);
			GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
		}

		void Load()
		{
			const FString& Ini = GGameUserSettingsIni;
			GConfig->GetString(PlaySection, TEXT("Map"), SavedMap, Ini);
			GConfig->GetBool(PlaySection, TEXT("Merc"), bMerc, Ini);
			GConfig->GetInt(PlaySection, TEXT("Bots"), Bots, Ini);
			GConfig->GetInt(PlaySection, TEXT("Skill"), Skill, Ini);
			GConfig->GetInt(PlaySection, TEXT("TimeLimit"), TimeLimit, Ini);
			GConfig->GetInt(PlaySection, TEXT("SpyLives"), SpyLives, Ini);
			GConfig->GetInt(PlaySection, TEXT("MercLives"), MercLives, Ini);
			GConfig->GetInt(PlaySection, TEXT("Respawn"), Respawn, Ini);
			GConfig->GetBool(PlaySection, TEXT("Practice"), bPractice, Ini);
			GConfig->GetInt(PlaySection, TEXT("MatchPlayers"), MatchPlayers, Ini);
			MatchPlayers = MatchPlayers < PlayMatchPlayersMin ? 0 : FMath::Min(MatchPlayers, PlayMatchPlayersMax);
			Bots = FMath::Clamp(Bots, 0, 15);
			Skill = FMath::Clamp(Skill, 0, 3);
		}

		void Save() const
		{
			const FString& Ini = GGameUserSettingsIni;
			if (MapIds.IsValidIndex(MapIndex))
			{
				GConfig->SetString(PlaySection, TEXT("Map"), *MapIds[MapIndex], Ini);
			}
			GConfig->SetBool(PlaySection, TEXT("Merc"), bMerc, Ini);
			GConfig->SetInt(PlaySection, TEXT("Bots"), Bots, Ini);
			GConfig->SetInt(PlaySection, TEXT("Skill"), Skill, Ini);
			GConfig->SetInt(PlaySection, TEXT("TimeLimit"), TimeLimit, Ini);
			GConfig->SetInt(PlaySection, TEXT("SpyLives"), SpyLives, Ini);
			GConfig->SetInt(PlaySection, TEXT("MercLives"), MercLives, Ini);
			GConfig->SetInt(PlaySection, TEXT("Respawn"), Respawn, Ini);
			GConfig->SetBool(PlaySection, TEXT("Practice"), bPractice, Ini);
			GConfig->SetInt(PlaySection, TEXT("MatchPlayers"), MatchPlayers, Ini);
			GConfig->Flush(false, Ini);
		}

		TArray<FString> MapIds;
		FString SavedMap;
		int32 MapIndex = 0;
		int32 ModeIndex = 0;
		bool bMerc = false;
		int32 Bots = 3;
		int32 Skill = 1;
		int32 TimeLimit = -1;
		int32 SpyLives = -1;
		int32 MercLives = -1;
		int32 Respawn = -1;
		bool bPractice = false;
		int32 MatchPlayers = 0;

		TSharedPtr<SBox> HeroBox;
		TWeakPtr<SWidget> FirstOption;
		TWeakPtr<SWidget> MapList;
		struct FMapRow
		{
			TWeakPtr<SWidget> Item;
			TWeakPtr<SExpandableArea> Family;
		};
		TArray<FMapRow> MapRows;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakePlay(const FScctMenuHost& Host)
{
	return SNew(SScctMenuPlay, Host);
}
