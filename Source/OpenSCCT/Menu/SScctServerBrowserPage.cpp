#include "Menu/SScctServerBrowserPage.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace Ui = ScctMenuStyle;

namespace
{
	// The front end's own section of the user's settings (SScctFrontEnd's Join page keeps LastAddress there).
	const TCHAR* const BrowserUserSection = TEXT("OpenSCCT.Online");

	constexpr float BrowserPageWidth = 1080.0f;
	constexpr float BrowserTableHeight = 250.0f;

	struct FBrowserColumn
	{
		const TCHAR* Title;
		const TCHAR* Switch;
		float Width;
	};

	// Godot's columns (ServerBrowserScreen: Server, Map, Mode, Players, Ping, State, Reached by, Joinable), with the
	// master's region before Reached by.
	const FBrowserColumn BrowserColumns[] = {
		{ TEXT("Server"), TEXT("server"), 236.0f },
		{ TEXT("Map"), TEXT("map"), 104.0f },
		{ TEXT("Mode"), TEXT("mode"), 104.0f },
		{ TEXT("Players"), TEXT("players"), 84.0f },
		{ TEXT("Ping"), TEXT("ping"), 66.0f },
		{ TEXT("State"), TEXT("state"), 70.0f },
		{ TEXT("Region"), TEXT("region"), 76.0f },
		{ TEXT("Reached by"), TEXT("reach"), 116.0f },
		{ TEXT("Joinable"), TEXT("joinable"), 156.0f },
	};

	const TCHAR* const BrowserSideLabels[] = { TEXT("AUTO"), TEXT("SPY"), TEXT("MERCENARY"), TEXT("WATCH") };
	const int32 BrowserSideTeams[] = { -1, 0, 1, 255 };
	const TCHAR* const BrowserPathLabels[] = { TEXT("AUTO"), TEXT("DIRECT"), TEXT("NAT PUNCH"), TEXT("RELAY") };

	/** A server's mode as the lobby names it (EGameMode). */
	const TCHAR* BrowserModeName(int32 Mode)
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

	/** The session's phase as the State column shows it. */
	const TCHAR* BrowserPhaseName(int32 Phase)
	{
		switch (Phase)
		{
		case 0: return TEXT("Lobby");
		case 1: return TEXT("Loading");
		case 2: return TEXT("In match");
		default: return TEXT("Results");
		}
	}

	bool RowHas(const FScctBrowserRow& Row, int32 Flag)
	{
		return (Row.Flags & Flag) != 0;
	}

	/** The rows' look: white boxes whose colour the row sets (selected, hovered). */
	const FButtonStyle& BrowserRowStyle()
	{
		static const FButtonStyle Style = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f))
			.SetNormalPadding(FMargin(8.0f, 6.0f))
			.SetPressedPadding(FMargin(8.0f, 6.0f));
		return Style;
	}
}

