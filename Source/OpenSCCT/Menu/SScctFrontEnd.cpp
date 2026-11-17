#include "Menu/SScctFrontEnd.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Menu/SScctHostPage.h"
#include "Menu/SScctLobbyPage.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
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
	const TCHAR* const MenuUserSection = TEXT("OpenSCCT.Online");

	// The offline page's choices: what the player reads and what AScctGameMode's URL options take.
	const TCHAR* const ModeLabels[] = { TEXT("Story"), TEXT("Disk Hunt"), TEXT("Deathmatch"), TEXT("Merc Deathmatch") };
	const TCHAR* const ModeKeys[] = { TEXT("Story"), TEXT("DiskHunt"), TEXT("Deathmatch"), TEXT("MercDeathMatch") };
	const int32 BotCounts[] = { 0, 1, 2, 3, 5, 7 };
	const TCHAR* const SkillLabels[] = { TEXT("Easy"), TEXT("Normal"), TEXT("Hard"), TEXT("Expert") };
	const int32 JoinSides[] = { -1, 0, 1, 255 };

	constexpr float PageWidth = 560.0f;

	/** A server's mode as the lobby names it (EGameMode). */
	const TCHAR* ModeNameOf(int32 Mode)
	{
		switch (Mode)
		{
		case 4: return TEXT("Disk Hunt");
		case 5: return TEXT("Story");
		case 6: return TEXT("Deathmatch");
		case 7: return TEXT("Merc Deathmatch");
		default: return TEXT("");
		}
	}

	TArray<FString> ToStrings(TConstArrayView<const TCHAR*> Texts)
	{
		TArray<FString> Strings;
		for (const TCHAR* Text : Texts)
		{
			Strings.Emplace(Text);
		}
		return Strings;
	}

	/** A labelled block of a page: the label, a gap, the content, a larger gap. */
	void AddBlock(const TSharedRef<SVerticalBox>& Box, const FString& LabelText, const TSharedRef<SWidget>& Content)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 7.0f)[ Ui::Label(LabelText) ];
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ Content ];
	}
}

void SScctFrontEnd::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;

	static const FSlateColorBrush Dim(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		// The backdrop is a lit level: a wash keeps the type readable over its bright places.
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(&Dim)
			.ColorAndOpacity(FLinearColor(0.0f, 0.004f, 0.01f, 0.5f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(96.0f, 60.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("OPENSCCT")))
				.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 46.0f, 420))
				.ColorAndOpacity(ScctHudStyle::Text())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 2.0f, 0.0f, 26.0f)
			[
				Ui::Label(TEXT("Spies versus mercenaries"), Ui::Accent())
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(PageBox, SBox)
			]
		]
	];
	Rebuild();
}

void SScctFrontEnd::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return;
	}

	// A visit takes the screen over, whichever page it was started from, and gives that page back at its end.
	const bool bVisiting = Visit->IsVisiting();
	if (bVisiting && Page != EPage::Visit)
	{
		PageBeforeVisit = Page;
		VisitView = FindVisitView();
		ShowPage(EPage::Visit);
	}
	else if (!bVisiting && Page == EPage::Visit)
	{
		VisitView = EVisitView::None;
		ShowPage(PageBeforeVisit);
	}
	else if (Page == EPage::Visit && FindVisitView() != VisitView)
	{
		VisitView = FindVisitView();
		Rebuild();
	}

	if (Visit->GetRevision() != SeenRevision)
	{
		SeenRevision = Visit->GetRevision();
		if (Page == EPage::Servers)
		{
			RefillServers();
		}
	}
}

SScctFrontEnd::EVisitView SScctFrontEnd::FindVisitView() const
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return EVisitView::None;
	}
	switch (Visit->GetState())
	{
	case EScctOnlineState::Connecting: return EVisitView::Connecting;
	case EScctOnlineState::Lobby: return EVisitView::Lobby;
	case EScctOnlineState::Travelling: return EVisitView::Travelling;
	case EScctOnlineState::Loading:
	case EScctOnlineState::Match:
	case EScctOnlineState::Results: return EVisitView::Round;
	default: return EVisitView::Failed;
	}
}

