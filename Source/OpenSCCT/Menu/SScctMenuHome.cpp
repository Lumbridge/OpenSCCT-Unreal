// The main menu: the wordmark and the menu on the left, the player and the maps on the right.

#include "Menu/ScctMenuPage.h"

#include "HAL/PlatformProcess.h"
#include "Menu/ScctDashboardCards.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctRejoinPrompt.h"
#include "Menu/ScctSplashTrivia.h"
#include "Menu/ScctUserSettings.h"
#include "OpenSCCT.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Online/ScctRejoinSubsystem.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	class SScctMenuHome : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuHome) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			if (UScctOnlineSubsystem* Visit = Host.Online.Get())
			{
				// The master's message of the day comes with its list.
				Visit->EnsureConnected();
				if (Visit->HasMaster() && Visit->GetMotd().IsEmpty())
				{
					Visit->RequestServerList();
				}
			}

			// The map shown on the right: a different one each time the game starts, among those that can be played.
			if (Menu != nullptr && Menu->GetMaps().Num() > 0)
			{
				const TArray<FScctMenuMapInfo>& Maps = Menu->GetMaps();
				FeaturedMap = Maps[FMath::Abs(static_cast<int32>(FPlatformTime::Cycles())) % Maps.Num()].Id;
			}

			const TSharedRef<SVerticalBox> Entries = SNew(SVerticalBox);
			EntriesBox = Entries;

			// A round this game dropped out of that is still being played: shown once the master or the server confirmed
			// it (Online/ScctRejoinSubsystem.h), with a question the first time it is offered.
			if (UScctOnlineSubsystem* Visit = Host.Online.Get())
			{
				Rejoin = Visit->GetGameInstance()->GetSubsystem<UScctRejoinSubsystem>();
			}
			if (UScctRejoinSubsystem* RejoinNow = Rejoin.Get())
			{
				RejoinNow->Check();
				const TWeakObjectPtr<UScctRejoinSubsystem> WeakRejoin = Rejoin;
				Entries->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SBox)
					.Visibility(TAttribute<EVisibility>::CreateLambda([this, WeakRejoin]
					{
						// With the top level only: an open group has the column to itself.
						FScctRejoinChoice Choice;
						return OpenGroup == INDEX_NONE && WeakRejoin.IsValid() && WeakRejoin->GetOffer(Choice) ? EVisibility::Visible : EVisibility::Collapsed;
					}))
					[
						SNew(SScctButton)
						.Look(EScctButtonLook::Menu)
						.Compact(true)
						.Text(TAttribute<FText>::CreateLambda([WeakRejoin]
						{
							FScctRejoinChoice Choice;
							return FText::FromString(WeakRejoin.IsValid() && WeakRejoin->GetOffer(Choice) ? FString::Printf(TEXT("REJOIN %s"), Choice.ServerName).ToUpper() : FString(TEXT("REJOIN")));
						}))
						.SubText(TAttribute<FText>::CreateLambda([WeakRejoin] { return FText::FromString(ScctRejoinPrompt::Describe(WeakRejoin.Get()) + TEXT(", still being played")); }))
						.SubTextVisibility(this, &SScctMenuHome::GetSubTextVisibility)
						.Description(FText::FromString(TEXT("Back into the round this game dropped out of, on the same side and with the score and lives kept, as the server held them.")))
						.OnPressed(FSimpleDelegate::CreateLambda([WeakRejoin] { if (WeakRejoin.IsValid()) { WeakRejoin->Rejoin(); } }))
					]
				];
				RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctMenuHome::WatchRejoin));
			}

			Entries->AddSlot().AutoHeight()[ SAssignNew(ListBox, SVerticalBox) ];

			// The top level is short: what starts a game, what belongs to the player and the rest are groups that open
			// in this column (ShowLevel), one step down.
			const auto Add = [](TArray<FHomeEntry>& To, const FString& Caption, const FString& Sub, const FString& Description, TFunction<void()> Action)
			{
				FHomeEntry& Made = To.AddDefaulted_GetRef();
				Made.Caption = Caption;
				Made.Sub = Sub;
				Made.Description = Description;
				Made.Action = MoveTemp(Action);
			};
			const auto AddGroup = [this, &Add](const FString& Name, const FString& Sub, const FString& Description) -> int32
			{
				const int32 Index = Groups.AddDefaulted();
				Groups[Index].Name = Name;
				Groups[Index].TopIndex = Top.Num();
				Add(Top, Name, Sub, Description, [this, Index] { ShowLevel(Index, 0, true); });
				return Index;
			};

			if (FOpenSCCTModule::FindContentRoot().IsEmpty())
			{
				Add(Top, TEXT("Set up from Steam"), TEXT("Build original maps and assets"),
					TEXT("Select your Steam installation of Chaos Theory. Setup reads the original files and saves the converted maps on this PC."),
					[] { FOpenSCCTModule::OpenSteamSetup(); });
			}

			const int32 Play = AddGroup(TEXT("Play"), TEXT("Online, offline, host, hub"),
				TEXT("Join an online server, play against bots, host a session or visit a hub."));
			Add(Groups[Play].Entries, TEXT("Play online"), TEXT("Find a session the master lists, or join by address"),
				TEXT("The servers the master lists, with their maps, modes and players, and joining a server by its address."),
				[this] { Host.Open(TEXT("online")); });
			Add(Groups[Play].Entries, TEXT("Play offline"), TEXT("A match against bots on any map installed here"),
				TEXT("Pick a map, a mode and a side, set the bots and the rules, and play at once. Nothing is sent over the network."),
				[this] { Host.Open(TEXT("play")); });
			Add(Groups[Play].Entries, TEXT("Host game"), TEXT("Create a session on this PC for others to join"),
				TEXT("Pick a map and the rules, choose how the server is reached and listed, and start a listen server that you play on."),
				[this] { Host.Open(TEXT("host")); });
			Add(Groups[Play].Entries, TEXT("Hub"), TEXT("Meet other players between matches"),
				TEXT("The hubs the master lists, joining one by its address, or hosting a hub on this PC."),
				[this] { Host.Open(TEXT("hub")); });

			const int32 Practice = AddGroup(TEXT("Practice"), TEXT("Spy practice, mercenary practice, tutorials"),
				TEXT("Practise either role against bots or learn it on the tutorial maps."));
			Add(Groups[Practice].Entries, TEXT("Practice as a spy"), ScctDashboard::PracticeLine(false),
				TEXT("Straight into an offline match as a spy against three bots, on the map you last played offline, with the practice rules."),
				[this] { ScctDashboard::StartPractice(Host, false); });
			Add(Groups[Practice].Entries, TEXT("Practice as a mercenary"), ScctDashboard::PracticeLine(true),
				TEXT("Straight into an offline match as a mercenary against three bots, on the map you last played offline, with the practice rules."),
				[this] { ScctDashboard::StartPractice(Host, true); });
			Add(Groups[Practice].Entries, ScctUi::GameText(TEXT("Tutorials_Page"), TEXT("TitlePage"), TEXT("Tutorials")), TEXT("The HUD, tips, gadgets and the tutorial maps"),
				TEXT("The HUD and keyboard pictures, every tip and gadget of both sides, the spy and mercenary tutorial maps with your progress, and visiting a map alone with its help paths."),
				[this] { Host.Open(TEXT("tutorials")); });

			const int32 Profile = AddGroup(TEXT("Profile"), TEXT("Account, armoury, demos"),
				TEXT("What belongs to the player: the account with its name, rank and career, the cosmetics of both sides, and the recorded matches."));
			Add(Groups[Profile].Entries, TEXT("Account"), TEXT("Your name, rank and career"),
				TEXT("Sign in or out, claim or change the name the servers show, see your rank, experience and career, prestige, and choose an emblem and a title."),
				[this] { Host.Open(TEXT("account")); });
			Add(Groups[Profile].Entries, TEXT("Armoury"), TEXT("Cosmetics for both sides"),
				TEXT("The camo, tint and light colour of the spy and the mercenary by slot: see what your account unlocked, equip a look, and choose whose cosmetics are shown."),
				[this] { Host.Open(TEXT("armoury")); });
			Add(Groups[Profile].Entries, TEXT("Demos"), TEXT("Watch recorded matches"),
				TEXT("The demos recorded on this PC and the master's recent online demos: watch one with the playback bar, keep a download, delete one, or see a match's final scoreboard."),
				[this] { Host.Open(TEXT("demos")); });

			Add(Top, TEXT("Maps & Content"), TEXT("Manage community maps"),
				TEXT("Download, update or remove community maps. Stock maps come from your installed Steam game."),
				[this] { Host.Open(TEXT("content")); });
			Add(Top, ScctUi::GameText(TEXT("All_Settings"), TEXT("TitlePage"), TEXT("Options")), TEXT("Video, audio, controls and key bindings"),
				TEXT("Everything you can set: the picture, the sound, the mouse and the controller, every key of both sides, and your name."),
				[this] { Host.Open(TEXT("settings")); });

			const int32 More = AddGroup(TEXT("More"), TEXT("Help & feedback, credits"),
				TEXT("Suggest a feature, browse the guides or see who made OpenSCCT."));
			Add(Groups[More].Entries, TEXT("Help & feedback"), TEXT("Feature requests and project guides"),
				TEXT("Suggest a feature, browse existing issues, view game details or read the project guides."),
				[this] { Host.Open(TEXT("feedback")); });
			Add(Groups[More].Entries, TEXT("Credits"), TEXT("The people and the licences"),
				TEXT("Who made OpenSCCT and this Unreal presentation, and the licences of the assets it uses."),
				[this] { Host.Open(TEXT("credits")); });

			Add(Top, ScctUi::GameText(TEXT("Menu_Multi"), TEXT("MainPage_Quit.Caption"), TEXT("Quit")), TEXT("Back to the desktop"),
				TEXT("Close the game."),
				[this] { Host.Back(); });

			ShowLevel(INDEX_NONE, 0, false);

			const FString Tagline = ScctUi::GameText(TEXT("Menu_Multi"), TEXT("TitlePage"), TEXT("Spies versus Mercenaries"));
			UScctUserSettings* Settings = UScctUserSettings::Get();
			const TAttribute<FText> BackdropAction = TAttribute<FText>::CreateLambda([Settings]
			{
				return Settings->bLiveMenuBackground ? INVTEXT("Pause live menu background") : INVTEXT("Play live menu background");
			});
			const FString Trivia = ScctSplashTrivia::Pick();
			UE_LOG(LogOpenSCCT, Log, TEXT("Main menu trivia: %s"), *Trivia);

			ChildSlot
			[
				SNew(SHorizontalBox)
				// The wordmark and the menu.
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(600.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("OPEN"))).Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 76.0f, 260)).ColorAndOpacity(ScctHudStyle::Text())
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("SCCT"))).Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Bold, 76.0f, 260)).ColorAndOpacity(ScctUi::Accent())
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f)
							[
								SNew(SScctButton)
								.Look(EScctButtonLook::Chip)
								.Text_Lambda([Settings] { return Settings->bLiveMenuBackground ? INVTEXT("\u2016") : INVTEXT("\u25B6"); })
								.Selected_Lambda([Settings] { return Settings->bLiveMenuBackground; })
								.ToolTipText(BackdropAction)
								.AccessibleText(BackdropAction)
								.OnPressed(FSimpleDelegate::CreateLambda([Settings]
								{
									Settings->bLiveMenuBackground = !Settings->bLiveMenuBackground;
									Settings->Save();
								}))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 2.0f, 0.0f, 0.0f)
						[
							ScctUi::Label(Tagline, ScctHudStyle::TextDim(), 13.0f)
						]
						// A Splinter Cell fact, a different one each time the page opens (the Godot menu's SplashTrivia).
						+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Trivia))
							.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 12.0f))
							.ColorAndOpacity(ScctHudStyle::Fade(ScctHudStyle::TextDim(), 0.85f))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 14.0f, 0.0f, 16.0f)
						[
							SNew(SBox).WidthOverride(64.0f).HAlign(HAlign_Left)[ SNew(SBox).WidthOverride(64.0f)[ ScctUi::Rule(0.5f) ] ]
						]
						// The entries take the height left above the key hints. When they need more (REJOIN shown, a
						// short window) their second lines go (Tick), and should even that not fit they scroll,
						// following the focus.
						+ SVerticalBox::Slot().FillHeight(1.0f).Padding(-24.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SBox).WidthOverride(480.0f)
							[
								SAssignNew(EntriesScroll, SScrollBox)
								.ScrollBarVisibility(EVisibility::Collapsed)
								.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
								+ SScrollBox::Slot()[ Entries ]
							]
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				// The dashboard (Menu/ScctDashboardCards.h, the Godot MainMenu's cards): players online and the sessions
				// with the master's announcement, what is new, the player's progress, links out and the map of the moment.
				// It scrolls when a short window cannot hold it.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SScrollBox)
					.ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(SBox).WidthOverride(420.0f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ ScctDashboard::MakeOnlineCard(Host) ]
								+ SVerticalBox::Slot().AutoHeight()[ ScctDashboard::MakeWhatsNewCard(Host) ]
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
						[
							SNew(SBox).WidthOverride(420.0f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ ScctDashboard::MakeProgressCard(Host) ]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ ScctDashboard::MakeExploreCard(Host) ]
								+ SVerticalBox::Slot().AutoHeight()[ BuildFeatured() ]
							]
						]
					]
				]
			];
		}

		virtual FString GetTitle() const override { return FString(); }

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			// The master is polled for the dashboard only while this page is drawn.
			ScctDashboard::NoteShown();
			// The focus asked for by ShowLevel, once the new entries are in the page.
			if (const TSharedPtr<SWidget> Focus = PendingFocus.Pin())
			{
				PendingFocus.Reset();
				FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
			}
			if (!EntriesScroll.IsValid() || !EntriesBox.IsValid())
			{
				return;
			}
			const float Room = EntriesScroll->GetTickSpaceGeometry().GetLocalSize().Y;
			const float Wanted = EntriesBox->GetDesiredSize().Y;
			if (Room <= 0.0f)
			{
				return;
			}
			if (!bTight)
			{
				// The height with the second lines, kept to know when there is room for them again.
				FullHeight = Wanted;
				bTight = Wanted > Room + 1.0f;
			}
			else if (FullHeight <= Room)
			{
				bTight = false;
			}
		}

		/** Escape or the pad's back in an open group: to the top level, on the group's entry. */
		virtual bool HandleBack() override
		{
			if (!Groups.IsValidIndex(OpenGroup))
			{
				return false;
			}
			ScctUi::Play(EScctUiSound::Back);
			ShowLevel(INDEX_NONE, Groups[OpenGroup].TopIndex, true);
			return true;
		}

	private:
		/** One line of the column: what it says and what pressing it does. */
		struct FHomeEntry
		{
			FString Caption;
			FString Sub;
			FString Description;
			TFunction<void()> Action;
		};

		/** A top-level entry that opens its own entries in the column; TopIndex is that entry's place in the top level. */
		struct FHomeGroup
		{
			FString Name;
			int32 TopIndex = 0;
			TArray<FHomeEntry> Entries;
		};

		TSharedRef<SScctButton> MakeEntry(const FHomeEntry& Entry)
		{
			TSharedPtr<SScctButton> Made;
			const TFunction<void()> Action = Entry.Action;
			ListBox->AddSlot().AutoHeight()
			[
				SAssignNew(Made, SScctButton)
				.Look(EScctButtonLook::Menu)
				.Compact(true)
				.Text(FText::FromString(Entry.Caption.ToUpper()))
				.SubText(FText::FromString(Entry.Sub))
				.SubTextVisibility(this, &SScctMenuHome::GetSubTextVisibility)
				.Description(FText::FromString(Entry.Description))
				.OnPressed(FSimpleDelegate::CreateLambda([Action] { Action(); }))
			];
			return Made.ToSharedRef();
		}

		/**
		 * Fills the column with the top level (INDEX_NONE) or with a group's entries under the group's name and above
		 * BACK. FocusIndex is the entry the focus starts on, taken on the next frame when bFocus (the page is being
		 * used). The entry pressed to get here is gone when this returns: the caller uses nothing of it afterwards.
		 */
		void ShowLevel(int32 Group, int32 FocusIndex, bool bFocus)
		{
			OpenGroup = Groups.IsValidIndex(Group) ? Group : INDEX_NONE;
			// A copy: the action of one of the entries being replaced may be what is running now.
			const TArray<FHomeEntry> Shown = OpenGroup != INDEX_NONE ? Groups[OpenGroup].Entries : Top;
			ListBox->ClearChildren();
			if (OpenGroup != INDEX_NONE)
			{
				ListBox->AddSlot().AutoHeight().Padding(24.0f, 0.0f, 0.0f, 8.0f)
				[
					ScctUi::Label(Groups[OpenGroup].Name.ToUpper(), ScctUi::Accent(), 13.0f)
				];
			}
			TSharedPtr<SWidget> Focus;
			for (int32 Index = 0; Index < Shown.Num(); ++Index)
			{
				const TSharedRef<SScctButton> Made = MakeEntry(Shown[Index]);
				if (Index == FocusIndex || !Focus.IsValid())
				{
					Focus = Made;
				}
			}
			if (OpenGroup != INDEX_NONE)
			{
				FHomeEntry Back;
				Back.Caption = TEXT("Back");
				Back.Sub = TEXT("To the main menu");
				Back.Description = TEXT("Back to the first list of the main menu.");
				Back.Action = [this] { HandleBack(); };
				MakeEntry(Back);
			}
			DefaultFocus = Focus;
			PendingFocus = bFocus ? Focus : nullptr;
			// The new entries are measured afresh for whether their second lines fit (Tick).
			bTight = false;
			FullHeight = 0.0f;
			if (EntriesScroll.IsValid())
			{
				EntriesScroll->ScrollToStart();
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("Main menu: showing %s"), OpenGroup != INDEX_NONE ? *Groups[OpenGroup].Name : TEXT("the top level"));
		}

		EVisibility GetSubTextVisibility() const { return bTight ? EVisibility::Collapsed : EVisibility::Visible; }

		/** Whole sentences from the start of a text, as many as fit in about MaxChars (at least one): never a sentence cut off. */
		static FString FirstSentences(const FString& Text, int32 MaxChars)
		{
			FString Out;
			int32 Start = 0;
			for (int32 Index = 0; Index < Text.Len(); ++Index)
			{
				const TCHAR Char = Text[Index];
				const bool bEnd = (Char == TEXT('.') || Char == TEXT('!') || Char == TEXT('?')) && (Index + 1 == Text.Len() || FChar::IsWhitespace(Text[Index + 1]));
				if (bEnd)
				{
					const FString Sentence = Text.Mid(Start, Index + 1 - Start).TrimStartAndEnd();
					if (!Out.IsEmpty() && Out.Len() + 1 + Sentence.Len() > MaxChars)
					{
						return Out;
					}
					Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + Sentence;
					Start = Index + 1;
				}
			}
			return Out.IsEmpty() ? Text : Out;
		}

		TSharedRef<SWidget> BuildFeatured()
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const int32 Count = Menu != nullptr ? Menu->GetMaps().Num() : 0;
			if (FeaturedMap.IsEmpty() || Menu == nullptr)
			{
				return ScctUi::Card(TEXT("Maps"),
					SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text(FText::FromString(TEXT("No map can be played yet. Convert your game with OpenSCCT, then import a map (README: Tools\\Import\\Import-Map.ps1)."))));
			}
			FString Description = Menu->GetMapText(FeaturedMap, EScctMenuMapText::Description);
			if (Description.TrimStartAndEnd().IsEmpty())
			{
				Description = Menu->GetMapText(FeaturedMap, EScctMenuMapText::BriefSpy);
			}
			Description = FirstSentences(Description.Replace(TEXT("|"), TEXT(" ")).TrimStartAndEnd(), 170);
			return ScctUi::Card(FString::Printf(TEXT("%d map%s ready to play"), Count, Count == 1 ? TEXT("") : TEXT("s")),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Picture(Menu->GetMapPicture(FeaturedMap, false), 360.0f, 180.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).Text(FText::FromString(Menu->GetMapTitle(FeaturedMap)))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Text(FText::FromString(Description))
				]);
		}

		/** Asks once per offer whether to rejoin, and closes the question when the offer went (rejoined or forgotten elsewhere). */
		EActiveTimerReturnType WatchRejoin(double, float)
		{
			UScctRejoinSubsystem* RejoinNow = Rejoin.Get();
			if (RejoinNow == nullptr || !Host.Dialogs.IsValid())
			{
				return EActiveTimerReturnType::Continue;
			}
			FScctRejoinChoice Choice;
			const bool bOffered = RejoinNow->GetOffer(Choice);
			// Once per offer for the whole run: LATER, then back on the main menu, does not ask again.
			static int32 AskedRevision = -1;
			if (bOffered && Choice.Revision != AskedRevision && !Host.Dialogs->IsOpen())
			{
				AskedRevision = Choice.Revision;
				ScctRejoinPrompt::Show(Host.Dialogs, RejoinNow);
				bAsking = true;
			}
			else if (bAsking && !Host.Dialogs->IsOpen())
			{
				bAsking = false;
			}
			else if (bAsking && !bOffered)
			{
				bAsking = false;
				Host.Dialogs->Answer(1);
			}
			return EActiveTimerReturnType::Continue;
		}

		FString FeaturedMap;
		TSharedPtr<SScrollBox> EntriesScroll;
		TSharedPtr<SVerticalBox> EntriesBox;
		/** The entries of the level shown, inside EntriesBox under REJOIN. */
		TSharedPtr<SVerticalBox> ListBox;
		TArray<FHomeEntry> Top;
		TArray<FHomeGroup> Groups;
		/** The group whose entries fill the column, or INDEX_NONE at the top level. */
		int32 OpenGroup = INDEX_NONE;
		TWeakPtr<SWidget> PendingFocus;
		/** The entries would not fit with their second lines, which are hidden; FullHeight is the height they need with them. */
		bool bTight = false;
		float FullHeight = 0.0f;
		TWeakObjectPtr<UScctRejoinSubsystem> Rejoin;
		bool bAsking = false;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeHome(const FScctMenuHost& Host)
{
	return SNew(SScctMenuHome, Host);
}