void SScctServerBrowserPage::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	OnBack = InArgs._OnBack;
	ReadSwitches();

	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	FString MasterUrl, InviteKey, LastAddress;
	UScctOnlineSubsystem::ReadMasterSettings(MasterUrl, InviteKey);
	GConfig->GetString(BrowserUserSection, TEXT("LastAddress"), LastAddress, GGameUserSettingsIni);

	MasterBox = Ui::TextBox(TEXT("master server (https://...)"), MasterUrl);
	InviteBox = Ui::TextBox(TEXT("invite key"), InviteKey, true);
	InviteBox->SetMinimumDesiredWidth(180.0f);
	AddressBox = Ui::TextBox(TEXT("host or host:port"), LastAddress, false, FOnTextCommitted::CreateLambda([this](const FText& Text, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			JoinAddress(Text.ToString(), true);
		}
	}));
	PasswordBox = Ui::TextBox(TEXT("password"), FString(), true, FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			JoinSelected();
		}
	}));

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Ui::Title(Ui::ToText(TEXT("SERVER LIST")), 24.0f) ];

	// The master and its invite key, as the Godot browser's top row.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ MasterBox.ToSharedRef() ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ SNew(SBox).WidthOverride(200.0f)[ InviteBox.ToSharedRef() ] ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("APPLY")), FOnClicked::CreateLambda([this] { ApplyMaster(); return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Ui::Button(Ui::ToText(TEXT("REFRESH")), FOnClicked::CreateLambda([this] { Refresh(); return FReply::Handled(); }), false, Ui::Accent())
		]
	];

	// What the browser is doing: the master's state, what the LAN and the saved servers gave.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([Weak]
		{
			return Weak.IsValid() ? Ui::ToText(Weak->GetBrowserText(EScctBrowserText::Status)) : FText::GetEmpty();
		}), 11.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];

	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ BuildHeader() ];
	Box->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(BrowserTableHeight)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()[ SAssignNew(RowBox, SVerticalBox) ]
		]
	];

	// The selected server's players by side.
	Box->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([this]
		{
			int32 Index = -1;
			const FScctBrowserRow* Row = FindSelected(&Index);
			if (Row == nullptr || !Online.IsValid())
			{
				return Ui::ToText(TEXT("Select a server to see its players."));
			}
			const FString Players = Online->GetBrowserPlayers(Index);
			return Ui::ToText(Players.IsEmpty() ? FString::Printf(TEXT("%s: nobody is playing."), Row->Name) : Players);
		}), 11.5f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f)[ Ui::Rule() ];
	Box->AddSlot().AutoHeight()[ BuildJoinBar() ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f)[ Ui::Rule() ];
	Box->AddSlot().AutoHeight()[ BuildAddressBar() ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ BuildAccount() ];

	ChildSlot
	[
		SNew(SBox).WidthOverride(BrowserPageWidth)[ Ui::Glass(Box, FMargin(24.0f)) ]
	];

	RefillSaved();
	Refresh();
}

SScctServerBrowserPage::~SScctServerBrowserPage()
{
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		Visit->CloseBrowser();
	}
}

void SScctServerBrowserPage::ReadSwitches()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("ScctBrowserJoin="), AutoJoinIndex);
	FParse::Value(CommandLine, TEXT("ScctBrowserPassword="), AutoPassword);
	FString Side;
	if (FParse::Value(CommandLine, TEXT("ScctSide="), Side))
	{
		SideIndex = Side.Equals(TEXT("spy"), ESearchCase::IgnoreCase) ? 1
			: Side.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 2
			: Side.Equals(TEXT("watch"), ESearchCase::IgnoreCase) ? 3 : 0;
	}
	FString Address;
	if (FParse::Value(CommandLine, TEXT("ScctSaveServer="), Address))
	{
		UScctOnlineSubsystem::AddSavedServer(Address);
	}
	if (FParse::Value(CommandLine, TEXT("ScctForgetServer="), Address))
	{
		UScctOnlineSubsystem::RemoveSavedServer(Address);
	}
	FString SortSwitch;
	// Not stopped at the comma: "ping,desc" is one value.
	if (FParse::Value(CommandLine, TEXT("ScctBrowserSort="), SortSwitch, false))
	{
		FString Name, Direction;
		if (!SortSwitch.Split(TEXT(","), &Name, &Direction))
		{
			Name = SortSwitch;
		}
		for (int32 Column = 0; Column < static_cast<int32>(EColumn::Count); ++Column)
		{
			if (Name.Equals(BrowserColumns[Column].Switch, ESearchCase::IgnoreCase))
			{
				SortColumn = static_cast<EColumn>(Column);
			}
		}
		bDescending = Direction.Equals(TEXT("desc"), ESearchCase::IgnoreCase);
	}
}

void SScctServerBrowserPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return;
	}

	const int32 Revision = Visit->GetBrowserRevision();
	if (Revision != SeenRevision)
	{
		SeenRevision = Revision;
		Refill();
	}
	RunUnattended(InDeltaTime);
}

// ---- the table ------------------------------------------------------------------------------------------------------

void SScctServerBrowserPage::Refresh()
{
	Message.Reset();
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		Visit->OpenBrowser();
	}
	SeenRevision = -2;
}

