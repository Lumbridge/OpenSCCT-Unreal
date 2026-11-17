#include "Menu/SScctHubPage.h"

#include "Framework/Application/SlateApplication.h"
#include "Hub/ScctHubSubsystem.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace HubPageUi = ScctMenuStyle;

namespace
{
	const TCHAR* const HubPageSection = TEXT("OpenSCCT.Online");

	/** The hub's default port (OpenSCCT's HubK.DefaultPort), named in the page's notes. */
	constexpr int32 HubPort = 28886;

	constexpr float HubPageWidth = 860.0f;

	/** The list's columns: hub, region, hosted by, players, ping. */
	const float HubColumnWidths[] = { 0.0f, 80.0f, 110.0f, 80.0f, 60.0f };

	/** A labelled block of the page: the label, a gap, the content, a larger gap. */
	void AddHubBlock(const TSharedRef<SVerticalBox>& Box, const FString& LabelText, const TSharedRef<SWidget>& Content, float After = 16.0f)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 7.0f)[ HubPageUi::Label(LabelText) ];
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, After)[ Content ];
	}

	TSharedRef<SWidget> HubNote(const FString& Text)
	{
		return HubPageUi::Text(HubPageUi::ToText(Text), 10.5f, FSlateColor(ScctHudStyle::TextFaint()), ScctHudStyle::EWeight::Regular, true);
	}

	/** One line of the list in its columns (the header or a hub). */
	TSharedRef<SWidget> HubColumns(const TArray<FString>& Cells, const FLinearColor& Colour, bool bHeader)
	{
		const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 Index = 0; Index < Cells.Num() && Index < UE_ARRAY_COUNT(HubColumnWidths); ++Index)
		{
			const TSharedRef<SWidget> Cell = bHeader
				? HubPageUi::Label(Cells[Index])
				: HubPageUi::Text(HubPageUi::ToText(Cells[Index]), 12.0f, FSlateColor(Colour), Index == 0 ? ScctHudStyle::EWeight::Bold : ScctHudStyle::EWeight::Regular);
			if (HubColumnWidths[Index] <= 0.0f)
			{
				Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Cell ];
			}
			else
			{
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(HubColumnWidths[Index])[ Cell ] ];
			}
		}
		return Row;
	}
}

