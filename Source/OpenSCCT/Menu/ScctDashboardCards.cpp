#include "Menu/ScctDashboardCards.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Menu/ScctBotNavPrompt.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctRankIcons.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Social/ScctAccountSubsystem.h"
#include "Social/ScctDashboardSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The player website (the Godot client's ProjectLinks.Website). */
	const TCHAR* const ScctDashboardWebsite = TEXT("https://master.lumbridge.co.uk");
	const TCHAR* const ScctDashboardReleases = TEXT("https://github.com/Lumbridge/OpenSCCT/releases");

	/** The Play offline page's saved choices (Menu/SScctMenuPlay.cpp). */
	const TCHAR* const ScctDashboardPlaySection = TEXT("OpenSCCT.Play");

	constexpr int32 ScctPracticeBots = 3;

	FString ScctDashText(EScctDashboardText Kind)
	{
		const UScctDashboardSubsystem* Dashboard = UScctDashboardSubsystem::GetCurrent();
		return Dashboard != nullptr ? Dashboard->GetText(Kind) : FString();
	}

	bool ScctDashFlag(int32 Flag)
	{
		const UScctDashboardSubsystem* Dashboard = UScctDashboardSubsystem::GetCurrent();
		return Dashboard != nullptr && Dashboard->HasFlag(Flag);
	}

	TAttribute<FText> ScctDashAttr(EScctDashboardText Kind, const TCHAR* Fallback = TEXT(""))
	{
		const FString Default = Fallback;
		return TAttribute<FText>::CreateLambda([Kind, Default]
		{
			const FString Text = ScctDashText(Kind);
			return FText::FromString(Text.IsEmpty() ? Default : Text);
		});
	}

	/** The announcement, else the message of the day (the dashboard's, else the server list's). */
	FString ScctDashNotice(const TWeakObjectPtr<UScctOnlineSubsystem>& Online, bool* bOutAnnouncement = nullptr)
	{
		FString Text = ScctDashText(EScctDashboardText::Announcement);
		if (bOutAnnouncement != nullptr)
		{
			*bOutAnnouncement = !Text.IsEmpty();
		}
		if (Text.IsEmpty())
		{
			Text = ScctDashText(EScctDashboardText::Motd);
		}
		if (Text.IsEmpty() && Online.IsValid())
		{
			Text = Online->GetMotd();
		}
		return Text.TrimStartAndEnd();
	}

	TSharedRef<SWidget> ScctDashLine(const TAttribute<FText>& Text, const FSlateFontInfo& Font, const FLinearColor& Colour, float Top = 0.0f)
	{
		return SNew(SBox).Padding(0.0f, Top, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(Text).Font(Font).ColorAndOpacity(Colour).AutoWrapText(true)
			];
	}

	TSharedRef<SWidget> ScctDashChip(const FString& Caption, const FString& Description, TFunction<void()> OnPressed)
	{
		return SNew(SScctButton)
			.Look(EScctButtonLook::Chip)
			.Text(FText::FromString(Caption.ToUpper()))
			.Description(FText::FromString(Description))
			.OnPressed(FSimpleDelegate::CreateLambda([OnPressed] { OnPressed(); }));
	}

	void ScctDashOpenUrl(const FString& Url)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: opening %s in the browser"), *Url);
		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
	}

	/** The map practice starts on: the one last played offline when it can still be played, else the first. */
	FString ScctDashPracticeMap(int32* OutMode, bool bMerc)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		if (Menu == nullptr || Menu->GetMaps().IsEmpty())
		{
			return FString();
		}
		FString Saved;
		GConfig->GetString(ScctDashboardPlaySection, TEXT("Map"), Saved, GGameUserSettingsIni);
		const FScctMenuMapInfo* Map = nullptr;
		for (const FScctMenuMapInfo& Each : Menu->GetMaps())
		{
			if (Each.Id.Equals(Saved, ESearchCase::IgnoreCase))
			{
				Map = &Each;
			}
		}
		if (Map == nullptr)
		{
			Map = &Menu->GetMaps()[0];
		}
		// The first two-sided mode the map plays (not Merc Deathmatch, where nobody is a spy).
		int32 Mode = 0;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (Map->Plays(Index))
			{
				Mode = Index;
				break;
			}
		}
		if (OutMode != nullptr)
		{
			*OutMode = Mode;
		}
		return Map->Id;
	}

	// ---- the What's new and Announcement pages ------------------------------------------------------------------

	class SScctWhatsNewPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctWhatsNewPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost, bool bInAnnouncement)
		{
			Host = InHost;
			bAnnouncement = bInAnnouncement;
			const TSharedRef<SVerticalBox> Top = SNew(SVerticalBox);
			Top->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
				.Text(ScctDashAttr(bAnnouncement ? EScctDashboardText::AnnouncementStatus : EScctDashboardText::ChangelogStatus))
			];
			TSharedPtr<SScctButton> All;
			if (!bAnnouncement)
			{
				Top->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SAssignNew(All, SScctButton)
					.Look(EScctButtonLook::Secondary)
					.Text(FText::FromString(TEXT("ALL RELEASES ON GITHUB")))
					.Description(FText::FromString(TEXT("Every release of OpenSCCT with its notes, in your web browser.")))
					.OnPressed(FSimpleDelegate::CreateLambda([] { ScctDashOpenUrl(ScctDashboardReleases); }))
				];
				DefaultFocus = All;
			}
			ChildSlot
			[
				SNew(SBox).MaxDesiredWidth(980.0f).HAlign(HAlign_Left)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Top ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(Scroll, SScrollBox)
						.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
						+ SScrollBox::Slot()[ SAssignNew(List, SVerticalBox) ]
					]
				]
			];
			Rebuild();
			RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctWhatsNewPage::Watch));
		}

		virtual FString GetTitle() const override { return bAnnouncement ? TEXT("Announcement") : TEXT("What's new"); }
		virtual FString GetKicker() const override { return bAnnouncement ? TEXT("Community briefing") : TEXT("The latest releases"); }

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			ScctDashboard::NoteShown();
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			// Page Up / Down, Home and End move through long notes.
			if (!Scroll.IsValid())
			{
				return false;
			}
			const float Page = FMath::Max(Scroll->GetCachedGeometry().GetLocalSize().Y * 0.8f, 200.0f);
			if (Key == EKeys::PageDown) { Scroll->SetScrollOffset(Scroll->GetScrollOffset() + Page); return true; }
			if (Key == EKeys::PageUp) { Scroll->SetScrollOffset(FMath::Max(0.0f, Scroll->GetScrollOffset() - Page)); return true; }
			if (Key == EKeys::Home) { Scroll->ScrollToStart(); return true; }
			if (Key == EKeys::End) { Scroll->ScrollToEnd(); return true; }
			return false;
		}

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			OutKeyAndWhat.Add({ TEXT("PG UP / DN"), TEXT("Scroll") });
			OutPadKeys.Add(TEXT("RS"));
		}

	private:
		EActiveTimerReturnType Watch(double, float)
		{
			const UScctDashboardSubsystem* Dashboard = UScctDashboardSubsystem::GetCurrent();
			const FString Key = Dashboard == nullptr ? FString() : bAnnouncement ? ScctDashNotice(Host.Online)
				: FString::JoinBy(Dashboard->GetReleases(), TEXT("|"), [](const FScctDashboardRelease& R) { return R.Tag + R.Date + FString::FromInt(R.Notes.Len()); });
			if (Key != ShownKey)
			{
				Rebuild();
			}
			return EActiveTimerReturnType::Continue;
		}

		void Rebuild()
		{
			const UScctDashboardSubsystem* Dashboard = UScctDashboardSubsystem::GetCurrent();
			List->ClearChildren();
			if (bAnnouncement)
			{
				bool bIsAnnouncement = false;
				const FString Text = ScctDashNotice(Host.Online, &bIsAnnouncement);
				ShownKey = Text;
				List->AddSlot().AutoHeight()
				[
					ScctUi::Card(bIsAnnouncement || Text.IsEmpty() ? TEXT("From the player site") : TEXT("Message of the day"),
						SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).LineHeightPercentage(1.15f)
						.Text(FText::FromString(Text.IsEmpty() ? TEXT("There are no current announcements.") : Text)))
				];
				return;
			}

			const TArray<FScctDashboardRelease> Releases = Dashboard != nullptr ? Dashboard->GetReleases() : TArray<FScctDashboardRelease>();
			ShownKey = FString::JoinBy(Releases, TEXT("|"), [](const FScctDashboardRelease& R) { return R.Tag + R.Date + FString::FromInt(R.Notes.Len()); });
			for (int32 Index = 0; Index < Releases.Num(); ++Index)
			{
				const FScctDashboardRelease& Release = Releases[Index];
				// The newest release opens with its notes shown, as the Godot page does.
				if (bFirstBuild && Index == 0)
				{
					Expanded.Add(Release.Tag);
				}
				const FString ReleaseTag = Release.Tag;
				const FString Url = Release.Url;
				const TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
				Body->AddSlot().AutoHeight()[ ScctDashLine(FText::FromString(Release.Name), ScctUi::H2(), ScctHudStyle::Text()) ];
				Body->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([this, ReleaseTag] { return Expanded.Contains(ReleaseTag) ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).LineHeightPercentage(1.15f)
						.Text(FText::FromString(Release.Notes.IsEmpty() ? TEXT("No release notes were provided for this version.") : Release.Notes))
					]
				];
				Body->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SScctButton)
						.Look(EScctButtonLook::Chip)
						.Text_Lambda([this, ReleaseTag] { return FText::FromString(Expanded.Contains(ReleaseTag) ? TEXT("HIDE RELEASE NOTES") : TEXT("READ RELEASE NOTES")); })
						.Description(FText::FromString(TEXT("Shows or hides what this release changed.")))
						.OnPressed(FSimpleDelegate::CreateLambda([this, ReleaseTag] { if (Expanded.Contains(ReleaseTag)) { Expanded.Remove(ReleaseTag); } else { Expanded.Add(ReleaseTag); } }))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ScctDashChip(TEXT("View on GitHub"), TEXT("This release's page on GitHub, in your web browser."), [Url] { ScctDashOpenUrl(Url); })
					]
				];
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
				[
					ScctUi::Card(FString::Printf(TEXT("%s · %s"), *Release.Tag, *Release.Date), Body)
				];
			}
			if (Releases.Num() > 0)
			{
				bFirstBuild = false;
			}
		}

		bool bAnnouncement = false;
		bool bFirstBuild = true;
		FString ShownKey = TEXT("-");
		TSet<FString> Expanded;
		TSharedPtr<SVerticalBox> List;
		TSharedPtr<SScrollBox> Scroll;
	};
}