void SScctServerBrowserPage::Refill()
{
	if (!Online.IsValid() || !RowBox.IsValid())
	{
		return;
	}

	Online->GetBrowserRows(Rows);
	Sort();
	RowBox->ClearChildren();
	if (Order.IsEmpty())
	{
		RowBox->AddSlot().AutoHeight().Padding(8.0f, 10.0f)
		[
			Ui::Text(Ui::ToText(TEXT("No server found yet.")), 12.0f, FSlateColor(ScctHudStyle::TextFaint()))
		];
	}
	for (const int32 Index : Order)
	{
		RowBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ BuildRow(Index) ];
	}
	LogRows();

	// The password prompt takes the keyboard when a server with a password is selected (once per selection).
	const FScctBrowserRow* Selected = FindSelected();
	if (Selected != nullptr && RowHas(*Selected, EScctOnlineServerFlags::Password) && PromptedKey != SelectedKey && PasswordBox.IsValid())
	{
		PromptedKey = SelectedKey;
		FSlateApplication::Get().SetKeyboardFocus(PasswordBox);
	}
}

void SScctServerBrowserPage::Sort()
{
	Order.Reset(Rows.Num());
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Order.Add(Index);
	}

	const EColumn Column = SortColumn;
	const bool bDown = bDescending;
	const TArray<FScctBrowserRow>& All = Rows;
	Order.StableSort([&All, Column, bDown](int32 A, int32 B)
	{
		const FScctBrowserRow& L = All[A];
		const FScctBrowserRow& R = All[B];
		int32 Compare = 0;
		switch (Column)
		{
		case EColumn::Players: Compare = L.Players - R.Players; break;
		case EColumn::Ping:
			// A server that has not answered goes last, whichever way round.
			if ((L.PingMs < 0) != (R.PingMs < 0))
			{
				return R.PingMs < 0;
			}
			Compare = L.PingMs - R.PingMs;
			break;
		case EColumn::State: Compare = L.Phase - R.Phase; break;
		case EColumn::Mode: Compare = FCString::Stricmp(BrowserModeName(L.Mode), BrowserModeName(R.Mode)); break;
		case EColumn::Map: Compare = FCString::Stricmp(L.Map, R.Map); break;
		case EColumn::Region: Compare = FCString::Stricmp(L.Region, R.Region); break;
		case EColumn::Reach: Compare = L.Source != R.Source ? L.Source - R.Source : L.Reach - R.Reach; break;
		case EColumn::Joinable:
			// Joinable first, then by the reason.
			Compare = (L.Problem[0] != 0) != (R.Problem[0] != 0) ? (L.Problem[0] != 0 ? 1 : -1) : FCString::Stricmp(L.Problem, R.Problem);
			break;
		default: break;
		}
		if (Compare == 0)
		{
			Compare = FCString::Stricmp(L.Name, R.Name);
		}
		return bDown ? Compare > 0 : Compare < 0;
	});
}

FReply SScctServerBrowserPage::HandleHeader(EColumn Column)
{
	// A second click on the sorted column turns the order round; another column sorts up (ping: fastest first).
	bDescending = SortColumn == Column ? !bDescending : false;
	SortColumn = Column;
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: server list sorted by %s, %s"), BrowserColumns[static_cast<int32>(Column)].Title, bDescending ? TEXT("descending") : TEXT("ascending"));
	Refill();
	return FReply::Handled();
}

void SScctServerBrowserPage::Select(const FString& Key)
{
	SelectedKey = Key;
	Message.Reset();
	if (PasswordBox.IsValid() && PromptedKey != Key)
	{
		PasswordBox->SetText(FText::GetEmpty());
	}
	Refill();
}

const FScctBrowserRow* SScctServerBrowserPage::FindSelected(int32* OutIndex) const
{
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (!SelectedKey.IsEmpty() && SelectedKey == Rows[Index].Key)
		{
			if (OutIndex != nullptr)
			{
				*OutIndex = Index;
			}
			return &Rows[Index];
		}
	}
	return nullptr;
}

TSharedRef<SWidget> SScctServerBrowserPage::BuildHeader()
{
	const TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
	for (int32 Column = 0; Column < static_cast<int32>(EColumn::Count); ++Column)
	{
		const EColumn Which = static_cast<EColumn>(Column);
		Header->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(BrowserColumns[Column].Width + (Column == 0 ? 8.0f : 0.0f))
			[
				SNew(SButton)
				.ButtonStyle(&BrowserRowStyle())
				.ButtonColorAndOpacity(FLinearColor::Transparent)
				.IsFocusable(false)
				.OnClicked(FOnClicked::CreateSP(this, &SScctServerBrowserPage::HandleHeader, Which))
				[
					SNew(STextBlock)
					.Text(TAttribute<FText>::CreateLambda([this, Which, Column]
					{
						const FString Title = FString(BrowserColumns[Column].Title).ToUpper();
						return Ui::ToText(SortColumn == Which ? Title + (bDescending ? TEXT(" ▼") : TEXT(" ▲")) : Title);
					}))
					.Font(ScctHudStyle::Label(9.0f))
					.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Which]
					{
						return FSlateColor(SortColumn == Which ? Ui::Accent() : ScctHudStyle::TextDim());
					}))
				]
			]
		];
	}
	return Header;
}

