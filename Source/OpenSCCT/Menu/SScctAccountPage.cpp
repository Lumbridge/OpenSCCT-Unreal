#include "Menu/SScctAccountPage.h"

#include "Brushes/SlateColorBrush.h"
#include "Menu/ScctMenuStyle.h"
#include "Menu/SScctRankIcon.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Social/ScctAccountSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace AccountUi = ScctMenuStyle;

namespace
{
	constexpr float AcctNarrowWidth = 640.0f;
	constexpr float AcctWideWidth = 1060.0f;
	constexpr float AcctColumnGap = 18.0f;

	/** OpenSCCT's PlayerNames.MinClaimLength and MaxClaimLength, for the help line. */
	constexpr int32 AcctMinNameLength = 3;
	constexpr int32 AcctMaxNameLength = 16;

	/** OpenSCCT's ProgressionK.MaxLevel and AcctMaxPrestige. */
	constexpr int32 AcctMaxLevel = 55;
	constexpr int32 AcctMaxPrestige = 10;

	TSharedRef<SWidget> AcctNote(const TAttribute<FText>& Text)
	{
		return AccountUi::Text(Text, 10.5f, FSlateColor(ScctHudStyle::TextFaint()), ScctHudStyle::EWeight::Regular, true);
	}

	TSharedRef<SWidget> AcctLine(const FString& Text, float Size = 12.0f, const FLinearColor& Colour = ScctHudStyle::Text(),
		ScctHudStyle::EWeight Weight = ScctHudStyle::EWeight::Regular)
	{
		return AccountUi::Text(AccountUi::ToText(Text), Size, FSlateColor(Colour), Weight, true);
	}

	/** A card of the page: a dim panel with a small caption. */
	TSharedRef<SWidget> AcctCard(const FString& Caption, const TSharedRef<SWidget>& Content)
	{
		static const FSlateColorBrush Panel(FLinearColor::White);
		return SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(&Panel).ColorAndOpacity(FLinearColor(0.02f, 0.035f, 0.05f, 0.55f)) ]
			+ SOverlay::Slot().Padding(16.0f, 12.0f, 16.0f, 14.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ AccountUi::Label(Caption) ]
				+ SVerticalBox::Slot().AutoHeight()[ Content ]
			];
	}

	/** One row of the career snapshot: what, then the number at the right. */
	TSharedRef<SWidget> AcctStatRow(const FString& What, int32 Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[ AcctLine(What, 12.0f, ScctHudStyle::TextDim()) ]
			+ SHorizontalBox::Slot().AutoWidth()[ AcctLine(FText::AsNumber(Value).ToString(), 12.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Bold) ];
	}

	/** A bar filled to Fraction in Colour. */
	TSharedRef<SWidget> AcctBar(float Fraction, const FLinearColor& Colour)
	{
		static const FSlateColorBrush Fill(FLinearColor::White);
		Fraction = FMath::Clamp(Fraction, 0.0f, 1.0f);
		return SNew(SBox).HeightOverride(8.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(&Fill).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f)) ]
			+ SOverlay::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(FMath::Max(Fraction, 0.0001f))[ SNew(SImage).Image(&Fill).ColorAndOpacity(Colour) ]
				+ SHorizontalBox::Slot().FillWidth(FMath::Max(1.0f - Fraction, 0.0001f))[ SNullWidget::NullWidget ]
			]
		];
	}

	/** A row of action buttons with gaps. */
	TSharedRef<SHorizontalBox> AcctActions()
	{
		return SNew(SHorizontalBox);
	}

	void AcctAddAction(const TSharedRef<SHorizontalBox>& Row, const TSharedRef<SWidget>& Button)
	{
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Button ];
	}
}

