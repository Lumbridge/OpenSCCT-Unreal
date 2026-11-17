// Play offline: the map list on the left, the chosen map in the middle, the match's settings on the right.

#include "Menu/ScctMenuPage.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Layout/SBox.h"
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
			FixMode();

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

			TSharedPtr<SScctButton> StartButton;
			ChildSlot
			[
				SNew(SHorizontalBox)
				// The maps.
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(350.0f)
					[
						ScctUi::Card(FString::Printf(TEXT("Maps · %d"), MapIds.Num()), BuildMapList(), FMargin(12.0f, 18.0f, 6.0f, 12.0f))
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
					SNew(SBox).WidthOverride(520.0f)
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
					SelectMap(Found);
				}
			}
		}

		virtual void OnClosing() override { Save(); }

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

		void FixMode()
		{
			const TArray<int32> Modes = GetModes();
			if (!Modes.Contains(ModeIndex))
			{
				ModeIndex = Modes[0];
			}
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
				FixMode();
				RefreshHero();
			}
		}

		// ---- the map list -----------------------------------------------------------------------------------------

		TSharedRef<SWidget> BuildMapList()
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
			for (int32 Index = 0; Index < MapIds.Num(); ++Index)
			{
				const FScctMenuMapInfo* Map = Menu->FindMap(MapIds[Index]);
				FString Modes;
				for (int32 Mode = 0; Map != nullptr && Mode < 4; ++Mode)
				{
					if (Map->Plays(Mode))
					{
						Modes += (Modes.IsEmpty() ? TEXT("") : TEXT(" · ")) + Menu->GetModeName(Mode);
					}
				}
				const FSlateBrush* Thumb = Menu->GetPicture(Menu->GetMapText(MapIds[Index], EScctMenuMapText::PreviewImage));
				if (Thumb == nullptr)
				{
					Thumb = Menu->GetMapPicture(MapIds[Index], false);
				}
				TSharedPtr<SWidget> Item;
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 6.0f, 4.0f)
				[
					SAssignNew(Item, SScctListItem)
					.Padding(FMargin(8.0f, 6.0f))
					.Selected(TAttribute<bool>::CreateLambda([this, Index] { return MapIndex == Index; }))
					.OnFocused(FSimpleDelegate::CreateLambda([this, Index]
					{
						SelectMap(Index);
						ScctUi::Describe(FText::FromString(MapTitle()), FText::FromString(TEXT("Enter or A: on to the match settings.")));
					}))
					.OnActivated(FSimpleDelegate::CreateLambda([this] { if (FirstOption.IsValid()) { FSlateApplication::Get().SetKeyboardFocus(FirstOption.Pin(), EFocusCause::Navigation); } }))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
						[
							ScctUi::Picture(Thumb, 88.0f, 50.0f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Font(ScctUi::Body(true)).ColorAndOpacity(ScctHudStyle::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Text(FText::FromString(Map != nullptr ? Map->Title : MapIds[Index]))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Text(FText::FromString(Modes))
							]
						]
					]
				];
				if (Index == MapIndex)
				{
					DefaultFocus = Item;
				}
			}
			return SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[ List ];
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
			LegendLines.RemoveAll([](const FString& Line) { return Line.IsEmpty(); });
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
						+ SOverlay::Slot()[ ScctUi::Picture(Menu->GetMapPicture(Id, bMerc), 0.0f, 340.0f) ]
						+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Left).Padding(18.0f, 0.0f, 0.0f, 14.0f)
						[
							ScctUi::Label(Players, ScctHudStyle::Text(), 11.0f)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(28.0f, 22.0f, 28.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 34.0f, 120)).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(MapTitle()))
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
				.Label(FText::FromString(ScctUi::GameText(TEXT("Create_Match"), TEXT("Mode.Caption"), TEXT("Game Mode"))))
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
				.Label(FText::FromString(ScctUi::GameText(TEXT("Create_Match"), TEXT("TimeLimitCaption"), TEXT("Game Length (min)"))))
				.Description(FText::FromString(TEXT("The round's length. \"Map's\" keeps what the map sets for this mode.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayTimeChoices) { Out.Add(Choice(V, R != nullptr ? R->TimeLimit : 0, TEXT(" min"), TEXT("No limit"))); } return Out; })
				.Get([this] { return IndexIn(PlayTimeChoices, TimeLimit); })
				.Set([this](int32 Value) { TimeLimit = PlayTimeChoices[Value]; }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameText(TEXT("Create_Match"), TEXT("SpyLivesCaption"), TEXT("Spy Lives"))))
				.Description(FText::FromString(TEXT("Lives of each spy. Not used in Merc Deathmatch.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayLivesChoices) { Out.Add(Choice(V, R != nullptr ? R->SpyLives : 0, TEXT(""), TEXT("Unlimited"))); } return Out; })
				.Get([this] { return IndexIn(PlayLivesChoices, SpyLives); })
				.Set([this](int32 Value) { SpyLives = PlayLivesChoices[Value]; }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameText(TEXT("Create_Match"), TEXT("MercLivesCaption"), TEXT("Mercenary Lives"))))
				.Description(FText::FromString(TEXT("Lives of each mercenary.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayLivesChoices) { Out.Add(Choice(V, R != nullptr ? R->MercLives : 0, TEXT(""), TEXT("Unlimited"))); } return Out; })
				.Get([this] { return IndexIn(PlayLivesChoices, MercLives); })
				.Set([this](int32 Value) { MercLives = PlayLivesChoices[Value]; }));
			Add(SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameText(TEXT("Create_Match"), TEXT("RespawnCaption"), TEXT("Respawn Length (sec)"))))
				.Description(FText::FromString(TEXT("Seconds between a death and the next life.")))
				.Wrap(false)
				.GetOptions([this] { TArray<FString> Out; const FScctMenuMode* R = GetModeRules(); for (const int32 V : PlayRespawnChoices) { Out.Add(Choice(V, R != nullptr ? R->RespawnSeconds : 0, TEXT(" s"), TEXT("0 s"))); } return Out; })
				.Get([this] { return IndexIn(PlayRespawnChoices, Respawn); })
				.Set([this](int32 Value) { Respawn = PlayRespawnChoices[Value]; }));
			Add(SNew(SScctToggle)
				.Label(FText::FromString(TEXT("Practice rules")))
				.Description(FText::FromString(TEXT("No time limit, unlimited lives and a 5 second respawn, unless set above. For learning a map.")))
				.Get([this] { return bPractice; })
				.Set([this](bool bValue) { bPractice = bValue; }));
			Add(SNew(SScctToggle)
				.Label(FText::FromString(TEXT("Sidearms")))
				.Description(FText::FromString(TEXT("The mercenaries also carry OpenSCCT's sidearms.")))
				.Get([this] { return bSidearms; })
				.Set([this](bool bValue) { bSidearms = bValue; }));
			Add(SNew(SScctToggle)
				.Label(FText::FromString(TEXT("Additional rifles")))
				.Description(FText::FromString(TEXT("The mercenaries may pick OpenSCCT's additional rifles.")))
				.Get([this] { return bRifles; })
				.Set([this](bool bValue) { bRifles = bValue; }));
			return Rows;
		}

		// ---- starting ---------------------------------------------------------------------------------------------

		void StartMatch()
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
			if (bPractice) { Url += TEXT("?Practice=1"); }
			if (bSidearms) { Url += TEXT("?Sidearms=1"); }
			if (bRifles) { Url += TEXT("?Rifles=1"); }
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
			GConfig->GetInt(PlaySection, TEXT("Mode"), ModeIndex, Ini);
			GConfig->GetBool(PlaySection, TEXT("Merc"), bMerc, Ini);
			GConfig->GetInt(PlaySection, TEXT("Bots"), Bots, Ini);
			GConfig->GetInt(PlaySection, TEXT("Skill"), Skill, Ini);
			GConfig->GetInt(PlaySection, TEXT("TimeLimit"), TimeLimit, Ini);
			GConfig->GetInt(PlaySection, TEXT("SpyLives"), SpyLives, Ini);
			GConfig->GetInt(PlaySection, TEXT("MercLives"), MercLives, Ini);
			GConfig->GetInt(PlaySection, TEXT("Respawn"), Respawn, Ini);
			GConfig->GetBool(PlaySection, TEXT("Practice"), bPractice, Ini);
			GConfig->GetBool(PlaySection, TEXT("Sidearms"), bSidearms, Ini);
			GConfig->GetBool(PlaySection, TEXT("Rifles"), bRifles, Ini);
			ModeIndex = FMath::Clamp(ModeIndex, 0, 3);
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
			GConfig->SetInt(PlaySection, TEXT("Mode"), ModeIndex, Ini);
			GConfig->SetBool(PlaySection, TEXT("Merc"), bMerc, Ini);
			GConfig->SetInt(PlaySection, TEXT("Bots"), Bots, Ini);
			GConfig->SetInt(PlaySection, TEXT("Skill"), Skill, Ini);
			GConfig->SetInt(PlaySection, TEXT("TimeLimit"), TimeLimit, Ini);
			GConfig->SetInt(PlaySection, TEXT("SpyLives"), SpyLives, Ini);
			GConfig->SetInt(PlaySection, TEXT("MercLives"), MercLives, Ini);
			GConfig->SetInt(PlaySection, TEXT("Respawn"), Respawn, Ini);
			GConfig->SetBool(PlaySection, TEXT("Practice"), bPractice, Ini);
			GConfig->SetBool(PlaySection, TEXT("Sidearms"), bSidearms, Ini);
			GConfig->SetBool(PlaySection, TEXT("Rifles"), bRifles, Ini);
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
		bool bSidearms = false;
		bool bRifles = false;

		TSharedPtr<SBox> HeroBox;
		TWeakPtr<SWidget> FirstOption;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakePlay(const FScctMenuHost& Host)
{
	return SNew(SScctMenuPlay, Host);
}