FString SScctServerBrowserPage::CellText(const FScctBrowserRow& Row, EColumn Column) const
{
	switch (Column)
	{
	case EColumn::Server: return Row.Name;
	case EColumn::Map: return Row.Map;
	case EColumn::Mode: return FString(BrowserModeName(Row.Mode)) + (RowHas(Row, EScctOnlineServerFlags::Practice) ? TEXT(" (practice)") : TEXT(""));
	case EColumn::Players: return FString::Printf(TEXT("%d/%d"), Row.Players, Row.MaxPlayers);
	case EColumn::Ping: return Row.PingMs >= 0 ? FString::FromInt(Row.PingMs) : TEXT("-");
	case EColumn::State: return BrowserPhaseName(Row.Phase);
	case EColumn::Region: return Row.Region[0] != 0 ? FString(Row.Region) : TEXT("-");
	case EColumn::Reach:
		switch (static_cast<EScctBrowserSource>(Row.Source))
		{
		case EScctBrowserSource::Lan: return RowHas(Row, EScctBrowserRowFlags::Saved) ? TEXT("LAN, saved") : TEXT("LAN");
		case EScctBrowserSource::Saved: return TEXT("Saved direct");
		default:
			switch (static_cast<EScctBrowserReach>(Row.Reach))
			{
			case EScctBrowserReach::Direct: return TEXT("Internet direct");
			case EScctBrowserReach::Punch: return TEXT("NAT punch");
			case EScctBrowserReach::Relay: return TEXT("Relay");
			default: return TEXT("Internet (checking)");
			}
		}
	case EColumn::Joinable: return Row.Problem[0] != 0 ? FString(Row.Problem) : TEXT("Joinable");
	default: return FString();
	}
}

TSharedRef<SWidget> SScctServerBrowserPage::BuildRow(int32 Index)
{
	const FScctBrowserRow& Row = Rows[Index];
	const FString Key = Row.Key;
	// A full server can still be tried (a place may have come free); the other reasons grey it out.
	const bool bBlocked = Row.Problem[0] != 0 && !RowHas(Row, EScctOnlineServerFlags::Full);
	const FLinearColor Colour = bBlocked ? ScctHudStyle::TextFaint() : ScctHudStyle::Text();

	const TSharedRef<SHorizontalBox> Cells = SNew(SHorizontalBox);
	for (int32 Column = 0; Column < static_cast<int32>(EColumn::Count); ++Column)
	{
		const EColumn Which = static_cast<EColumn>(Column);
		TSharedRef<SWidget> Cell = Ui::Text(Ui::ToText(CellText(Row, Which)), 11.5f, FSlateColor(Colour));
		if (Which == EColumn::Server)
		{
			// The name and its marks: PASSWORD in the mercenaries' amber, the others dim.
			const TSharedRef<SHorizontalBox> Name = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Cell ];
			const auto Mark = [&Name](const TCHAR* Text, const FLinearColor& MarkColour)
			{
				Name->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(7.0f, 0.0f, 0.0f, 0.0f)[ Ui::Label(Text, MarkColour) ];
			};
			if (RowHas(Row, EScctOnlineServerFlags::Password)) { Mark(TEXT("PASSWORD"), ScctHudStyle::Accent(1)); }
			if (RowHas(Row, EScctOnlineServerFlags::Dedicated)) { Mark(TEXT("DEDICATED"), ScctHudStyle::TextDim()); }
			if (RowHas(Row, EScctOnlineServerFlags::RequireAccounts)) { Mark(TEXT("ACCOUNT"), ScctHudStyle::TextDim()); }
			if (RowHas(Row, EScctOnlineServerFlags::Practice)) { Mark(TEXT("PRACTICE"), ScctHudStyle::TextDim()); }
			Cell = Name;
		}
		else if (Which == EColumn::Joinable && Row.Problem[0] == 0)
		{
			Cell = Ui::Text(Ui::ToText(CellText(Row, Which)), 11.5f, FSlateColor(ScctHudStyle::Good()));
		}
		Cells->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(BrowserColumns[Column].Width).Clipping(EWidgetClipping::ClipToBounds)[ Cell ]
		];
	}

	const TSharedRef<SButton> Button = SNew(SButton)
		.ButtonStyle(&BrowserRowStyle())
		.IsFocusable(false)
		.ToolTipText(Ui::ToText(FString::Printf(TEXT("%s  %s"), Row.Address, Row.Key)))
		.OnClicked(FOnClicked::CreateLambda([this, Key]
		{
			Select(Key);
			return FReply::Handled();
		}))
		[
			Cells
		];
	const TWeakPtr<SButton> WeakButton = Button;
	Button->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this, WeakButton, Key]
	{
		const TSharedPtr<SButton> Pinned = WeakButton.Pin();
		const bool bHovered = Pinned.IsValid() && Pinned->IsHovered();
		if (Key == SelectedKey)
		{
			return FSlateColor(ScctHudStyle::Fade(Ui::Accent(), bHovered ? 0.42f : 0.32f));
		}
		return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, bHovered ? 0.12f : 0.04f));
	}));
	return Button;
}

