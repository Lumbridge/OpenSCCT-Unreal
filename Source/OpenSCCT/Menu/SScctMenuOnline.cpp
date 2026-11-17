// Play online: the master's server list (sorted, filtered, with the chosen server's details), joining by address, and
// the full browser (Menu/SScctServerBrowserPage.h: the master, a LAN search and the saved servers in one table with
// measured pings, sorting, the side and connect path before JOIN). The browser tab is opened by the page name
// "online/browser" (or "browser", "lan"), and by -ScctBrowserJoin.

#include "Menu/ScctMenuPage.h"

#include "Framework/Application/SlateApplication.h"
#include "Menu/SScctServerBrowserPage.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const OnlineMenuSection = TEXT("OpenSCCT.Online");
	constexpr int32 RecentCount = 6;

	/** "host", "host:port" or "[v6]:port": a name or address of letters, digits, dots and dashes, a port of 1 to 65535. */
	bool OnlineIsServerAddress(const FString& Address)
	{
		FString HostPart = Address, Port;
		if (Address.StartsWith(TEXT("[")))
		{
			int32 Close = INDEX_NONE;
			if (!Address.FindChar(TEXT(']'), Close) || Close < 2)
			{
				return false;
			}
			HostPart = Address.Mid(1, Close - 1);
			const FString After = Address.Mid(Close + 1);
			if (!After.IsEmpty() && !After.StartsWith(TEXT(":")))
			{
				return false;
			}
			Port = After.Mid(1);
			for (const TCHAR Character : HostPart)
			{
				if (!FChar::IsHexDigit(Character) && Character != TEXT(':') && Character != TEXT('.')) { return false; }
			}
		}
		else
		{
			if (Address.Split(TEXT(":"), &HostPart, &Port) && Port.Contains(TEXT(":")))
			{
				return false;
			}
			if (HostPart.IsEmpty() || HostPart.Len() > 253)
			{
				return false;
			}
			for (const TCHAR Character : HostPart)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('.') && Character != TEXT('-')) { return false; }
			}
		}
		if (Address.EndsWith(TEXT(":")))
		{
			return false;
		}
		if (!Port.IsEmpty())
		{
			if (Port.Len() > 5) { return false; }
			for (const TCHAR Character : Port)
			{
				if (!FChar::IsDigit(Character)) { return false; }
			}
			const int32 Number = FCString::Atoi(*Port);
			return Number >= 1 && Number <= 65535;
		}
		return true;
	}

	FString OnlineModeName(int32 Mode)
	{
		switch (Mode)
		{
		case 4: return TEXT("Disk Hunt");
		case 5: return TEXT("Story");
		case 6: return TEXT("Deathmatch");
		case 7: return TEXT("Merc Deathmatch");
		default: return TEXT("—");
		}
	}

	FString OnlinePhaseName(int32 Phase)
	{
		switch (Phase)
		{
		case 0: return TEXT("In lobby");
		case 1: return TEXT("Loading");
		case 2: return TEXT("Playing");
		case 3: return TEXT("Results");
		default: return TEXT("—");
		}
	}

	FString OnlineMapTitle(const FString& Id)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		return Menu != nullptr ? Menu->GetMapTitle(Id) : Id.ToUpper();
	}

	class SScctMenuOnline : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuOnline) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost, bool bAddress)
		{
			Host = InHost;
			GConfig->GetBool(OnlineMenuSection, TEXT("HideFull"), bHideFull, GGameUserSettingsIni);
			GConfig->GetBool(OnlineMenuSection, TEXT("HideUnjoinable"), bHideUnjoinable, GGameUserSettingsIni);
			GConfig->GetBool(OnlineMenuSection, TEXT("HideLocked"), bHideLocked, GGameUserSettingsIni);

			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SAssignNew(Tabs, SScctTabBar)
					.Tabs({ TEXT("Server list"), TEXT("Join by address"), TEXT("LAN & saved servers") })
					.Initial(bAddress ? 1 : 0)
					.OnChanged(FSimpleDelegate::CreateLambda([this] { ShowTab(Tabs->GetTab()); }))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(Switcher, SWidgetSwitcher)
					+ SWidgetSwitcher::Slot()[ BuildServers() ]
					+ SWidgetSwitcher::Slot()[ BuildAddress() ]
					// Made when first shown: it starts a LAN search and pings every server it lists.
					+ SWidgetSwitcher::Slot()[ SAssignNew(BrowserBox, SBox) ]
				]
			];
			// An unattended browser join (-ScctBrowserJoin) starts on the browser.
			int32 BrowserJoin = -1;
			const bool bBrowserJoin = FParse::Value(FCommandLine::Get(), TEXT("ScctBrowserJoin="), BrowserJoin) && BrowserJoin >= 0;
			if (bBrowserJoin)
			{
				Tabs->SetTab(BrowserTab);
			}
			ShowTab(bBrowserJoin ? BrowserTab : bAddress ? 1 : 0);
			if (UScctOnlineSubsystem* Visit = Host.Online.Get())
			{
				Visit->RequestServerList();
			}
		}

		virtual FString GetTitle() const override { return TEXT("Play online"); }
		virtual FString GetKicker() const override { return TEXT("OpenSCCT servers"); }
		virtual bool HasTabs() const override { return true; }
		virtual void StepTab(int32 Direction) override { Tabs->Step(Direction); }

		virtual void ShowSub(const FString& Sub) override
		{
			const bool bBrowser = Sub.Equals(TEXT("browser"), ESearchCase::IgnoreCase) || Sub.Equals(TEXT("lan"), ESearchCase::IgnoreCase) || Sub.Equals(TEXT("saved"), ESearchCase::IgnoreCase);
			Tabs->SetTab(bBrowser ? BrowserTab : Sub.Equals(TEXT("address"), ESearchCase::IgnoreCase) || Sub.Equals(TEXT("join"), ESearchCase::IgnoreCase) ? 1 : 0);
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if ((Key == EKeys::R || Key == EKeys::F5 || Key == EKeys::Gamepad_FaceButton_Top) && Tabs->GetTab() == 0)
			{
				Refresh();
				return true;
			}
			return false;
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit != nullptr && Visit->GetRevision() != SeenRevision)
			{
				SeenRevision = Visit->GetRevision();
				RefillServers();
			}
		}

	private:
		void ShowTab(int32 Tab)
		{
			if (Tab == BrowserTab && !Browser.IsValid())
			{
				BrowserBox->SetContent(SAssignNew(Browser, SScctServerBrowserPage)
					.Online(Host.Online)
					.OnBack(FSimpleDelegate::CreateLambda([this] { if (Host.Back) { Host.Back(); } })));
			}
			Switcher->SetActiveWidgetIndex(Tab);
			DefaultFocus = Tab == 0 ? ServerFocus : Tab == 1 ? AddressFocus : TWeakPtr<SWidget>(Browser);
			if (const TSharedPtr<SWidget> Focus = DefaultFocus.Pin())
			{
				FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
			}
		}

		void Refresh()
		{
			if (UScctOnlineSubsystem* Visit = Host.Online.Get())
			{
				Visit->RequestServerList();
			}
		}

		bool IsSignedIn() const
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			return Visit != nullptr && Visit->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn);
		}

		/** Why the server cannot be joined from here, or empty. */
		FString WhyNot(const FScctOnlineServerEntry& Server) const
		{
			if ((Server.Flags & EScctOnlineServerFlags::SameBuild) == 0) { return FString::Printf(TEXT("It runs another version of OpenSCCT (%s)."), Server.Version); }
			if ((Server.Flags & EScctOnlineServerFlags::HasMap) == 0) { return FString::Printf(TEXT("Its map, %s, is not installed here."), *OnlineMapTitle(Server.Map)); }
			if ((Server.Flags & EScctOnlineServerFlags::SameMapVersion) == 0) { return TEXT("It plays another version of its map."); }
			if ((Server.Flags & EScctOnlineServerFlags::RequireAccounts) != 0 && !IsSignedIn()) { return TEXT("It takes signed-in players only: sign in below."); }
			if ((Server.Flags & EScctOnlineServerFlags::Full) != 0) { return TEXT("It is full."); }
			return FString();
		}

		// ---- the server list --------------------------------------------------------------------------------------

		TSharedRef<SWidget> BuildServers()
		{
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			TSharedPtr<SScctButton> RefreshButton;
			const auto Filter = [this](const TCHAR* Caption, const TCHAR* Description, bool* Value, const TCHAR* Key)
			{
				return SNew(SScctButton)
					.Look(EScctButtonLook::Chip)
					.Text(FText::FromString(FString(Caption).ToUpper()))
					.Description(FText::FromString(Description))
					.Selected(TAttribute<bool>::CreateLambda([Value] { return *Value; }))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Value, Key]
					{
						*Value = !*Value;
						GConfig->SetBool(OnlineMenuSection, Key, *Value, GGameUserSettingsIni);
						RefillServers();
					}));
			};

			const TSharedRef<SWidget> List = SNew(SVerticalBox)
				// What the master is doing, and the filters.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility(TAttribute<EVisibility>::CreateLambda([Weak] { return Weak.IsValid() && Weak->GetStatus().MasterState == static_cast<int32>(EScctOnlineMasterState::Busy) ? EVisibility::Visible : EVisibility::Collapsed; }))
						[
							SNew(SScctSpinner).Size(20.0f)
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([Weak]
						{
							const bool bFailed = Weak.IsValid() && Weak->GetStatus().MasterState == static_cast<int32>(EScctOnlineMasterState::Failed);
							return FSlateColor(bFailed ? ScctHudStyle::Danger() : ScctHudStyle::TextDim());
						}))
						.AutoWrapText(true)
						.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(StatusLine()); }))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						Filter(TEXT("Hide full"), TEXT("Leave out the servers that have no free place."), &bHideFull, TEXT("HideFull"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						Filter(TEXT("Hide locked"), TEXT("Leave out the servers that ask for a password."), &bHideLocked, TEXT("HideLocked"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						Filter(TEXT("Joinable only"), TEXT("Leave out the servers this game cannot join: another version, a map that is not installed."), &bHideUnjoinable, TEXT("HideUnjoinable"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(14.0f, 0.0f, 0.0f, 0.0f)
					[
						SAssignNew(RefreshButton, SScctButton)
						.Text(FText::FromString(TEXT("REFRESH")))
						.Description(FText::FromString(TEXT("Ask the master server for its list again (R, F5 or Y).")))
						.OnPressed(FSimpleDelegate::CreateLambda([this] { Refresh(); }))
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(Table, SScctTable)
					.Columns({
						{ TEXT("Server"), 1.0f, 0.0f, false, false },
						{ TEXT("Map"), 0.0f, 210.0f, false, false },
						{ TEXT("Mode"), 0.0f, 150.0f, false, false },
						{ ScctUi::GameText(TEXT("Browser_ServersList"), TEXT("ColumnHeadings[2]"), TEXT("Players")), 0.0f, 110.0f, true, true },
						{ TEXT("State"), 0.0f, 130.0f, false, false },
						{ TEXT("Region"), 0.0f, 80.0f, false, false },
					})
					.SortColumn(3)
					.SortDescending(true)
					.EmptyText(FText::FromString(TEXT("No server is listed. Refresh, or join a server by its address.")))
					.OnSelected(SScctTable::FOnRow::CreateLambda([this](int32) { RefreshDetails(); }))
					.OnActivated(SScctTable::FOnRow::CreateLambda([this](int32 Id) { JoinListed(Id); }))
				];
			ServerFocus = RefreshButton;

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					ScctUi::Panel(List, FMargin(20.0f, 18.0f, 12.0f, 14.0f), 0.72f)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(420.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 0.0f, 0.0f, 14.0f)[ SAssignNew(DetailsBox, SBox) ]
						+ SVerticalBox::Slot().AutoHeight()[ BuildAccount() ]
					]
				];
		}

		FString StatusLine() const
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr || !Visit->HasMaster())
			{
				return TEXT("No master server is set for this game ([OpenSCCT.Online] MasterUrl in Config/DefaultGame.ini). Join a server by its address instead.");
			}
			switch (static_cast<EScctOnlineMasterState>(Visit->GetStatus().MasterState))
			{
			case EScctOnlineMasterState::Busy: return TEXT("Asking the master server...");
			case EScctOnlineMasterState::Failed: return Visit->GetMasterError();
			case EScctOnlineMasterState::Ready:
				return FString::Printf(TEXT("%d server%s listed%s"), Visit->GetServers().Num(), Visit->GetServers().Num() == 1 ? TEXT("") : TEXT("s"),
					Shown < Visit->GetServers().Num() ? *FString::Printf(TEXT(", %d hidden by the filters"), Visit->GetServers().Num() - Shown) : TEXT(""));
			default: return FString();
			}
		}

		void RefillServers()
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr || !Table.IsValid())
			{
				return;
			}
			TArray<FScctTableRow> Rows;
			const TArray<FScctOnlineServerEntry>& Servers = Visit->GetServers();
			for (int32 Index = 0; Index < Servers.Num(); ++Index)
			{
				const FScctOnlineServerEntry& Server = Servers[Index];
				const bool bLocked = (Server.Flags & EScctOnlineServerFlags::Password) != 0;
				const bool bFull = (Server.Flags & EScctOnlineServerFlags::Full) != 0;
				const FString Why = WhyNot(Server);
				if ((bHideFull && bFull) || (bHideLocked && bLocked) || (bHideUnjoinable && !Why.IsEmpty() && !bFull))
				{
					continue;
				}
				FScctTableRow& Row = Rows.AddDefaulted_GetRef();
				FString Name = Server.Name;
				if (bLocked) { Name = TEXT("[locked] ") + Name; }
				if ((Server.Flags & EScctOnlineServerFlags::RequireAccounts) != 0) { Name += TEXT("  · signed-in only"); }
				FString Mode = OnlineModeName(Server.Mode);
				if ((Server.Flags & EScctOnlineServerFlags::Practice) != 0) { Mode += TEXT(" (practice)"); }
				Row.Cells = { Name, OnlineMapTitle(Server.Map), Mode, FString::Printf(TEXT("%d / %d"), Server.Players, Server.MaxPlayers), OnlinePhaseName(Server.Phase), FString(Server.Region).ToUpper() };
				Row.Id = Index;
				Row.bDim = !Why.IsEmpty();
				if ((Server.Flags & EScctOnlineServerFlags::Trusted) != 0)
				{
					Row.AccentCells = 1;
					Row.Accent = ScctUi::Accent();
				}
			}
			Shown = Rows.Num();
			Table->SetRows(Rows);
			RefreshDetails();
		}

		void RefreshDetails()
		{
			if (!DetailsBox.IsValid())
			{
				return;
			}
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			const int32 Index = Table.IsValid() ? Table->GetSelectedId() : INDEX_NONE;
			if (Visit == nullptr || !Visit->GetServers().IsValidIndex(Index))
			{
				DetailsBox->SetContent(ScctUi::Card(TEXT("Server"),
					SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text(FText::FromString(TEXT("Choose a server in the list to see its map, its rules and who plays there. Enter, A or a double click joins it.")))));
				return;
			}
			const FScctOnlineServerEntry& Server = Visit->GetServers()[Index];
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const FString Why = WhyNot(Server);
			const auto Fact = [](const FString& Name, const FString& Value)
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(110.0f)[ ScctUi::Label(Name, ScctHudStyle::TextFaint(), 11.0f) ] ]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Value)) ];
			};
			FString Marks;
			if ((Server.Flags & EScctOnlineServerFlags::Trusted) != 0) { Marks += TEXT("Official · "); }
			Marks += (Server.Flags & EScctOnlineServerFlags::Dedicated) != 0 ? TEXT("Dedicated") : TEXT("Hosted by a player");
			if ((Server.Flags & EScctOnlineServerFlags::Password) != 0) { Marks += TEXT(" · Password"); }

			DetailsBox->SetContent(ScctUi::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Picture(Menu != nullptr ? Menu->GetMapPicture(Server.Map, false) : nullptr, 0.0f, 200.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 18.0f, 24.0f, 2.0f)
				[
					SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).Text(FText::FromString(Server.Name))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 0.0f, 24.0f, 14.0f)
				[
					ScctUi::Label(Marks, ScctUi::Accent(), 11.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 2.0f)[ Fact(TEXT("Map"), OnlineMapTitle(Server.Map)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 2.0f)[ Fact(TEXT("Mode"), OnlineModeName(Server.Mode)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 2.0f)[ Fact(TEXT("Players"), FString::Printf(TEXT("%d of %d"), Server.Players, Server.MaxPlayers)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 2.0f)[ Fact(TEXT("State"), OnlinePhaseName(Server.Phase)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 2.0f)[ Fact(TEXT("Version"), Server.Version) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 14.0f, 24.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Danger()).AutoWrapText(true).Visibility(Why.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					.Text(FText::FromString(TEXT("Cannot join: ") + Why))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				+ SVerticalBox::Slot().AutoHeight().Padding(24.0f, 18.0f, 24.0f, 22.0f)
				[
					SNew(SScctButton)
					.Look(EScctButtonLook::Primary)
					.FillWidth(true)
					.IsEnabled(Why.IsEmpty() || (Server.Flags & EScctOnlineServerFlags::RequireAccounts) != 0)
					.Text(FText::FromString(TEXT("JOIN SERVER")))
					.Description(FText::FromString(TEXT("Join this server's lobby on the smaller side.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Index] { JoinListed(Index); }))
				],
				FMargin(0.0f), 0.74f));
		}

		void JoinListed(int32 Index)
		{
			UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr || !Visit->GetServers().IsValidIndex(Index))
			{
				return;
			}
			const FScctOnlineServerEntry& Server = Visit->GetServers()[Index];
			if ((Server.Flags & EScctOnlineServerFlags::RequireAccounts) != 0 && !IsSignedIn())
			{
				if (Host.Dialogs.IsValid())
				{
					Host.Dialogs->Toast(TEXT("This server takes signed-in players only. Sign in first."), ScctHudStyle::Danger());
				}
				return;
			}
			// The one password prompt of the menus, with the side asked for on arrival (as joining by address has
			// them): a protected server's password is typed here, and the side is a button.
			const bool bLocked = (Server.Flags & EScctOnlineServerFlags::Password) != 0;
			const FString Name = Server.Name;
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			if (!Host.Dialogs.IsValid())
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: joining listed server %s"), *Name);
				Visit->JoinListed(Index);
				return;
			}
			const auto JoinAs = [Weak, Index, Name](int32 Team)
			{
				return [Weak, Index, Name, Team](const FString& Password)
				{
					if (UScctOnlineSubsystem* V = Weak.Get(); V != nullptr && V->GetServers().IsValidIndex(Index))
					{
						UE_LOG(LogOpenSCCT, Log, TEXT("Menu: joining listed server %s (side %d%s)"), *Name, Team, Password.IsEmpty() ? TEXT("") : TEXT(", with a password"));
						V->JoinListed(Index, Password, Team);
					}
				};
			};
			FScctDialog Dialog;
			Dialog.Title = FString::Printf(TEXT("Join %s"), *Name);
			Dialog.Body = bLocked ? TEXT("This server asks for a password. Type it, then pick the side you want on arrival; you can change it in the lobby.")
				: TEXT("Pick the side you want on arrival; you can change it in the lobby.");
			Dialog.bTextField = bLocked;
			Dialog.bPassword = bLocked;
			Dialog.TextHint = TEXT("Password");
			Dialog.Buttons.Add({ TEXT("Cancel"), EScctButtonLook::Secondary, nullptr });
			Dialog.Buttons.Add({ ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 0, TEXT("Spy")), EScctButtonLook::Secondary, JoinAs(0) });
			Dialog.Buttons.Add({ ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 1, TEXT("Mercenary")), EScctButtonLook::Secondary, JoinAs(1) });
			Dialog.Buttons.Add({ TEXT("Smaller side"), EScctButtonLook::Primary, JoinAs(-1) });
			Dialog.FocusButton = 3;
			Dialog.CancelButton = 0;
			Host.Dialogs->Show(Dialog);
		}

		TSharedRef<SWidget> BuildAccount()
		{
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			return ScctUi::Card(TEXT("Account"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true)
					.Text(TAttribute<FText>::CreateLambda([Weak]
					{
						const UScctOnlineSubsystem* Visit = Weak.Get();
						if (Visit == nullptr || !Visit->HasMaster()) { return FText::FromString(TEXT("No master server: accounts are not used.")); }
						switch (static_cast<EScctOnlineAccountState>(Visit->GetStatus().AccountState))
						{
						case EScctOnlineAccountState::SignedIn: return FText::FromString(FString::Printf(TEXT("Signed in as %s"), *Visit->GetAccountName()));
						case EScctOnlineAccountState::SigningIn: return FText::FromString(TEXT("Finish signing in in your browser."));
						case EScctOnlineAccountState::Failed: return FText::FromString(Visit->GetAccountError());
						default: return FText::FromString(TEXT("Guest. The official servers take signed-in players only."));
						}
					}))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SNew(STextBlock).Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Bold, 26.0f, 300)).ColorAndOpacity(ScctUi::Accent())
					.Visibility(TAttribute<EVisibility>::CreateLambda([Weak] { return Weak.IsValid() && !Weak->GetSignInCode().IsEmpty() && Weak->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SigningIn) ? EVisibility::Visible : EVisibility::Collapsed; }))
					.Text(TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? FText::FromString(Weak->GetSignInCode()) : FText::GetEmpty(); }))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[
					SNew(SScctButton)
					.IsEnabled(TAttribute<bool>::CreateLambda([Weak] { return Weak.IsValid() && Weak->HasMaster(); }))
					.Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(IsSignedIn() ? TEXT("SIGN OUT") : TEXT("SIGN IN")); }))
					.Description(FText::FromString(TEXT("Sign in with your OpenSCCT account in your browser, or sign out.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Weak]
					{
						if (UScctOnlineSubsystem* Visit = Weak.Get())
						{
							IsSignedIn() ? Visit->SignOut() : Visit->BeginSignIn();
						}
					}))
				]);
		}

		// ---- join by address --------------------------------------------------------------------------------------

		TSharedRef<SWidget> BuildAddress()
		{
			FString LastAddress;
			GConfig->GetString(OnlineMenuSection, TEXT("LastAddress"), LastAddress, GGameUserSettingsIni);
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			const FOnTextCommitted OnEnter = FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter)
				{
					JoinAddress();
				}
			});
			NameBox = ScctUi::TextBox(TEXT("Player"), Visit != nullptr ? Visit->GetPlayerName() : FString(), false, OnEnter);
			AddressBox = ScctUi::TextBox(TEXT("host or host:port (port 28888 when left out)"), LastAddress, false, OnEnter);
			PasswordBox = ScctUi::TextBox(TEXT("only if the server asks for one"), FString(), true, OnEnter);
			NameBox->SetIsReadOnly(IsSignedIn());

			const auto Field = [](const FString& Caption, const TSharedRef<SWidget>& Box)
			{
				return SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 0.0f, 7.0f)[ ScctUi::Label(Caption, ScctHudStyle::TextDim(), 11.5f) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ Box ];
			};

			TSharedPtr<SScctButton> Connect;
			const TSharedRef<SWidget> Form = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ Field(TEXT("Your name"), NameBox.ToSharedRef()) ]
				+ SVerticalBox::Slot().AutoHeight()[ Field(ScctUi::GameText(TEXT("JoinWithIp"), TEXT("sserverip"), TEXT("Server Address")), AddressBox.ToSharedRef()) ]
				+ SVerticalBox::Slot().AutoHeight()[ Field(ScctUi::GameText(TEXT("Create_Match"), TEXT("PasswordCaption"), TEXT("Password")), PasswordBox.ToSharedRef()) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(-12.0f, 0.0f, -12.0f, 18.0f)
				[
					SNew(SScctStepper)
					.Label(FText::FromString(TEXT("Side")))
					.Description(FText::FromString(TEXT("The side asked for when you arrive; you can change it in the lobby.")))
					.Options({ TEXT("Smaller side"), TEXT("Spies"), TEXT("Mercenaries"), TEXT("Watch") })
					.Get([this] { return JoinSide; })
					.Set([this](int32 Value) { JoinSide = Value; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Danger()).AutoWrapText(true).Text(TAttribute<FText>::CreateLambda([this] { return FText::FromString(AddressError); }))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[
					SAssignNew(Connect, SScctButton)
					.Look(EScctButtonLook::Primary)
					.MinWidth(200.0f)
					.Text(FText::FromString(TEXT("CONNECT")))
					.Description(FText::FromString(TEXT("Join the server at this address.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { JoinAddress(); }))
				];

			// The servers joined before, newest first.
			const TSharedRef<SVerticalBox> Recent = SNew(SVerticalBox);
			const TArray<FString> Addresses = LoadRecent();
			for (const FString& Address : Addresses)
			{
				Recent->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(SScctButton)
					.FillWidth(true)
					.Text(FText::FromString(Address))
					.Description(FText::FromString(TEXT("Join this server again.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Address] { AddressBox->SetText(FText::FromString(Address)); JoinAddress(); }))
				];
			}
			if (Addresses.IsEmpty())
			{
				Recent->AddSlot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text(FText::FromString(TEXT("The servers you join by address are kept here.")))
				];
			}
			AddressFocus = AddressBox;

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(620.0f).VAlign(VAlign_Top)[ ScctUi::Panel(Form, FMargin(28.0f, 24.0f), 0.72f) ]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(420.0f).VAlign(VAlign_Top)[ ScctUi::Card(TEXT("Recent servers"), Recent) ]
				];
		}

		TArray<FString> LoadRecent() const
		{
			FString Joined;
			GConfig->GetString(OnlineMenuSection, TEXT("RecentAddresses"), Joined, GGameUserSettingsIni);
			TArray<FString> Addresses;
			Joined.ParseIntoArray(Addresses, TEXT(" "));
			// What an older build saved without checking (two addresses run together) is not offered.
			Addresses.RemoveAll([](const FString& Address) { return !OnlineIsServerAddress(Address); });
			return Addresses;
		}

		void JoinAddress()
		{
			UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr || !AddressBox.IsValid())
			{
				return;
			}
			const FString Address = AddressBox->GetText().ToString().TrimStartAndEnd().Replace(TEXT(" "), TEXT(""));
			if (Address.IsEmpty())
			{
				AddressError = TEXT("Type the server's address first.");
				FSlateApplication::Get().SetKeyboardFocus(AddressBox, EFocusCause::SetDirectly);
				return;
			}
			if (!OnlineIsServerAddress(Address))
			{
				AddressError = TEXT("That is not a server address: type a name or an IP address, with :port after it when the port is not 28888.");
				FSlateApplication::Get().SetKeyboardFocus(AddressBox, EFocusCause::SetDirectly);
				return;
			}
			AddressError.Reset();
			if (!IsSignedIn())
			{
				Visit->SetPlayerName(NameBox->GetText().ToString());
			}
			TArray<FString> Addresses = LoadRecent();
			Addresses.Remove(Address);
			Addresses.Insert(Address, 0);
			Addresses.SetNum(FMath::Min(Addresses.Num(), RecentCount));
			GConfig->SetString(OnlineMenuSection, TEXT("RecentAddresses"), *FString::Join(Addresses, TEXT(" ")), GGameUserSettingsIni);
			GConfig->SetString(OnlineMenuSection, TEXT("LastAddress"), *Address, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
			// The browser's saved servers too, where it pings it and offers to remove it.
			UScctOnlineSubsystem::AddSavedServer(Address);
			const int32 Sides[] = { -1, 0, 1, 255 };
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: joining %s"), *Address);
			Visit->Join(Address, PasswordBox->GetText().ToString(), Sides[FMath::Clamp(JoinSide, 0, 3)]);
		}

		static constexpr int32 BrowserTab = 2;
		TSharedPtr<SBox> BrowserBox;
		TSharedPtr<SScctServerBrowserPage> Browser;
		TSharedPtr<SScctTabBar> Tabs;
		TSharedPtr<SWidgetSwitcher> Switcher;
		TSharedPtr<SScctTable> Table;
		TSharedPtr<SBox> DetailsBox;
		TSharedPtr<SEditableTextBox> NameBox;
		TSharedPtr<SEditableTextBox> AddressBox;
		TSharedPtr<SEditableTextBox> PasswordBox;
		TWeakPtr<SWidget> ServerFocus;
		TWeakPtr<SWidget> AddressFocus;
		FString AddressError;
		int32 JoinSide = 0;
		int32 SeenRevision = -1;
		int32 Shown = 0;
		bool bHideFull = false;
		bool bHideLocked = false;
		bool bHideUnjoinable = false;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeOnline(const FScctMenuHost& Host, bool bAddress)
{
	return SNew(SScctMenuOnline, Host, bAddress);
}
