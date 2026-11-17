// The main menu: the wordmark and the menu on the left, the player and the maps on the right.

#include "Menu/ScctMenuPage.h"

#include "Menu/ScctMenuSubsystem.h"
#include "Online/ScctOnlineSubsystem.h"
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

			const TSharedRef<SScctButton> First = Entry(TEXT("Play offline"), TEXT("A match against bots on any map installed here"),
				TEXT("Pick a map, a mode and a side, set the bots and the rules, and play at once. Nothing is sent over the network."),
				[this] { Host.Open(TEXT("play")); });
			DefaultFocus = First;
			Entry(TEXT("Play online"), TEXT("Find a server or join one by its address"),
				TEXT("The servers the master lists, with their maps, modes and players, and joining a server by its address."),
				[this] { Host.Open(TEXT("online")); });
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
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim())
						.Text(TAttribute<FText>::CreateLambda([Weak, IsSignedIn]
						{
							if (!Weak.IsValid() || !Weak->HasMaster()) { return FText::FromString(TEXT("Playing offline: no master server is set")); }
							return FText::FromString(IsSignedIn() ? TEXT("Signed in: your name is your account's") : TEXT("Guest: sign in to join the official servers"));
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
			FString Description = Menu->GetMapText(FeaturedMap, EScctMenuMapText::Description).Replace(TEXT("|"), TEXT(" "));
			return ScctUi::Card(FString::Printf(TEXT("%d map%s ready to play"), Count, Count == 1 ? TEXT("") : TEXT("s")),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Picture(Menu->GetMapPicture(FeaturedMap, false), 392.0f, 196.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Menu->GetMapTitle(FeaturedMap)))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).MaxDesiredHeight(60.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Text(FText::FromString(Description))
					]
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

		FString FeaturedMap;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeHome(const FScctMenuHost& Host)
{
	return SNew(SScctMenuHome, Host);
}