// ---- joining --------------------------------------------------------------------------------------------------------

int32 SScctServerBrowserPage::PreferredTeam() const
{
	return BrowserSideTeams[FMath::Clamp(SideIndex, 0, 3)];
}

TSharedRef<SWidget> SScctServerBrowserPage::BuildJoinBar()
{
	const TSharedRef<SHorizontalBox> Sides = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BrowserSideLabels); ++Index)
	{
		const FLinearColor Colour = Index == 1 ? ScctHudStyle::Accent(0) : Index == 2 ? ScctHudStyle::Accent(1) : Ui::Accent();
		Sides->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(BrowserSideLabels[Index]), FOnClicked::CreateLambda([this, Index] { SideIndex = Index; return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this, Index] { return SideIndex == Index; }), Colour)
		];
	}

	const TSharedRef<SHorizontalBox> Paths = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BrowserPathLabels); ++Index)
	{
		Paths->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(BrowserPathLabels[Index]), FOnClicked::CreateLambda([Index]
			{
				UScctOnlineSubsystem::SetConnectMode(Index);
				return FReply::Handled();
			}), TAttribute<bool>::CreateLambda([Index] { return UScctOnlineSubsystem::GetConnectMode() == Index; }), Ui::Accent())
		];
	}

	const auto IsPrompt = [this]
	{
		const FScctBrowserRow* Row = FindSelected();
		return Row != nullptr && RowHas(*Row, EScctOnlineServerFlags::Password);
	};

	return SNew(SVerticalBox)
		// The password prompt: only while a server with a password is selected.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			SNew(SHorizontalBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([IsPrompt] { return IsPrompt() ? EVisibility::Visible : EVisibility::Collapsed; }))
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([this]
				{
					const FScctBrowserRow* Row = FindSelected();
					return Ui::ToText(Row != nullptr ? FString::Printf(TEXT("%s has a password:"), Row->Name) : FString());
				}), 12.0f, FSlateColor(ScctHudStyle::Accent(1)))
			]
			+ SHorizontalBox::Slot().AutoWidth()[ PasswordBox.ToSharedRef() ]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(76.0f)[ Ui::Label(TEXT("Side")) ] ]
					+ SHorizontalBox::Slot().AutoWidth()[ Sides ]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(76.0f)[ Ui::Label(TEXT("Connect")) ] ]
					+ SHorizontalBox::Slot().AutoWidth()[ Paths ]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Ui::Button(Ui::ToText(TEXT("JOIN")), FOnClicked::CreateLambda([this] { JoinSelected(); return FReply::Handled(); }),
					false, Ui::Accent(), 160.0f)
			]
		]
		// Why the last join was refused.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(Message); }), 11.5f,
				TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(bMessageIsError ? ScctHudStyle::Danger() : ScctHudStyle::TextDim()); }),
				ScctHudStyle::EWeight::Regular, true)
		];
}