void SScctAccountPage::Construct(const FArguments& InArgs)
{
	Account = InArgs._Account;
	Online = InArgs._Online;
	OnBack = InArgs._OnBack;
	OnOpen = InArgs._OnOpen;
	FParse::Value(FCommandLine::Get(), TEXT("ScctAccountPress="), AutoPress);

	NameBox = AccountUi::TextBox(TEXT("your name"), FString(), false, FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			Claim();
		}
	}));

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	// The menus' page host draws the page's title above it.
	Box->AddSlot().AutoHeight()
	[
		SNew(SBox).MaxDesiredHeight(690.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()[ SAssignNew(ContentBox, SBox) ]
		]
	];

	// The page's last message: a claim, a refusal, a sign-out.
	Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 12.0f)
	[
		AccountUi::Text(TAttribute<FText>::CreateSP(this, &SScctAccountPage::StatusLine), 12.0f, FSlateColor(AccountUi::Accent()), ScctHudStyle::EWeight::Regular, true)
	];

	const TWeakObjectPtr<UScctAccountSubsystem> Weak = Account;
	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			AccountUi::Button(AccountUi::ToText(TEXT("REFRESH")), FOnClicked::CreateLambda([Weak]
			{
				if (Weak.IsValid())
				{
					Weak->Refresh(true);
				}
				return FReply::Handled();
			}))
		]
		// The career pages (the Godot account page's Full stats and Match history, and its Achievements).
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			AccountUi::Button(AccountUi::ToText(TEXT("FULL STATS")), FOnClicked::CreateLambda([this] { if (OnOpen) { OnOpen(TEXT("stats")); } return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			AccountUi::Button(AccountUi::ToText(TEXT("MATCH HISTORY")), FOnClicked::CreateLambda([this] { if (OnOpen) { OnOpen(TEXT("history")); } return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			AccountUi::Button(AccountUi::ToText(TEXT("ACHIEVEMENTS")), FOnClicked::CreateLambda([this] { if (OnOpen) { OnOpen(TEXT("achievements")); } return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
		[
			AccountUi::Button(AccountUi::ToText(TEXT("BACK")), FOnClicked::CreateLambda([this] { OnBack.ExecuteIfBound(); return FReply::Handled(); }))
		]
	];

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride_Lambda([Weak]
		{
			return Weak.IsValid() && Weak->GetView() == EScctAccountView::SignedIn ? AcctWideWidth : AcctNarrowWidth;
		})
		[
			AccountUi::Glass(Box)
		]
	];

	if (UScctAccountSubsystem* Subsystem = Account.Get())
	{
		ChangedHandle = Subsystem->OnChanged.AddLambda([this] { bDirty = true; });
		// The page shows the account as the master has it now (at most once a minute).
		Subsystem->Refresh(false);
	}
	Rebuild();
}

SScctAccountPage::~SScctAccountPage()
{
	if (UScctAccountSubsystem* Subsystem = Account.Get())
	{
		Subsystem->OnChanged.Remove(ChangedHandle);
	}
}

void SScctAccountPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bDirty)
	{
		Rebuild();
	}

	if (!AutoPress.IsEmpty())
	{
		AutoPressTimer -= InDeltaTime;
		if (AutoPressTimer <= 0.0f)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Account page: pressing %s (-ScctAccountPress)"), *AutoPress);
			if (AutoPress == TEXT("changename"))
			{
				bChangingName = true;
			}
			else if (AutoPress == TEXT("prestige"))
			{
				bPrestigeArmed = true;
			}
			else if (AutoPress == TEXT("locked"))
			{
				bShowLocked = true;
			}
			AutoPress.Reset();
			Rebuild();
		}
	}
}

FText SScctAccountPage::StatusLine() const
{
	const UScctAccountSubsystem* Subsystem = Account.Get();
	if (Subsystem == nullptr)
	{
		return AccountUi::ToText(TEXT("Accounts are not available in this build."));
	}
	const FString Status = Subsystem->GetText(EScctAccountText::Status);
	if (!Status.IsEmpty())
	{
		return AccountUi::ToText(Status);
	}
	// A failed sign-in (an expired code, a refusal) is the online subsystem's.
	if (Subsystem->GetView() == EScctAccountView::SignedOut && Online.IsValid())
	{
		return AccountUi::ToText(Online->GetAccountError());
	}
	return FText::GetEmpty();
}

void SScctAccountPage::Rebuild()
{
	bDirty = false;
	const UScctAccountSubsystem* Subsystem = Account.Get();
	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	if (Subsystem == nullptr || !Subsystem->IsReady())
	{
		Content = AcctLine(TEXT("Connecting to the account service..."), 13.0f, ScctHudStyle::TextDim());
	}
	else
	{
		EScctAccountView View = Subsystem->GetView();
		if (View == EScctAccountView::SignedIn && bChangingName)
		{
			View = EScctAccountView::PickName;
		}
		if (View != EScctAccountView::PickName && View != EScctAccountView::SignedIn)
		{
			bChangingName = false;
		}
		switch (View)
		{
		case EScctAccountView::NoMaster: Content = BuildNoMaster(); break;
		case EScctAccountView::SignedOut: Content = BuildSignedOut(); break;
		case EScctAccountView::SigningIn: Content = BuildSigningIn(); break;
		case EScctAccountView::PickName: Content = BuildPickName(); break;
		default: Content = BuildSignedIn(); break;
		}
	}
	ContentBox->SetContent(Content);
}

// ---- signed out -----------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctAccountPage::BuildNoMaster()
{
	return AcctCard(TEXT("Play your way"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ AcctLine(TEXT("Connect your account"), 22.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Light) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ AcctLine(TEXT("This game has no master server set, so there are no accounts to sign in to.")) ]
		+ SVerticalBox::Slot().AutoHeight()[ AcctNote(AccountUi::ToText(TEXT("Offline and LAN play remain available without an account."))) ]);
}

TSharedRef<SWidget> SScctAccountPage::BuildSignedOut()
{
	const auto Benefit = [](const FString& Caption, const FString& Text)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ AcctLine(Caption, 13.0f, AccountUi::Accent(), ScctHudStyle::EWeight::Bold) ]
			+ SVerticalBox::Slot().AutoHeight()[ AcctNote(AccountUi::ToText(Text)) ];
	};
	const TWeakObjectPtr<UScctAccountSubsystem> Weak = Account;
	return AcctCard(TEXT("Your OpenSCCT account"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ AcctLine(TEXT("Make your progress count"), 22.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Light) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ AcctLine(TEXT("Sign in with Discord to claim your player name and keep your career with you.")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 14.0f, 0.0f)[ Benefit(TEXT("Your identity"), TEXT("Reserve your name and join servers that require an account.")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 14.0f, 0.0f)[ Benefit(TEXT("Your progression"), TEXT("Track stats, earn achievements and unlock cosmetics.")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)[ Benefit(TEXT("Your settings"), TEXT("Take your controls and preferences to another PC.")) ]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			AccountUi::Button(AccountUi::ToText(TEXT("SIGN IN WITH DISCORD")), FOnClicked::CreateLambda([Weak]
			{
				if (Weak.IsValid())
				{
					Weak->BeginSignIn();
				}
				return FReply::Handled();
			}), true, AccountUi::Accent(), 240.0f)
		]
		+ SVerticalBox::Slot().AutoHeight()[ AcctNote(AccountUi::ToText(TEXT("You can still play on servers that allow guests, offline and on LAN without signing in."))) ]);
}

TSharedRef<SWidget> SScctAccountPage::BuildSigningIn()
{
	const TWeakObjectPtr<UScctAccountSubsystem> Weak = Account;
	const TWeakObjectPtr<UScctOnlineSubsystem> WeakOnline = Online;
	return AcctCard(TEXT("Discord sign-in"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ AcctLine(TEXT("Finish signing in with your browser"), 20.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Light) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ AcctLine(TEXT("Check the browser shows this code, then sign in with Discord. This page will update by itself.")) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			AccountUi::Text(TAttribute<FText>::CreateLambda([Weak]
			{
				return Weak.IsValid() ? AccountUi::ToText(Weak->GetText(EScctAccountText::SignInCode)) : FText::GetEmpty();
			}), 32.0f, FSlateColor(AccountUi::Accent()), ScctHudStyle::EWeight::Light)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ AcctNote(AccountUi::ToText(TEXT("Waiting for Discord…"))) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			AccountUi::Button(AccountUi::ToText(TEXT("CANCEL SIGN-IN")), FOnClicked::CreateLambda([WeakOnline]
			{
				// Forgetting the sign-in drops the code being waited on.
				if (WeakOnline.IsValid())
				{
					WeakOnline->SignOut();
				}
				return FReply::Handled();
			}))
		]);
}

// ---- the name -------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctAccountPage::BuildPickName()
{
	const UScctAccountSubsystem* Subsystem = Account.Get();
	if (NameBox->GetText().IsEmpty())
	{
		// The claimed name to change, or the name this player joins servers under as a start.
		FString Start = Subsystem->GetText(EScctAccountText::Name);
		if (Start.IsEmpty() && Online.IsValid())
		{
			Start = Online->GetPlayerName();
		}
		NameBox->SetText(AccountUi::ToText(Start));
	}

	const bool bClaiming = Subsystem->HasFlag(EScctAccountFlags::Claiming);
	const TSharedRef<SHorizontalBox> Buttons = AcctActions();
	const TSharedRef<SButton> ClaimButton = AccountUi::Button(AccountUi::ToText(bChangingName ? TEXT("SAVE NAME") : TEXT("CLAIM NAME")),
		FOnClicked::CreateLambda([this] { Claim(); return FReply::Handled(); }), true, AccountUi::Accent(), 160.0f);
	ClaimButton->SetEnabled(!bClaiming);
	AcctAddAction(Buttons, ClaimButton);
	if (bChangingName)
	{
		AcctAddAction(Buttons, AccountUi::Button(AccountUi::ToText(TEXT("CANCEL")), FOnClicked::CreateLambda([this]
		{
			bChangingName = false;
			Rebuild();
			return FReply::Handled();
		})));
	}
	else
	{
		const TWeakObjectPtr<UScctAccountSubsystem> Weak = Account;
		AcctAddAction(Buttons, AccountUi::Button(AccountUi::ToText(TEXT("SIGN OUT")), FOnClicked::CreateLambda([Weak]
		{
			if (Weak.IsValid())
			{
				Weak->SignOut();
			}
			return FReply::Handled();
		})));
	}

	return AcctCard(bChangingName ? TEXT("Your identity") : TEXT("One last step"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			AcctLine(bChangingName ? TEXT("Change your player name") : TEXT("Choose your player name"), 22.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Light)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			AcctLine(FString::Printf(TEXT("The name other players see. %d to %d letters, digits, _ - and .; nobody else can use it while it is yours."), AcctMinNameLength, AcctMaxNameLength))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)[ NameBox.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight()[ Buttons ]);
}

void SScctAccountPage::Claim()
{
	if (UScctAccountSubsystem* Subsystem = Account.Get())
	{
		const FString Name = NameBox->GetText().ToString().TrimStartAndEnd();
		if (Subsystem->ClaimName(Name))
		{
			// The signed-in view comes back once the master took the name.
			bChangingName = false;
		}
	}
}

// ---- signed in ------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctAccountPage::BuildSignedIn()
{
	NameBox->SetText(FText::GetEmpty());
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, AcctColumnGap)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, AcctColumnGap, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, AcctColumnGap)[ BuildIdentity() ]
				+ SVerticalBox::Slot().AutoHeight()[ BuildCareer() ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				BuildRank()
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[ BuildTitles() ];
}

TSharedRef<SWidget> SScctAccountPage::BuildIdentity()
{
	UScctAccountSubsystem* Subsystem = Account.Get();
	const bool bLocked = Subsystem->HasFlag(EScctAccountFlags::NameLocked);
	const TWeakObjectPtr<UScctAccountSubsystem> Weak = Account;

	const TSharedRef<SHorizontalBox> Buttons = AcctActions();
	const TSharedRef<SButton> Change = AccountUi::Button(AccountUi::ToText(TEXT("CHANGE NAME")), FOnClicked::CreateLambda([this]
	{
		bChangingName = true;
		Rebuild();
		return FReply::Handled();
	}));
	Change->SetEnabled(!bLocked && !Subsystem->HasFlag(EScctAccountFlags::Preview));
	AcctAddAction(Buttons, Change);
	const TSharedRef<SButton> Out = AccountUi::Button(AccountUi::ToText(TEXT("SIGN OUT")), FOnClicked::CreateLambda([Weak]
	{
		if (Weak.IsValid())
		{
			Weak->SignOut();
		}
		return FReply::Handled();
	}));
	Out->SetEnabled(!Subsystem->HasFlag(EScctAccountFlags::SigningOut));
	AcctAddAction(Buttons, Out);

	// Where the settings sync stands.
	FString Sync;
	switch (static_cast<EScctProfileSyncState>(Subsystem->GetInfo().SyncState))
	{
	case EScctProfileSyncState::Idle: Sync = TEXT("Settings and controls are in step with your account."); break;
	case EScctProfileSyncState::Queued: Sync = TEXT("A change goes to your account in a moment."); break;
	case EScctProfileSyncState::Sending: Sync = TEXT("Sending your settings to your account..."); break;
	case EScctProfileSyncState::Pulling: Sync = TEXT("Taking your account's settings..."); break;
	default: Sync = Subsystem->HasFlag(EScctAccountFlags::Preview) ? TEXT("Preview: nothing is synced.") : TEXT("Settings sync is off for this run."); break;
	}

	FString Caption = Subsystem->HasFlag(EScctAccountFlags::Admin) ? TEXT("Signed in · Administrator") : TEXT("Signed in");
	if (Subsystem->HasFlag(EScctAccountFlags::Preview))
	{
		Caption += TEXT(" · Preview");
	}
	FString Name = Subsystem->GetText(EScctAccountText::Name);
	if (Name.IsEmpty())
	{
		Name = TEXT("Asking the master server…");
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ AcctLine(Name, 26.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Light) ];
	if (Subsystem->HasFlag(EScctAccountFlags::Known) && Subsystem->GetInfo().TitleChosen >= 0)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ AcctLine(Subsystem->GetText(EScctAccountText::Title), 12.0f, AccountUi::Accent()) ];
	}
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		AcctNote(AccountUi::ToText(TEXT("Settings and controls follow your account. Display, graphics, performance, network and device settings stay on this PC.")))
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ AcctLine(Sync, 11.0f, ScctHudStyle::TextDim()) ];
	if (Subsystem->HasFlag(EScctAccountFlags::RefreshFailed))
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ AcctLine(TEXT("The master server did not answer; this is your account as last seen."), 11.0f, ScctHudStyle::Danger()) ];
	}
	Box->AddSlot().AutoHeight()[ Buttons ];
	if (bLocked)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[ AcctNote(AccountUi::ToText(Subsystem->GetText(EScctAccountText::NameCooldown))) ];
	}
	return AcctCard(Caption, Box);
}

