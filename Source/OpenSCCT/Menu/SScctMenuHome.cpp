// The main menu: the wordmark and the menu on the left, the player and the maps on the right.

#include "Menu/ScctMenuPage.h"

#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctRejoinPrompt.h"
#include "Menu/ScctSplashTrivia.h"
#include "OpenSCCT.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Online/ScctRejoinSubsystem.h"
#include "Widgets/Layout/SBox.h"
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
			const auto Entry = [this, &Entries](const FString& Caption, const FString& Sub, const FString& Description, TFunction<void()> OnPressed) -> TSharedRef<SScctButton>
			{
				TSharedPtr<SScctButton> Made;
				Entries->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SAssignNew(Made, SScctButton)
					.Look(EScctButtonLook::Menu)
					.Text(FText::FromString(Caption.ToUpper()))
					.SubText(FText::FromString(Sub))
					.Description(FText::FromString(Description))
					.OnPressed(FSimpleDelegate::CreateLambda([OnPressed] { OnPressed(); }))
				];
				return Made.ToSharedRef();
			};

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
					.Visibility(TAttribute<EVisibility>::CreateLambda([WeakRejoin]
					{
						FScctRejoinChoice Choice;
						return WeakRejoin.IsValid() && WeakRejoin->GetOffer(Choice) ? EVisibility::Visible : EVisibility::Collapsed;
					}))
					[
						SNew(SScctButton)
						.Look(EScctButtonLook::Menu)
						.Text(TAttribute<FText>::CreateLambda([WeakRejoin]
						{
							FScctRejoinChoice Choice;
							return FText::FromString(WeakRejoin.IsValid() && WeakRejoin->GetOffer(Choice) ? FString::Printf(TEXT("REJOIN %s"), Choice.ServerName).ToUpper() : FString(TEXT("REJOIN")));
						}))
						.SubText(TAttribute<FText>::CreateLambda([WeakRejoin] { return FText::FromString(ScctRejoinPrompt::Describe(WeakRejoin.Get()) + TEXT(", still being played")); }))
						.Description(FText::FromString(TEXT("Back into the round this game dropped out of, on the same side and with the score and lives kept, as the server held them.")))
						.OnPressed(FSimpleDelegate::CreateLambda([WeakRejoin] { if (WeakRejoin.IsValid()) { WeakRejoin->Rejoin(); } }))
					]
				];
				RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctMenuHome::WatchRejoin));
			}

			const TSharedRef<SScctButton> First = Entry(TEXT("Play offline"), TEXT("A match against bots on any map installed here"),
				TEXT("Pick a map, a mode and a side, set the bots and the rules, and play at once. Nothing is sent over the network."),
				[this] { Host.Open(TEXT("play")); });
			DefaultFocus = First;
			Entry(TEXT("Play online"), TEXT("Find a server or join one by its address"),
				TEXT("The servers the master lists, with their maps, modes and players, and joining a server by its address."),
				[this] { Host.Open(TEXT("online")); });
			Entry(TEXT("Host game"), TEXT("Create a session on this PC for others to join"),
				TEXT("Pick a map and the rules, choose how the server is reached and listed, and start a listen server that you play on."),
				[this] { Host.Open(TEXT("host")); });
			Entry(TEXT("Hub"), TEXT("Meet other players between matches"),
				TEXT("The hubs the master lists, joining one by its address, or hosting a hub on this PC."),
				[this] { Host.Open(TEXT("hub")); });
			Entry(TEXT("Account"), TEXT("Your name, rank and career"),
				TEXT("Sign in or out, claim or change the name the servers show, see your rank, experience and career, prestige, and choose an emblem and a title."),
				[this] { Host.Open(TEXT("account")); });
			Entry(ScctUi::GameText(TEXT("All_Settings"), TEXT("TitlePage"), TEXT("Options")), TEXT("Video, audio, controls and key bindings"),
				TEXT("Everything you can set: the picture, the sound, the mouse and the controller, every key of both sides, and your name."),
				[this] { Host.Open(TEXT("settings")); });
			Entry(TEXT("Credits"), TEXT("The people and the licences"),
				TEXT("Who made OpenSCCT and this Unreal presentation, and the licences of the assets it uses."),
				[this] { Host.Open(TEXT("credits")); });
			Entry(ScctUi::GameText(TEXT("Menu_Multi"), TEXT("MainPage_Quit.Caption"), TEXT("Quit")), TEXT("Back to the desktop"),
				TEXT("Close the game."),
				[this] { Host.Back(); });

			const FString Tagline = ScctUi::GameText(TEXT("Menu_Multi"), TEXT("TitlePage"), TEXT("Spies versus Mercenaries"));
			const FString Trivia = ScctSplashTrivia::Pick();
			UE_LOG(LogOpenSCCT, Log, TEXT("Main menu trivia: %s"), *Trivia);

			ChildSlot
			[
				SNew(SHorizontalBox)
				// The wordmark and the menu.
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 30.0f, 0.0f, 0.0f)
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
						+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 22.0f, 0.0f, 34.0f)
						[
							SNew(SBox).WidthOverride(64.0f).HAlign(HAlign_Left)[ SNew(SBox).WidthOverride(64.0f)[ ScctUi::Rule(0.5f) ] ]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(-24.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SBox).WidthOverride(480.0f)[ Entries ]
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				// The player, the map of the moment and the master's news.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SBox).WidthOverride(440.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ BuildNews() ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ BuildProfile() ]
						+ SVerticalBox::Slot().AutoHeight()[ BuildFeatured() ]
					]
				]
			];
		}

		virtual FString GetTitle() const override { return FString(); }

	private:
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

		TSharedRef<SWidget> BuildProfile()
		{
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			const auto IsSignedIn = [Weak] { return Weak.IsValid() && Weak->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn); };
			return ScctUi::Card(TEXT("Agent"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text())
						.Text(TAttribute<FText>::CreateLambda([Weak, IsSignedIn]
						{
							const FString Name = !Weak.IsValid() ? FString() : IsSignedIn() ? Weak->GetAccountName() : Weak->GetPlayerName();
							return FText::FromString(Name.IsEmpty() ? TEXT("Player") : Name);
						}))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
						.Text(TAttribute<FText>::CreateLambda([Weak, IsSignedIn]
						{
							if (!Weak.IsValid() || !Weak->HasMaster()) { return FText::FromString(TEXT("Offline: no master server set")); }
							return FText::FromString(IsSignedIn() ? TEXT("Signed in to OpenSCCT") : TEXT("Guest (not signed in)"));
						}))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SScctButton)
					.Look(EScctButtonLook::Chip)
					.Text(FText::FromString(TEXT("PROFILE")))
					.Description(FText::FromString(TEXT("Your name, and signing in to your account.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("settings/profile")); }))
				]);
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
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Picture(Menu->GetMapPicture(FeaturedMap, false), 392.0f, 196.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).Text(FText::FromString(Menu->GetMapTitle(FeaturedMap)))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Text(FText::FromString(Description))
				]);
		}

		TSharedRef<SWidget> BuildNews()
		{
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			return SNew(SBox)
				.Visibility(TAttribute<EVisibility>::CreateLambda([Weak] { return Weak.IsValid() && !Weak->GetMotd().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; }))
				[
					ScctUi::Card(TEXT("From the master server"),
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
						.Text(TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? FText::FromString(Weak->GetMotd()) : FText::GetEmpty(); })))
				];
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
		TWeakObjectPtr<UScctRejoinSubsystem> Rejoin;
		bool bAsking = false;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeHome(const FScctMenuHost& Host)
{
	return SNew(SScctMenuHome, Host);
}