void SScctServerBrowserPage::JoinSelected()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	int32 Index = -1;
	const FScctBrowserRow* Row = FindSelected(&Index);
	if (Visit == nullptr || Row == nullptr)
	{
		Message = TEXT("Select a server, or type an address below.");
		bMessageIsError = true;
		return;
	}

	const FString Password = PasswordBox.IsValid() ? PasswordBox->GetText().ToString() : FString();
	if (RowHas(*Row, EScctOnlineServerFlags::Password) && Password.IsEmpty())
	{
		Message = FString::Printf(TEXT("%s has a password: type it first."), Row->Name);
		bMessageIsError = true;
		FSlateApplication::Get().SetKeyboardFocus(PasswordBox);
		return;
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: joining '%s' from the server list (%s, side %s, connect %s)"), Row->Name, *CellText(*Row, EColumn::Reach),
		BrowserSideLabels[SideIndex], BrowserPathLabels[UScctOnlineSubsystem::GetConnectMode()]);
	FString Refusal;
	if (!Visit->JoinBrowserRow(Index, Password, PreferredTeam(), Refusal))
	{
		Message = Refusal;
		bMessageIsError = true;
	}
}

TSharedRef<SWidget> SScctServerBrowserPage::BuildAddressBar()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Ui::Label(TEXT("Address")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ AddressBox.ToSharedRef() ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				Ui::Button(Ui::ToText(TEXT("JOIN ADDRESS")), FOnClicked::CreateLambda([this]
				{
					JoinAddress(AddressBox->GetText().ToString(), true);
					return FReply::Handled();
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("SAVE ADDRESS")), FOnClicked::CreateLambda([this] { SaveAddress(); return FReply::Handled(); }))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 6.0f, 10.0f, 0.0f)[ Ui::Label(TEXT("Saved")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(SavedBox, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.0f, 6.0f))
			]
		];
}

void SScctServerBrowserPage::RefillSaved()
{
	if (!SavedBox.IsValid())
	{
		return;
	}

	SavedBox->ClearChildren();
	const TArray<FString> Saved = UScctOnlineSubsystem::GetSavedServers();
	if (Saved.IsEmpty())
	{
		SavedBox->AddSlot()[ Ui::Text(Ui::ToText(TEXT("No saved servers. JOIN ADDRESS and SAVE ADDRESS add one.")), 11.0f, FSlateColor(ScctHudStyle::TextFaint())) ];
		return;
	}
	for (const FString& Address : Saved)
	{
		SavedBox->AddSlot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ Ui::Text(Ui::ToText(Address), 11.5f) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
			[
				Ui::Button(Ui::ToText(TEXT("JOIN")), FOnClicked::CreateLambda([this, Address] { JoinAddress(Address, false); return FReply::Handled(); }))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("REMOVE")), FOnClicked::CreateLambda([this, Address]
				{
					UScctOnlineSubsystem::RemoveSavedServer(Address);
					RefillSaved();
					Refresh();
					return FReply::Handled();
				}), false, ScctHudStyle::Danger())
			]
		];
	}
}

void SScctServerBrowserPage::JoinAddress(const FString& Address, bool bSave)
{
	UScctOnlineSubsystem* Visit = Online.Get();
	const FString Trimmed = Address.TrimStartAndEnd();
	if (Visit == nullptr || Trimmed.IsEmpty())
	{
		Message = TEXT("Type the host's address first.");
		bMessageIsError = true;
		FSlateApplication::Get().SetKeyboardFocus(AddressBox);
		return;
	}

	GConfig->SetString(BrowserUserSection, TEXT("LastAddress"), *Trimmed, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	if (bSave)
	{
		UScctOnlineSubsystem::AddSavedServer(Trimmed);
	}
	Visit->Join(Trimmed, PasswordBox.IsValid() ? PasswordBox->GetText().ToString() : FString(), PreferredTeam());
}

void SScctServerBrowserPage::SaveAddress()
{
	const FString Address = AddressBox.IsValid() ? AddressBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (Address.IsEmpty())
	{
		Message = TEXT("Type an address to save.");
		bMessageIsError = true;
		return;
	}
	if (UScctOnlineSubsystem::AddSavedServer(Address))
	{
		RefillSaved();
		Refresh();
	}
	Message = FString::Printf(TEXT("%s is in the saved servers."), *Address);
	bMessageIsError = false;
}

void SScctServerBrowserPage::ApplyMaster()
{
	if (!MasterBox.IsValid() || !InviteBox.IsValid())
	{
		return;
	}
	// The registry's change notice gives the bridge the new master at once; the search then starts over on it.
	UScctOnlineSubsystem::SetMasterSettings(MasterBox->GetText().ToString(), InviteBox->GetText().ToString());
	Refresh();
}

TSharedRef<SWidget> SScctServerBrowserPage::BuildAccount()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	return SNew(SHorizontalBox)
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
		+ SHorizontalBox::Slot().AutoWidth().Padding(12.0f, 0.0f, 6.0f, 0.0f)
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
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateLambda([this]
			{
				OnBack.ExecuteIfBound();
				return FReply::Handled();
			}), false, Ui::Accent(), 100.0f)
		];
}