TSharedRef<SWidget> SScctAccountPage::BuildRank()
{
	UScctAccountSubsystem* Subsystem = Account.Get();
	const FScctAccountInfo& Info = Subsystem->GetInfo();
	if (!Subsystem->HasFlag(EScctAccountFlags::Known))
	{
		return AcctCard(TEXT("Rank & progression"), AcctLine(TEXT("Asking the master server…"), 12.0f, ScctHudStyle::TextDim()));
	}
	if (!Subsystem->HasFlag(EScctAccountFlags::HasProgress))
	{
		return AcctCard(TEXT("Rank & progression"), AcctLine(TEXT("This master server has no levels yet."), 12.0f, ScctHudStyle::TextDim()));
	}

	const FLinearColor Colour = UScctAccountSubsystem::ToColour(Info.RankColour);
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// The badge: the rank in its emblem's colour, and what it is.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SScctRankIcon).Size(52.0f).Rank(FScctRankEmblem{ Info.Level, Info.Prestige, Subsystem->HasFlag(EScctAccountFlags::Master) })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f)
		[
			AcctLine(Subsystem->GetText(EScctAccountText::RankTag), 30.0f, Colour, ScctHudStyle::EWeight::Bold)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			AcctLine(Subsystem->GetText(EScctAccountText::RankDescription), 13.0f, ScctHudStyle::Text())
		]
	];
	const float Fraction = Info.XpForNext > 0 ? static_cast<float>(Info.XpIntoLevel) / static_cast<float>(Info.XpForNext) : 1.0f;
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ AcctBar(Fraction, Colour) ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)[ AcctLine(Subsystem->GetText(EScctAccountText::NextLevel), 13.0f) ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ AcctLine(Subsystem->GetText(EScctAccountText::TotalXp), 11.0f, ScctHudStyle::TextDim()) ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
	[
		AcctNote(AccountUi::ToText(TEXT("Earn XP through score, wins, objectives and achievements on official servers.")))
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ AcctNote(AccountUi::ToText(Subsystem->GetText(EScctAccountText::XpRules))) ];

	// Prestige: pressed once it says what happens, pressed again it is done.
	if (Subsystem->HasFlag(EScctAccountFlags::CanPrestige))
	{
		const int32 Next = Info.Prestige + 1;
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			AcctLine(bPrestigeArmed
				? FString::Printf(TEXT("Prestige %d puts you back to level 1 with a new emblem and title. Level-based pistols and finishes relock. Your achievements, mastery rewards, titles and stats stay. Press again to confirm."), Next)
				: Subsystem->GetText(EScctAccountText::PrestigeHelp), 12.0f, bPrestigeArmed ? ScctHudStyle::Accent(1) : ScctHudStyle::Text())
		];
		const TSharedRef<SHorizontalBox> Buttons = AcctActions();
		const TSharedRef<SButton> Press = AccountUi::Button(AccountUi::ToText(bPrestigeArmed ? FString(TEXT("CONFIRM PRESTIGE")) : FString::Printf(TEXT("ENTER PRESTIGE %d"), Next)),
			FOnClicked::CreateLambda([this] { PressPrestige(); return FReply::Handled(); }), bPrestigeArmed, ScctHudStyle::Accent(1), 190.0f);
		Press->SetEnabled(!Subsystem->HasFlag(EScctAccountFlags::Prestiging));
		AcctAddAction(Buttons, Press);
		if (bPrestigeArmed)
		{
			AcctAddAction(Buttons, AccountUi::Button(AccountUi::ToText(TEXT("CANCEL")), FOnClicked::CreateLambda([this]
			{
				bPrestigeArmed = false;
				Rebuild();
				return FReply::Handled();
			})));
		}
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Buttons ];
	}
	else if (Subsystem->HasFlag(EScctAccountFlags::Master))
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ AcctLine(Subsystem->GetText(EScctAccountText::PrestigeHelp), 12.0f, ScctHudStyle::TextDim()) ];
	}
	else
	{
		bPrestigeArmed = false;
	}

	// The prestige emblems: the ones reached in their colours, the others faint.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ AccountUi::Label(TEXT("Prestige emblems")) ];
	const TSharedRef<SWrapBox> Emblems = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5.0f, 5.0f));
	for (int32 P = 1; P <= AcctMaxPrestige + 1; ++P)
	{
		const bool bMasterEmblem = P > AcctMaxPrestige;
		const bool bReached = bMasterEmblem ? Subsystem->HasFlag(EScctAccountFlags::Master) : Info.Prestige >= P;
		Emblems->AddSlot()
		[
			SNew(SBox).WidthOverride(34.0f).HeightOverride(34.0f)
			.ToolTipText(AccountUi::ToText(bMasterEmblem
				? FString::Printf(TEXT("Master Prestige%s"), bReached ? TEXT("") : *FString::Printf(TEXT(" (reach level %d at prestige %d)"), AcctMaxLevel, AcctMaxPrestige))
				: FString::Printf(TEXT("Prestige %d%s"), P, bReached ? TEXT("") : TEXT(" (not reached yet)"))))
			[
				SNew(SScctRankIcon).Size(34.0f).Fade(bReached ? 1.0f : 0.25f).Rank(bMasterEmblem ? FScctRankEmblem::Master() : FScctRankEmblem::OfPrestige(P))
			]
		];
	}
	Box->AddSlot().AutoHeight()[ Emblems ];
	return AcctCard(TEXT("Rank & progression"), Box);
}