void SScctHubPage::Construct(const FArguments& InArgs)
{
	Hub = InArgs._Hub;
	Online = InArgs._Online;
	OnBack = InArgs._OnBack;

	FString Address;
	GConfig->GetString(HubPageSection, TEXT("LastHubAddress"), Address, GGameUserSettingsIni);
	FParse::Value(FCommandLine::Get(), TEXT("ScctHubAddress="), Address);
	if (Address.IsEmpty())
	{
		Address = FString::Printf(TEXT("127.0.0.1:%d"), HubPort);
	}
	FParse::Value(FCommandLine::Get(), TEXT("ScctHubPress="), AutoPress);
	SeenAddress = Address;

	NameBox = HubPageUi::TextBox(TEXT("your name"), Hub.IsValid() ? Hub->GetPlayerName() : FString());
	AddressBox = HubPageUi::TextBox(TEXT("address or address:port"), Address, false, FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
	{
		if (Commit == ETextCommit::OnEnter)
		{
			JoinAddress();
		}
	}));

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ HubPageUi::Title(HubPageUi::ToText(TEXT("HUB")), 24.0f) ];

	// The visit: who arrives, as what.
	AddHubBlock(Box, TEXT("Your visit"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 18.0f, 0.0f)[ NameBox.ToSharedRef() ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ HubPageUi::Label(TEXT("Arrive as")) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			HubPageUi::Button(HubPageUi::ToText(TEXT("SPY")), FOnClicked::CreateLambda([this] { SideIndex = 0; return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this] { return SideIndex == 0; }), ScctHudStyle::Accent(0), 100.0f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			HubPageUi::Button(HubPageUi::ToText(TEXT("MERCENARY")), FOnClicked::CreateLambda([this] { SideIndex = 1; return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this] { return SideIndex == 1; }), ScctHudStyle::Accent(1), 100.0f)
		]);

	// The master's hubs.
	const TWeakObjectPtr<UScctHubSubsystem> WeakHub = Hub;
	AddHubBlock(Box, TEXT("Hubs"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(10.0f, 0.0f, 10.0f, 6.0f)
		[
			HubColumns({ TEXT("Hub"), TEXT("Region"), TEXT("Hosted by"), TEXT("Players"), TEXT("Ping") }, ScctHudStyle::TextDim(), true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MinDesiredHeight(120.0f).MaxDesiredHeight(220.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()[ SAssignNew(HubRows, SVerticalBox) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 4.0f)
		[
			HubPageUi::Text(TAttribute<FText>::CreateLambda([WeakHub]
			{
				return WeakHub.IsValid() ? HubPageUi::ToText(WeakHub->GetListMessage()) : FText::GetEmpty();
			}), 11.0f, TAttribute<FSlateColor>::CreateLambda([WeakHub]
			{
				return FSlateColor(WeakHub.IsValid() && WeakHub->GetListState() == EScctHubListState::Failed ? ScctHudStyle::Danger() : ScctHudStyle::TextDim());
			}), ScctHudStyle::EWeight::Regular, true)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			HubNote(TEXT("Official hubs are run by the OpenSCCT servers; the others are hosted by players. Pick the one with the lowest ping."))
		]);

	// A hub by its address.
	AddHubBlock(Box, TEXT("Join a hub by address"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ AddressBox.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			HubNote(FString::Printf(TEXT("A hub on your network or at a direct IP; the port is %d unless the address names one."), HubPort))
		]);

	// A hub on this PC.
	AddHubBlock(Box, TEXT("Host a hub"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			HubNote(FString::Printf(TEXT("Opens a hub on this PC (UDP port %d) and takes you in. Others join it from the hub list or by your address; the hub closes when you leave it."), HubPort))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			HubPageUi::Button(TAttribute<FText>::CreateLambda([]
			{
				return HubPageUi::ToText(FScctSettings::GetBool(UScctHubSubsystem::HubListedSetting) ? TEXT("SHOWN IN THE HUB LIST") : TEXT("NOT IN THE HUB LIST"));
			}), FOnClicked::CreateLambda([]
			{
				FScctSettings::SetBool(UScctHubSubsystem::HubListedSetting, !FScctSettings::GetBool(UScctHubSubsystem::HubListedSetting));
				FScctSettings::Save();
				return FReply::Handled();
			}), TAttribute<bool>::CreateLambda([] { return FScctSettings::GetBool(UScctHubSubsystem::HubListedSetting); }), HubPageUi::Accent(), 220.0f)
		], 12.0f);

	// What the visit is doing, or why it ended.
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
	[
		HubPageUi::Text(TAttribute<FText>::CreateSP(this, &SScctHubPage::StatusLine), 12.0f,
			TAttribute<FSlateColor>::CreateSP(this, &SScctHubPage::StatusColour), ScctHudStyle::EWeight::Regular, true)
	];

	// The bottom bar.
	const auto Pressed = [this](void (SScctHubPage::*Action)())
	{
		return FOnClicked::CreateLambda([this, Action] { (this->*Action)(); return FReply::Handled(); });
	};
	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ HubPageUi::Button(HubPageUi::ToText(TEXT("JOIN")), Pressed(&SScctHubPage::Join), true, HubPageUi::Accent(), 130.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ HubPageUi::Button(HubPageUi::ToText(TEXT("HOST A HUB ON THIS PC")), Pressed(&SScctHubPage::Host)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ HubPageUi::Button(HubPageUi::ToText(TEXT("REFRESH")), Pressed(&SScctHubPage::Refresh)) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)[ HubPageUi::Button(HubPageUi::ToText(TEXT("BACK")), Pressed(&SScctHubPage::Back)) ]
	];

	ChildSlot
	[
		SNew(SBox).WidthOverride(HubPageWidth)[ HubPageUi::Glass(Box) ]
	];

	if (UScctHubSubsystem* Visit = Hub.Get())
	{
		ChangedHandle = Visit->OnChanged.AddLambda([this] { bListDirty = true; });
	}
	Refresh();
}

SScctHubPage::~SScctHubPage()
{
	if (UScctHubSubsystem* Visit = Hub.Get())
	{
		Visit->OnChanged.Remove(ChangedHandle);
	}
}

void SScctHubPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Typing an address means that one: the list's pick no longer counts.
	const FString Address = AddressBox.IsValid() ? AddressBox->GetText().ToString() : FString();
	if (Address != SeenAddress)
	{
		if (!PickedId.IsEmpty())
		{
			PickedId.Reset();
			bListDirty = true;
		}
		SeenAddress = Address;
	}

	if (bListDirty)
	{
		bListDirty = false;
		RefillHubs();
	}

	if (!AutoPress.IsEmpty())
	{
		AutoPressTimer -= InDeltaTime;
		if (AutoPressTimer <= 0.0f && AutoPress.Equals(TEXT("joinlisted"), ESearchCase::IgnoreCase))
		{
			// The first hub of the list that can be joined, once the list is in.
			const UScctHubSubsystem* Visit = Hub.Get();
			const FScctHubListEntry* First = Visit != nullptr ? Visit->GetHubList().FindByPredicate([](const FScctHubListEntry& Entry) { return Entry.Problem[0] == 0; }) : nullptr;
			if (First != nullptr)
			{
				AutoPress.Reset();
				PickedId = First->Id;
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctHubPress: picking %s in the hub list and pressing JOIN"), First->Name);
				Join();
			}
			else if (AutoPressTimer < -20.0f)
			{
				AutoPress.Reset();
				UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: -ScctHubPress=joinlisted: no hub in the list can be joined"));
			}
		}
		else if (AutoPressTimer <= 0.0f)
		{
			const FString Press = MoveTemp(AutoPress);
			AutoPress.Reset();
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctHubPress: pressing %s on the hub page"), *Press.ToUpper());
			if (Press.Equals(TEXT("host"), ESearchCase::IgnoreCase))
			{
				Host();
			}
			else if (Press.Equals(TEXT("join"), ESearchCase::IgnoreCase))
			{
				Join();
			}
			else if (Press.Equals(TEXT("refresh"), ESearchCase::IgnoreCase))
			{
				Refresh();
			}
		}
	}
}

void SScctHubPage::RefillHubs()
{
	const UScctHubSubsystem* Visit = Hub.Get();
	if (Visit == nullptr || !HubRows.IsValid())
	{
		return;
	}

	HubRows->ClearChildren();
	const TArray<FScctHubListEntry>& Hubs = Visit->GetHubList();
	if (!PickedId.IsEmpty() && !Hubs.ContainsByPredicate([this](const FScctHubListEntry& Entry) { return PickedId == Entry.Id; }))
	{
		PickedId.Reset();
	}

	for (const FScctHubListEntry& Entry : Hubs)
	{
		const FString Id = Entry.Id;
		const bool bOfficial = (Entry.Flags & EScctHubListFlags::Official) != 0;
		const FString Problem = Entry.Problem;
		const FString Name = Problem.IsEmpty() ? FString(Entry.Name) : FString::Printf(TEXT("%s (%s)"), Entry.Name, *Problem);
		const FString Region = Entry.Region[0] != 0 ? FString(Entry.Region) : FString(TEXT("-"));
		const FString HostedBy = bOfficial ? TEXT("Official") : (Entry.Flags & EScctHubListFlags::Dedicated) != 0 ? TEXT("Dedicated") : TEXT("Player");
		const FString Players = FString::Printf(TEXT("%d/%d"), Entry.Players, Entry.MaxPlayers);
		const FString Ping = Entry.PingMs >= 0 ? FString::FromInt(Entry.PingMs) : FString(TEXT("-"));
		const FLinearColor Colour = !Problem.IsEmpty() ? ScctHudStyle::TextFaint() : bOfficial ? HubPageUi::Accent() : ScctHudStyle::Text();

		HubRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SOverlay)
			// The whole row is the button that picks it; a second click joins it.
			+ SOverlay::Slot()
			[
				HubPageUi::Button(FText::GetEmpty(), FOnClicked::CreateLambda([this, Id]
				{
					if (PickedId == Id)
					{
						Join();
					}
					else
					{
						PickedId = Id;
						bListDirty = true;
					}
					return FReply::Handled();
				}), TAttribute<bool>::CreateLambda([this, Id] { return PickedId == Id; }), HubPageUi::Accent())
			]
			+ SOverlay::Slot().Padding(10.0f, 7.0f)
			[
				SNew(SBox).Visibility(EVisibility::HitTestInvisible)
				[
					HubColumns({ Name, Region, HostedBy, Players, Ping }, Colour, false)
				]
			]
		];
	}
}

void SScctHubPage::SaveName()
{
	if (UScctOnlineSubsystem* Visits = Online.Get(); Visits != nullptr && NameBox.IsValid())
	{
		Visits->SetPlayerName(NameBox->GetText().ToString());
	}
}

void SScctHubPage::Join()
{
	UScctHubSubsystem* Visit = Hub.Get();
	if (Visit == nullptr)
	{
		return;
	}

	const int32 Index = PickedId.IsEmpty() ? INDEX_NONE
		: Visit->GetHubList().IndexOfByPredicate([this](const FScctHubListEntry& Entry) { return PickedId == Entry.Id; });
	if (Index == INDEX_NONE)
	{
		JoinAddress();
		return;
	}

	Message.Reset();
	SaveName();
	Visit->JoinListed(Index, SideIndex);
}

void SScctHubPage::JoinAddress()
{
	UScctHubSubsystem* Visit = Hub.Get();
	if (Visit == nullptr || !AddressBox.IsValid())
	{
		return;
	}

	const FString Address = AddressBox->GetText().ToString().TrimStartAndEnd();
	if (Address.IsEmpty())
	{
		Message = TEXT("Enter the hub's address.");
		FSlateApplication::Get().SetKeyboardFocus(AddressBox);
		return;
	}

	Message.Reset();
	SaveName();
	GConfig->SetString(HubPageSection, TEXT("LastHubAddress"), *Address, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	Visit->Join(Address, SideIndex);
}

void SScctHubPage::Host()
{
	if (UScctHubSubsystem* Visit = Hub.Get())
	{
		Message.Reset();
		SaveName();
		Visit->HostAndJoin(0, SideIndex, false);
	}
}

void SScctHubPage::Refresh()
{
	PickedId.Reset();
	if (UScctHubSubsystem* Visit = Hub.Get())
	{
		Visit->RequestHubList();
	}
	bListDirty = true;
}

void SScctHubPage::Back()
{
	// A visit still connecting (or a hub this PC opened) stops with the page.
	if (UScctHubSubsystem* Visit = Hub.Get())
	{
		Visit->Leave();
	}
	OnBack.ExecuteIfBound();
}

FText SScctHubPage::StatusLine() const
{
	const UScctHubSubsystem* Visit = Hub.Get();
	if (Visit == nullptr)
	{
		return FText::GetEmpty();
	}
	if (!Visit->GetNotice().IsEmpty())
	{
		return HubPageUi::ToText(Visit->GetNotice());
	}
	if (Visit->GetState() != EScctHubState::Idle)
	{
		return HubPageUi::ToText(Visit->GetStatusText());
	}
	return HubPageUi::ToText(Message);
}

FSlateColor SScctHubPage::StatusColour() const
{
	const UScctHubSubsystem* Visit = Hub.Get();
	const bool bFailed = Visit != nullptr && (!Visit->GetNotice().IsEmpty() || Visit->GetState() == EScctHubState::Failed);
	return FSlateColor(bFailed ? ScctHudStyle::Danger() : HubPageUi::Accent());
}