// ---- unattended -----------------------------------------------------------------------------------------------------

void SScctServerBrowserPage::LogRows()
{
	FString Shape, Pings;
	for (int32 Shown = 0; Shown < Order.Num(); ++Shown)
	{
		const FScctBrowserRow& Row = Rows[Order[Shown]];
		Shape += FString::Printf(TEXT("\n  row %d: '%s' | %s | %s | %s | %s | %s | %s | %s%s%s"), Shown, Row.Name, Row.Map, *CellText(Row, EColumn::Mode),
			*CellText(Row, EColumn::Players), *CellText(Row, EColumn::State), *CellText(Row, EColumn::Region), *CellText(Row, EColumn::Reach),
			*CellText(Row, EColumn::Joinable), RowHas(Row, EScctOnlineServerFlags::Password) ? TEXT(" | PASSWORD") : TEXT(""),
			RowHas(Row, EScctBrowserRowFlags::Answered) ? TEXT("") : TEXT(" | no answer"));
		Pings += FString::Printf(TEXT(" %s"), *CellText(Row, EColumn::Ping));
	}

	// The rows when they change, and their pings now and then (they move with every answer).
	const double Now = FPlatformTime::Seconds();
	if (Shape != LoggedRows || Now - LoggedPingsAt > 10.0)
	{
		const bool bShape = Shape != LoggedRows;
		LoggedRows = Shape;
		LoggedPingsAt = Now;
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: server list, %d row(s) sorted by %s, pings (ms):%s%s"), Order.Num(), BrowserColumns[static_cast<int32>(SortColumn)].Title,
			Pings.IsEmpty() ? TEXT(" none") : *Pings, bShape ? *Shape : TEXT(""));
	}
}

void SScctServerBrowserPage::RunUnattended(float DeltaSeconds)
{
	if (AutoJoinIndex < 0 || AutoStep >= 3)
	{
		return;
	}

	AutoClock += DeltaSeconds;
	AutoStepClock += DeltaSeconds;
	switch (AutoStep)
	{
	case 0:
	{
		// The row, once it is there and has answered (a listed server behind a NAT never does: 8 s at most).
		if (!Order.IsValidIndex(AutoJoinIndex))
		{
			return;
		}
		const FScctBrowserRow& Row = Rows[Order[AutoJoinIndex]];
		if (!RowHas(Row, EScctBrowserRowFlags::Answered) && AutoClock < 8.0f)
		{
			return;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctBrowserJoin: selecting row %d '%s' (ping %s, %s)%s"), AutoJoinIndex, Row.Name, *CellText(Row, EColumn::Ping),
			*CellText(Row, EColumn::Reach), RowHas(Row, EScctOnlineServerFlags::Password) ? TEXT(": it has a password, the prompt opens") : TEXT(""));
		Select(Row.Key);
		break;
	}
	case 1:
		if (AutoStepClock < 2.0f)
		{
			return;
		}
		if (PasswordBox.IsValid() && !AutoPassword.IsEmpty())
		{
			PasswordBox->SetText(Ui::ToText(AutoPassword));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctBrowserJoin: password typed (%d characters), side %s"), AutoPassword.Len(), BrowserSideLabels[SideIndex]);
		break;
	default:
		if (AutoStepClock < 2.0f)
		{
			return;
		}
		JoinSelected();
		if (!Message.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctBrowserJoin: %s"), *Message);
		}
		break;
	}
	++AutoStep;
	AutoStepClock = 0.0f;
}