void SScctAccountPage::PressPrestige()
{
	if (!bPrestigeArmed)
	{
		bPrestigeArmed = true;
		Rebuild();
		return;
	}
	if (UScctAccountSubsystem* Subsystem = Account.Get())
	{
		Subsystem->Prestige();
	}
	bPrestigeArmed = false;
	Rebuild();
}

TSharedRef<SWidget> SScctAccountPage::BuildCareer()
{
	const UScctAccountSubsystem* Subsystem = Account.Get();
	const FScctAccountInfo& Info = Subsystem->GetInfo();
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (Subsystem->HasFlag(EScctAccountFlags::Known))
	{
		const TPair<const TCHAR*, int32> Rows[] = {
			{ TEXT("Matches played"), Info.Matches }, { TEXT("Spy wins"), Info.SpyWins }, { TEXT("Mercenary wins"), Info.MercWins },
			{ TEXT("Losses"), Info.Losses }, { TEXT("Kills"), Info.Kills }, { TEXT("Deaths"), Info.Deaths } };
		for (const TPair<const TCHAR*, int32>& Row : Rows)
		{
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ AcctStatRow(Row.Key, Row.Value) ];
		}
	}
	Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[ AcctNote(AccountUi::ToText(Subsystem->GetText(EScctAccountText::CareerHelp))) ];
	return AcctCard(TEXT("Career snapshot"), Box);
}