void SScctFrontEnd::ShowPageNamed(const FString& Name)
{
	if (Name.Equals(TEXT("offline"), ESearchCase::IgnoreCase))
	{
		ShowPage(EPage::Offline);
	}
	else if (Name.Equals(TEXT("join"), ESearchCase::IgnoreCase))
	{
		ShowPage(EPage::Join);
	}
	else if (Name.Equals(TEXT("servers"), ESearchCase::IgnoreCase))
	{
		ShowPage(EPage::Servers);
	}
	else if (Name.Equals(TEXT("host"), ESearchCase::IgnoreCase))
	{
		ShowPage(EPage::Host);
	}
	else if (Name.Equals(TEXT("start"), ESearchCase::IgnoreCase))
	{
		// Unattended: the offline page with its choices as they stand, and its START pressed.
		ShowPage(EPage::Offline);
		StartOffline();
	}
}

void SScctFrontEnd::ShowPage(EPage NewPage)
{
	Page = NewPage;
	if (Page == EPage::Servers && Online.IsValid())
	{
		Online->RequestServerList();
	}
	Rebuild();
}

void SScctFrontEnd::Rebuild()
{
	ServerRows.Reset();

	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	switch (Page)
	{
	case EPage::Home: Content = BuildHome(); break;
	case EPage::Offline: Content = BuildOffline(); break;
	case EPage::Join: Content = BuildJoin(); break;
	case EPage::Servers: Content = BuildServers(); break;
	case EPage::Visit: Content = BuildVisit(); break;
	case EPage::Host:
		// Create Session (Menu/SScctHostPage.h); a hosted game's lobby then takes the screen over as any visit does.
		Content = SNew(SScctHostPage).Online(Online).OnBack(FSimpleDelegate::CreateLambda([this] { ShowPage(EPage::Home); }));
		break;
	}
	PageBox->SetContent(Content);
	SeenRevision = -1;
}

TSharedRef<SWidget> SScctFrontEnd::Choice(const TArray<FString>& Labels, int32& Value, const FLinearColor& Colour)
{
	// The value lives in this widget, which outlives the buttons it owns.
	int32* const Picked = &Value;
	const TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		Row->AddSlot()
		[
			Ui::Button(Ui::ToText(Labels[Index].ToUpper()),
				FOnClicked::CreateLambda([Picked, Index] { *Picked = Index; return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([Picked, Index] { return *Picked == Index; }), Colour)
		];
	}
	return Row;
}

// ---- the menu's own pages -----------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctFrontEnd::BuildHome()
{
	const auto Entry = [this](const TCHAR* LabelText, EPage Target)
	{
		return Ui::Button(Ui::ToText(LabelText), FOnClicked::CreateSP(this, &SScctFrontEnd::HandleShowPage, Target), false, Ui::Accent(), 240.0f);
	};

	return SNew(SBox).WidthOverride(320.0f)
	[
		Ui::Glass(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Entry(TEXT("PLAY OFFLINE"), EPage::Offline) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Entry(TEXT("HOST GAME"), EPage::Host) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Entry(TEXT("JOIN BY ADDRESS"), EPage::Join) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 20.0f)[ Entry(TEXT("SERVER LIST"), EPage::Servers) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Ui::Button(Ui::ToText(TEXT("QUIT")), FOnClicked::CreateLambda([] { FPlatformMisc::RequestExit(false); return FReply::Handled(); }))
			])
	];
}

FReply SScctFrontEnd::HandleShowPage(EPage Target)
{
	ShowPage(Target);
	return FReply::Handled();
}