namespace ScctDashboard
{
	void NoteShown()
	{
		if (UScctDashboardSubsystem* Dashboard = UScctDashboardSubsystem::GetCurrent())
		{
			Dashboard->NoteShown();
		}
	}

	FString MasterLabel()
	{
		const UScctDashboardSubsystem* Dashboard = UScctDashboardSubsystem::GetCurrent();
		return Dashboard != nullptr ? Dashboard->GetMasterLine().ToUpper() : FString();
	}

	TSharedRef<SWidget> MakeOnlineCard(const FScctMenuHost& Host)
	{
		const TWeakObjectPtr<UScctOnlineSubsystem> Online = Host.Online;
		return ScctUi::Card(TEXT("Online now"),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity_Lambda([] { return FSlateColor(ScctDashFlag(EScctDashboardFlags::PlayersKnown) ? ScctUi::Accent() : ScctHudStyle::Text()); })
				.Text(ScctDashAttr(EScctDashboardText::PlayersLine, TEXT("Players online: —")))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ScctDashLine(ScctDashAttr(EScctDashboardText::SessionsLine, TEXT("Asking the master server...")), ScctUi::Small(), ScctHudStyle::TextDim(), 4.0f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([Online] { return ScctDashNotice(Online).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ ScctUi::Rule(0.1f) ]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
							.Text_Lambda([Online]
							{
								// Two lines at most here; the Announcement page has all of it.
								FString Text = ScctDashNotice(Online);
								if (Text.Len() > 150)
								{
									Text = Text.Left(147).TrimEnd() + TEXT("…");
								}
								return FText::FromString(Text);
							})
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 0.0f, 0.0f, 0.0f)
						[
							ScctDashChip(TEXT("Read"), TEXT("The master's announcement or message of the day in full."), [Host] { Host.Open(TEXT("announcement")); })
						]
					]
				]
			]);
	}

	TSharedRef<SWidget> MakeProgressCard(const FScctMenuHost& Host)
	{
		const TWeakObjectPtr<UScctOnlineSubsystem> Online = Host.Online;
		const TWeakObjectPtr<UScctAccountSubsystem> Account = UScctAccountSubsystem::Get(Host.Online.Get());
		const auto SignedIn = [Account] { return Account.IsValid() && Account->GetView() == EScctAccountView::SignedIn; };
		const auto HasRank = [Account, SignedIn] { return SignedIn() && Account->HasFlag(EScctAccountFlags::HasProgress); };
		const auto Shown = [](bool bShown) { return bShown ? EVisibility::Visible : EVisibility::Collapsed; };

		return ScctUi::Card(TEXT("Your progress"),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text())
					.Text_Lambda([Online, Account, SignedIn]
					{
						if (SignedIn())
						{
							const FString Name = Account->GetText(EScctAccountText::Name);
							return FText::FromString(Name.IsEmpty() ? TEXT("Signed in") : Name);
						}
						const FString Name = Online.IsValid() ? Online->GetPlayerName().TrimStartAndEnd() : FString();
						return FText::FromString(Name.IsEmpty() || Name == TEXT("Player") ? TEXT("Welcome, agent") : TEXT("Welcome, ") + Name);
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 0.0f, 0.0f, 0.0f)
				[
					// The rank's emblem (Menu/ScctRankIcons) before its level.
					SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
					.Visibility_Lambda([HasRank, Shown] { return Shown(HasRank()); })
					[
						SNew(SImage).Image_Lambda([Account]
						{
							const FScctAccountInfo& Info = Account.IsValid() ? Account->GetInfo() : FScctAccountInfo();
							return ScctRankIcons::For(Info.Prestige, Info.Level, 24.0f);
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Caps(13.0f))
					.Visibility_Lambda([HasRank, Shown] { return Shown(HasRank()); })
					.ColorAndOpacity_Lambda([Account] { return FSlateColor(Account.IsValid() ? UScctAccountSubsystem::ToColour(Account->GetInfo().RankColour) : ScctUi::Accent()); })
					.Text_Lambda([Account] { return FText::FromString(Account.IsValid() ? Account->GetText(EScctAccountText::RankTag) : FString()); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SBox).HeightOverride(6.0f).Visibility_Lambda([HasRank, Shown] { return Shown(HasRank()); })
				[
					SNew(SProgressBar).FillColorAndOpacity(ScctUi::Accent())
					.Percent_Lambda([Account]
					{
						const FScctAccountInfo& Info = Account.IsValid() ? Account->GetInfo() : FScctAccountInfo();
						return TOptional<float>(Info.XpForNext > 0 ? FMath::Clamp(static_cast<float>(Info.XpIntoLevel) / Info.XpForNext, 0.0f, 1.0f) : 1.0f);
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
				.Text_Lambda([Online, Account, SignedIn, HasRank]
				{
					if (!Online.IsValid() || !Online->HasMaster()) { return FText::FromString(TEXT("Offline: no master server is set, so there is no account.")); }
					if (!SignedIn()) { return FText::FromString(TEXT("Sign in to track your rank and unlocks.")); }
					if (!HasRank()) { return FText::FromString(TEXT("Progress is currently unavailable.")); }
					FString Line = Account->GetText(EScctAccountText::NextLevel);
					if (Account->HasFlag(EScctAccountFlags::RefreshFailed)) { Line += TEXT(" · Last received"); }
					return FText::FromString(Line);
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text())
				.Visibility_Lambda([SignedIn, Shown] { return Shown(SignedIn() && !ScctDashText(EScctDashboardText::RecentResults).IsEmpty()); })
				.Text(ScctDashAttr(EScctDashboardText::RecentResults))
				.ToolTipText(FText::FromString(TEXT("Latest recorded rounds, newest first. W: win · L: loss · D: draw or no winner")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
				.Visibility_Lambda([SignedIn, Shown] { return Shown(SignedIn() && ScctDashFlag(EScctDashboardFlags::HasUnlock)); })
				.Text(ScctDashAttr(EScctDashboardText::NextUnlock))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				// The chips wrap across the card's width: the card is narrower than the four of them.
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f))
				+ SWrapBox::Slot()
				[
					SNew(SScctButton)
					.Look(EScctButtonLook::Chip)
					.Text_Lambda([SignedIn] { return FText::FromString(SignedIn() ? TEXT("MANAGE ACCOUNT") : TEXT("SIGN IN / ACCOUNT")); })
					.Description(FText::FromString(TEXT("Sign in or out, claim or change your name, your rank, career, prestige and title.")))
					.OnPressed(FSimpleDelegate::CreateLambda([Host] { Host.Open(TEXT("account")); }))
				]
				+ SWrapBox::Slot()
				[
					ScctDashChip(TEXT("Match history"), TEXT("Every round you played, on this PC and on your account, with its scoreboard."), [Host] { Host.Open(TEXT("history")); })
				]
				+ SWrapBox::Slot()
				[
					ScctDashChip(TEXT("Stats"), TEXT("Your career totals by category."), [Host] { Host.Open(TEXT("stats")); })
				]
				+ SWrapBox::Slot()
				[
					// The next unlock opens the achievement it names, as the Godot next-unlock card does.
					ScctDashChip(TEXT("Achievements"), TEXT("Every achievement with your progress and what it unlocks."),
						[Host] { Host.Open(ScctDashFlag(EScctDashboardFlags::HasUnlock) ? TEXT("achievements/next") : TEXT("achievements")); })
				]
			]);
	}

	TSharedRef<SWidget> MakeWhatsNewCard(const FScctMenuHost& Host)
	{
		return ScctUi::Card(TEXT("What's new"),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).Text(ScctDashAttr(EScctDashboardText::LatestTitle, TEXT("Latest update")))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ScctDashLine(ScctDashAttr(EScctDashboardText::LatestPreview, TEXT("Checking release notes...")), ScctUi::Small(), ScctHudStyle::Text(), 6.0f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ScctDashLine(ScctDashAttr(EScctDashboardText::LatestState), ScctUi::Small(), ScctHudStyle::TextDim(), 6.0f)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				ScctDashChip(TEXT("Read recent changes"), TEXT("The latest five releases of OpenSCCT with their notes."), [Host] { Host.Open(TEXT("whatsnew")); })
			]);
	}

	TSharedRef<SWidget> MakeExploreCard(const FScctMenuHost& Host)
	{
		return ScctUi::Card(TEXT("Explore"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				ScctDashChip(TEXT("Player website"), TEXT("Find servers, join through the launcher, see profiles and leaderboards, and manage your account's privacy, in your web browser."),
					[] { ScctDashOpenUrl(ScctDashboardWebsite); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				ScctDashChip(TEXT("Releases"), TEXT("Every release of OpenSCCT on GitHub, in your web browser."), [] { ScctDashOpenUrl(ScctDashboardReleases); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ScctDashChip(TEXT("Demos"), TEXT("Recorded matches: yours and the master's recent online demos."), [Host] { Host.Open(TEXT("demos")); })
			],
			FMargin(24.0f, 16.0f));
	}

	FString PracticeLine(bool bMerc)
	{
		const FString Map = ScctDashPracticeMap(nullptr, bMerc);
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		if (Map.IsEmpty() || Menu == nullptr)
		{
			return TEXT("No map can be played yet");
		}
		FString Title = Menu->GetMapTitle(Map);
		Title = Title.Left(1).ToUpper() + Title.Mid(1).ToLower();
		return FString::Printf(TEXT("%s with %d bots, practice rules"), *Title, ScctPracticeBots);
	}

	void StartPractice(const FScctMenuHost& Host, bool bMerc)
	{
		int32 Mode = 0;
		const FString Map = ScctDashPracticeMap(&Mode, bMerc);
		UScctOnlineSubsystem* Visit = Host.Online.Get();
		if (Map.IsEmpty() || Visit == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: no map to practise on"));
			return;
		}
		const TWeakObjectPtr<UScctOnlineSubsystem> WeakVisit = Visit;
		const auto Launch = [WeakVisit, Map, Mode, bMerc]
		{
			UScctOnlineSubsystem* Online = WeakVisit.Get();
			UWorld* World = Online != nullptr ? Online->GetWorld() : nullptr;
			if (World == nullptr || GEngine == nullptr)
			{
				return;
			}
			// The Play offline page's start (AScctGameMode reads these), with the offline practice rules.
			FString Url = FString::Printf(TEXT("%s?Mode=%s?Side=%s?Bots=%d?Difficulty=2?Practice=1"), *UScctOnlineSubsystem::LevelOf(Map),
				UScctMenuSubsystem::ModeKey(Mode), bMerc ? TEXT("merc") : TEXT("spy"), ScctPracticeBots);
			const FString Name = Online->GetPlayerName().Replace(TEXT("?"), TEXT("")).Replace(TEXT("#"), TEXT(""));
			if (!Name.IsEmpty())
			{
				Url += TEXT("?Name=") + Name;
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: practice as a %s: %s"), bMerc ? TEXT("mercenary") : TEXT("spy"), *Url);
			GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
		};
		// The map's prebuilt bot navigation is offered first, as on the Play offline page.
		ScctBotNavPrompt::OfferThenLaunch(Host.Dialogs, Map, [WeakVisit] { return WeakVisit.IsValid(); }, Launch);
	}

	TSharedRef<SScctMenuPage> MakeWhatsNewPage(const FScctMenuHost& Host)
	{
		return SNew(SScctWhatsNewPage, Host, false);
	}

	TSharedRef<SScctMenuPage> MakeAnnouncementPage(const FScctMenuHost& Host)
	{
		return SNew(SScctWhatsNewPage, Host, true);
	}
}