TSharedRef<SWidget> SScctAccountPage::BuildTitles()
{
	UScctAccountSubsystem* Subsystem = Account.Get();
	if (!Subsystem->HasFlag(EScctAccountFlags::HasProgress))
	{
		return SNullWidget::NullWidget;
	}

	const TArray<FScctAccountTitleRow> Titles = Subsystem->GetTitles();
	const FScctAccountInfo& Info = Subsystem->GetInfo();
	const TWeakObjectPtr<UScctAccountSubsystem> Weak = Account;
	const auto TitleButton = [Weak](const FString& Label, const FString& Id, bool bChosen, bool bUnlocked, const FString& Tooltip)
	{
		const TSharedRef<SButton> Button = AccountUi::Button(AccountUi::ToText(bUnlocked ? Label : Label + TEXT(" (locked)")),
			FOnClicked::CreateLambda([Weak, Id]
			{
				if (Weak.IsValid())
				{
					Weak->SetTitle(Id);
				}
				return FReply::Handled();
			}), bChosen);
		Button->SetEnabled(bUnlocked);
		Button->SetToolTipText(AccountUi::ToText(Tooltip));
		return Button;
	};

	const TSharedRef<SWrapBox> Unlocked = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
	const TSharedRef<SWrapBox> Locked = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
	Unlocked->AddSlot()[ TitleButton(TEXT("None"), FString(), Info.TitleChosen < 0, true, TEXT("No title")) ];
	int32 LockedCount = 0;
	for (const FScctAccountTitleRow& Title : Titles)
	{
		if (Title.bUnlocked)
		{
			Unlocked->AddSlot()[ TitleButton(Title.Text, Title.Id, Title.bChosen, true, Title.HowToUnlock) ];
		}
		else
		{
			++LockedCount;
			Locked->AddSlot()[ TitleButton(Title.Text, Title.Id, false, false, Title.HowToUnlock) ];
		}
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ AcctLine(Subsystem->GetText(EScctAccountText::Title), 18.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Light) ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
	[
		AcctNote(AccountUi::ToText(FString::Printf(TEXT("Shown under your name in lobbies and the hub. %d of %d unlocked."), Info.TitlesUnlocked, Info.TitlesTotal)))
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Unlocked ];
	if (LockedCount > 0)
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 0.0f, 0.0f, bShowLocked ? 10.0f : 0.0f)
		[
			AccountUi::Button(AccountUi::ToText(FString::Printf(TEXT("LOCKED TITLES (%d)"), LockedCount)), FOnClicked::CreateLambda([this]
			{
				bShowLocked = !bShowLocked;
				Rebuild();
				return FReply::Handled();
			}), bShowLocked)
		];
		if (bShowLocked)
		{
			Box->AddSlot().AutoHeight()[ Locked ];
		}
	}
	return AcctCard(TEXT("Player title"), Box);
}