TSharedRef<SWidget> SScctFrontEnd::BuildOffline()
{
	if (Maps.IsEmpty())
	{
		Maps = UScctOnlineSubsystem::FindImportedMaps();
		// AquaE first if it is there: the map everything was first seen on.
		MapIndex = FMath::Max(Maps.IndexOfByKey(FString(TEXT("AquaE"))), 0);
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 20.0f)[ Ui::Title(Ui::ToText(TEXT("PLAY OFFLINE")), 24.0f) ];
	if (Maps.IsEmpty())
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
		[
			Ui::Text(Ui::ToText(TEXT("No map can be played yet: the OpenSCCT content folder was not found, or no map has been imported into this game.")),
				12.0f, FSlateColor(ScctHudStyle::Danger()), ScctHudStyle::EWeight::Regular, true)
		];
	}
	else
	{
		AddBlock(Box, TEXT("Map"), Choice(Maps, MapIndex, Ui::Accent()));
		AddBlock(Box, TEXT("Mode"), Choice(ToStrings(ModeLabels), ModeIndex, Ui::Accent()));
		AddBlock(Box, TEXT("Side"), SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				Ui::Button(Ui::ToText(TEXT("SPY")), FOnClicked::CreateLambda([this] { SideIndex = 0; return FReply::Handled(); }),
					TAttribute<bool>::CreateLambda([this] { return SideIndex == 0; }), ScctHudStyle::Accent(0), 110.0f)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("MERCENARY")), FOnClicked::CreateLambda([this] { SideIndex = 1; return FReply::Handled(); }),
					TAttribute<bool>::CreateLambda([this] { return SideIndex == 1; }), ScctHudStyle::Accent(1), 110.0f)
			]);

		TArray<FString> BotLabels;
		for (const int32 Count : BotCounts)
		{
			BotLabels.Add(FString::FromInt(Count));
		}
		AddBlock(Box, TEXT("Bots"), Choice(BotLabels, BotsIndex, Ui::Accent()));
		AddBlock(Box, TEXT("Bot skill"), Choice(ToStrings(SkillLabels), SkillIndex, Ui::Accent()));
	}

	Box->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("START")), FOnClicked::CreateLambda([this] { StartOffline(); return FReply::Handled(); }), !Maps.IsEmpty(), Ui::Accent(), 150.0f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateSP(this, &SScctFrontEnd::HandleShowPage, EPage::Home))
		]
	];
	return SNew(SBox).WidthOverride(PageWidth)[ Ui::Glass(Box) ];
}

void SScctFrontEnd::StartOffline()
{
	UWorld* World = Online.IsValid() ? Online->GetWorld() : nullptr;
	if (World == nullptr || GEngine == nullptr || !Maps.IsValidIndex(MapIndex))
	{
		return;
	}

	// AScctGameMode reads these (Play/ScctGameMode.h). A name with the URL's own separator would cut the options.
	FString Url = FString::Printf(TEXT("%s?Mode=%s?Side=%s?Bots=%d?Difficulty=%d"), *UScctOnlineSubsystem::LevelOf(Maps[MapIndex]),
		ModeKeys[ModeIndex], SideIndex == 1 ? TEXT("merc") : TEXT("spy"), BotCounts[BotsIndex], SkillIndex + 1);
	const FString Name = Online->GetPlayerName().Replace(TEXT("?"), TEXT(""));
	if (!Name.IsEmpty())
	{
		Url += TEXT("?Name=") + Name;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: starting an offline match: %s"), *Url);
	GEngine->SetClientTravel(World, *Url, TRAVEL_Absolute);
}

TSharedRef<SWidget> SScctFrontEnd::BuildJoin()
{
	FString LastAddress;
	GConfig->GetString(MenuUserSection, TEXT("LastAddress"), LastAddress, GGameUserSettingsIni);
	const FOnTextCommitted OnEnter = FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			StartJoin();
		}
	});
	NameBox = Ui::TextBox(TEXT("Player"), Online.IsValid() ? Online->GetPlayerName() : FString());
	AddressBox = Ui::TextBox(TEXT("host or host:port"), LastAddress, false, OnEnter);
	PasswordBox = Ui::TextBox(TEXT("only if the server asks for one"), FString(), true, OnEnter);

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 20.0f)[ Ui::Title(Ui::ToText(TEXT("JOIN BY ADDRESS")), 24.0f) ];
	AddBlock(Box, TEXT("Your name"), NameBox.ToSharedRef());
	AddBlock(Box, TEXT("Server address"), AddressBox.ToSharedRef());
	AddBlock(Box, TEXT("Password"), PasswordBox.ToSharedRef());
	AddBlock(Box, TEXT("Side"), Choice({ TEXT("Smaller side"), TEXT("Spy"), TEXT("Mercenary"), TEXT("Watch") }, JoinSideIndex, Ui::Accent()));
	Box->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("CONNECT")), FOnClicked::CreateLambda([this] { StartJoin(); return FReply::Handled(); }), true, Ui::Accent(), 150.0f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateSP(this, &SScctFrontEnd::HandleShowPage, EPage::Home))
		]
	];
	return SNew(SBox).WidthOverride(PageWidth)[ Ui::Glass(Box) ];
}

void SScctFrontEnd::StartJoin()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !AddressBox.IsValid())
	{
		return;
	}

	const FString Address = AddressBox->GetText().ToString().TrimStartAndEnd();
	if (Address.IsEmpty())
	{
		FSlateApplication::Get().SetKeyboardFocus(AddressBox);
		return;
	}
	Visit->SetPlayerName(NameBox->GetText().ToString());
	GConfig->SetString(MenuUserSection, TEXT("LastAddress"), *Address, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	Visit->Join(Address, PasswordBox->GetText().ToString(), JoinSides[JoinSideIndex]);
}

TSharedRef<SWidget> SScctFrontEnd::BuildServers()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Ui::Title(Ui::ToText(TEXT("SERVER LIST")), 24.0f) ];

	// What the master is doing, or why there is no list.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([Weak]
		{
			const UScctOnlineSubsystem* Visit = Weak.Get();
			if (Visit == nullptr || !Visit->HasMaster())
			{
				return Ui::ToText(TEXT("No master server is set for this game ([OpenSCCT.Online] MasterUrl in Config/DefaultGame.ini)."));
			}
			switch (static_cast<EScctOnlineMasterState>(Visit->GetStatus().MasterState))
			{
			case EScctOnlineMasterState::Busy: return Ui::ToText(TEXT("Asking the master server..."));
			case EScctOnlineMasterState::Failed: return Ui::ToText(Visit->GetMasterError());
			case EScctOnlineMasterState::Ready: return FText::FromString(FString::Printf(TEXT("%d server(s) online"), Visit->GetServers().Num()));
			default: return FText::GetEmpty();
			}
		}), 11.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];
	Box->AddSlot().AutoHeight().MaxHeight(420.0f)
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()[ SAssignNew(ServerRows, SVerticalBox) ]
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 14.0f)[ Ui::Rule() ];

	// The account: the official servers take signed-in players only.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([Weak]
			{
				const UScctOnlineSubsystem* Visit = Weak.Get();
				if (Visit == nullptr)
				{
					return FText::GetEmpty();
				}
				switch (static_cast<EScctOnlineAccountState>(Visit->GetStatus().AccountState))
				{
				case EScctOnlineAccountState::SignedIn: return FText::FromString(FString::Printf(TEXT("Signed in as %s"), *Visit->GetAccountName()));
				case EScctOnlineAccountState::SigningIn:
					return Visit->GetSignInCode().IsEmpty()
						? Ui::ToText(TEXT("Opening the sign-in page in your browser..."))
						: FText::FromString(FString::Printf(TEXT("Finish signing in in your browser. Code: %s"), *Visit->GetSignInCode()));
				case EScctOnlineAccountState::Failed: return Ui::ToText(Visit->GetAccountError());
				default: return Ui::ToText(TEXT("Not signed in. Servers marked ACCOUNT take signed-in players only."));
				}
			}), 11.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(12.0f, 0.0f, 0.0f, 0.0f)
		[
			Ui::Button(TAttribute<FText>::CreateLambda([Weak]
			{
				return Ui::ToText(Weak.IsValid() && Weak->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn) ? TEXT("SIGN OUT") : TEXT("SIGN IN"));
			}), FOnClicked::CreateLambda([Weak]
			{
				if (UScctOnlineSubsystem* Visit = Weak.Get())
				{
					if (Visit->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn))
					{
						Visit->SignOut();
					}
					else
					{
						Visit->BeginSignIn();
					}
				}
				return FReply::Handled();
			}))
		]
	];
	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("REFRESH")), FOnClicked::CreateLambda([Weak]
			{
				if (Weak.IsValid())
				{
					Weak->RequestServerList();
				}
				return FReply::Handled();
			}), false, Ui::Accent(), 150.0f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateSP(this, &SScctFrontEnd::HandleShowPage, EPage::Home))
		]
	];
	return SNew(SBox).WidthOverride(760.0f)[ Ui::Glass(Box) ];
}

void SScctFrontEnd::RefillServers()
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !ServerRows.IsValid())
	{
		return;
	}

	ServerRows->ClearChildren();
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const TArray<FScctOnlineServerEntry>& Servers = Visit->GetServers();
	for (int32 Index = 0; Index < Servers.Num(); ++Index)
	{
		const FScctOnlineServerEntry& Server = Servers[Index];
		FString Marks;
		if ((Server.Flags & EScctOnlineServerFlags::Password) != 0) { Marks += TEXT("  PASSWORD"); }
		if ((Server.Flags & EScctOnlineServerFlags::RequireAccounts) != 0) { Marks += TEXT("  ACCOUNT"); }
		if ((Server.Flags & EScctOnlineServerFlags::SameBuild) == 0) { Marks += TEXT("  OTHER VERSION"); }
		if ((Server.Flags & EScctOnlineServerFlags::Full) != 0) { Marks += TEXT("  FULL"); }
		const FString Line = FString::Printf(TEXT("%s    %s  %s    %d/%d%s"), Server.Name, Server.Map, ModeNameOf(Server.Mode), Server.Players, Server.MaxPlayers, *Marks);
		ServerRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			Ui::Button(Ui::ToText(Line), FOnClicked::CreateLambda([Weak, Index]
			{
				if (Weak.IsValid())
				{
					Weak->JoinListed(Index);
				}
				return FReply::Handled();
			}))
		];
	}
}

// ---- the visit ----------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctFrontEnd::BuildVisit()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return SNullWidget::NullWidget;
	}
	if (VisitView == EVisitView::Lobby)
	{
		// The lobby is its own page (Menu/SScctLobbyPage.h): setup, launch feedback, Session Options.
		return SNew(SScctLobbyPage).Online(Online);
	}

	FString Heading, LeaveLabel = TEXT("LEAVE");
	TAttribute<FText> Body = FText::GetEmpty();
	FLinearColor BodyColour = ScctHudStyle::TextDim();
	switch (VisitView)
	{
	case EVisitView::Connecting:
		Heading = TEXT("CONNECTING");
		LeaveLabel = TEXT("CANCEL");
		// What the join has tried so far, one attempt per line.
		Body = TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? Ui::ToText(Weak->GetConnectLog()) : FText::GetEmpty(); });
		break;
	case EVisitView::Travelling:
		Heading = Visit->GetMapToLoad().IsEmpty() ? TEXT("RETURNING TO THE LOBBY") : FString::Printf(TEXT("TRAVELLING TO %s"), *Visit->GetMapToLoad().ToUpper());
		Body = Ui::ToText(Visit->GetServerName());
		break;
	case EVisitView::Round:
		if (Visit->HasFlag(EScctOnlineStatusFlags::RoundRefused))
		{
			Heading = TEXT("ROUND IN PROGRESS");
			Body = Ui::ToText(TEXT("The server is playing a map that is not installed here. You are back in the lobby when the round is over."));
		}
		else
		{
			Heading = FString::Printf(TEXT("JOINING THE ROUND ON %s"), *Visit->GetMapToLoad().ToUpper());
			Body = Ui::ToText(Visit->GetServerName());
		}
		break;
	default:
		Heading = Visit->GetState() == EScctOnlineState::Left ? TEXT("YOU LEFT THE SERVER") : TEXT("CONNECTION FAILED");
		LeaveLabel = TEXT("BACK");
		Body = TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? Ui::ToText(Weak->GetNotice()) : FText::GetEmpty(); });
		BodyColour = ScctHudStyle::Text();
		break;
	}

	return SNew(SBox).WidthOverride(PageWidth)
	[
		Ui::Glass(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
			[
				SNew(STextBlock)
				.Text(Ui::ToText(Heading))
				.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 22.0f, 200))
				.ColorAndOpacity(VisitView == EVisitView::Failed ? ScctHudStyle::Danger() : ScctHudStyle::Text())
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 22.0f)
			[
				Ui::Text(Body, 12.5f, FSlateColor(BodyColour), ScctHudStyle::EWeight::Regular, true)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
			[
				Ui::Button(Ui::ToText(LeaveLabel), FOnClicked::CreateLambda([Weak]
				{
					if (Weak.IsValid())
					{
						Weak->Leave();
					}
					return FReply::Handled();
				}), false, Ui::Accent(), 120.0f)
			])
	];
}
