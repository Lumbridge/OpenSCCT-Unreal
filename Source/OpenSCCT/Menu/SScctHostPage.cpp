#include "Menu/SScctHostPage.h"

#include "Menu/SScctMapPicker.h"
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
	// The player's own settings (Saved/Config): the last session, as the Godot client keeps it in online.cfg.
	const TCHAR* const UserSection = TEXT("OpenSCCT.Online");
	const TCHAR* const KeyLastSetup = TEXT("HostSetup");
	const TCHAR* const KeyLastName = TEXT("HostName");
	const TCHAR* const KeyLastPlayersChosen = TEXT("HostPlayersChosen");

	constexpr float PageWidth = 1260.0f;
	constexpr float SettingsHeight = 560.0f;

	/** Sets Key in an options line, replacing its value or adding it at the end. */
	FString WithOption(const FString& Line, const FString& Key, const FString& Value)
	{
		TArray<FString> Pairs;
		Line.ParseIntoArray(Pairs, TEXT("?"), true);
		bool bFound = false;
		for (FString& Pair : Pairs)
		{
			FString Name, Old;
			if (Pair.Split(TEXT("="), &Name, &Old) && Name.Equals(Key, ESearchCase::IgnoreCase))
			{
				Pair = Name + TEXT("=") + Value;
				bFound = true;
			}
		}
		if (!bFound)
		{
			Pairs.Add(Key + TEXT("=") + Value);
		}
		return TEXT("?") + FString::Join(Pairs, TEXT("?"));
	}

	bool IsYes(const FString& Value)
	{
		return Value == TEXT("1") || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
	}

	TSharedRef<SWidget> Heading(const FString& Text)
	{
		return SNew(SBox).Padding(0.0f, 10.0f, 0.0f, 8.0f)[ Ui::Label(Text, Ui::Accent()) ];
	}

	TSharedRef<SWidget> Note(const TAttribute<FText>& Text)
	{
		return SNew(SBox).Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Ui::Text(Text, 10.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true) ];
	}
}

void SScctHostPage::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	OnBack = InArgs._OnBack;
	Maps = ScctHostSetup::Maps();

	// The hosting choices start from the Online settings page.
	const FScctHostRequest Defaults = FScctHostRequest::Default();
	bListed = Defaults.bListed;
	bUpnp = Defaults.bUpnp;
	bRequireAccounts = Defaults.bRequireAccounts;
	bRecordDemos = Defaults.bRecordDemos;
	bLan = Defaults.bLan;
	LanPort = Defaults.LanPort;

	// The last session when its map can still be shown here, else the first map's defaults (Steel when it is there,
	// as in the Godot client).
	FString Last, LastName;
	GConfig->GetString(UserSection, KeyLastSetup, Last, GGameUserSettingsIni);
	GConfig->GetString(UserSection, KeyLastName, LastName, GGameUserSettingsIni);
	GConfig->GetBool(UserSection, KeyLastPlayersChosen, bPlayersChosen, GGameUserSettingsIni);
	const auto Shown = [this](const FString& Map) { return Maps.ContainsByPredicate([&Map](const FScctHostMap& Entry) { return Entry.Id.Equals(Map, ESearchCase::IgnoreCase); }); };
	if (!Last.IsEmpty() && Shown(ScctHostSetup::Option(Last, TEXT("MapName"))))
	{
		Setup = ScctHostSetup::Text(EScctSetupText::Normalize, Last, FString(), PlayersArgument());
	}
	if (Setup.IsEmpty() && !Maps.IsEmpty())
	{
		const FString First = Shown(TEXT("SteeE")) ? FString(TEXT("SteeE")) : Shown(TEXT("AquaE")) ? FString(TEXT("AquaE")) : Maps[0].Id;
		bPlayersChosen = false;
		Setup = ScctHostSetup::Text(EScctSetupText::Defaults, FString(), FString::Printf(TEXT("?MapName=%s?Mode=Story"), *First));
	}

	// The name the player goes online under (kept by the online subsystem, which may not have read it yet).
	FString PlayerName = Online.IsValid() ? Online->GetPlayerName() : FString();
	if (PlayerName.IsEmpty())
	{
		GConfig->GetString(UserSection, TEXT("PlayerName"), PlayerName, GGameUserSettingsIni);
	}
	if (PlayerName.IsEmpty())
	{
		PlayerName = TEXT("Player");
	}
	NameBox = Ui::TextBox(TEXT("Session name"), LastName.IsEmpty() ? PlayerName + TEXT("'s game") : LastName);
	PasswordBox = Ui::TextBox(TEXT("Empty: anyone can join"), FString(), true, FOnTextCommitted::CreateLambda([this](const FText& Text, ETextCommit::Type)
	{
		// Only the characters the original allows, at most sixteen (Create_Match.InitComponent).
		const FString Filtered = ScctHostSetup::FilterPassword(Text.ToString());
		if (Filtered != Text.ToString())
		{
			PasswordBox->SetText(Ui::ToText(Filtered));
		}
	}));
	PortBox = Ui::TextBox(TEXT("1024 - 65535"), FString::FromInt(Defaults.Port));

	const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	Left->AddSlot().AutoHeight()
	[
		Ui::Title(TAttribute<FText>::CreateLambda([this]
		{
			const FString Map = ScctHostSetup::Option(Setup, TEXT("MapName"));
			const FScctHostMap* Entry = Maps.FindByPredicate([&Map](const FScctHostMap& Candidate) { return Candidate.Id.Equals(Map, ESearchCase::IgnoreCase); });
			return Ui::ToText(Entry != nullptr ? FString::Printf(TEXT("%s  (%s)"), *Entry->Name, *Entry->Id) : Map);
		}), 18.0f)
	];
	Left->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 12.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(Description); }), 10.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];
	if (Maps.IsEmpty())
	{
		Left->AddSlot().AutoHeight()
		[
			Ui::Text(Ui::ToText(TEXT("No map can be hosted: the OpenSCCT content folder was not found, or no installed map has been imported into this game.")),
				12.0f, FSlateColor(ScctHudStyle::Danger()), ScctHudStyle::EWeight::Regular, true)
		];
	}
	else
	{
		Left->AddSlot().AutoHeight()
		[
			SNew(SBox).HeightOverride(SettingsHeight - 70.0f)
			[
				SAssignNew(Picker, SScctMapPicker)
				.Maps(Maps)
				.TileWidth(124.0f)
				.Selected(TAttribute<FString>::CreateLambda([this] { return ScctHostSetup::Option(Setup, TEXT("MapName")); }))
				.OnPicked(FOnScctMapPicked::CreateSP(this, &SScctHostPage::PickMap))
			]
		];
	}

	ChildSlot
	[
		SNew(SBox).WidthOverride(PageWidth)
		[
			Ui::Glass(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)[ Ui::Title(Ui::ToText(TEXT("CREATE SESSION")), 24.0f) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.54f).Padding(0.0f, 0.0f, 26.0f, 0.0f)[ Left ]
					+ SHorizontalBox::Slot().FillWidth(0.46f)[ SAssignNew(SettingsBox, SBox).HeightOverride(SettingsHeight) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f)[ Ui::Rule() ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
					[
						Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(Error); }), 11.5f, FSlateColor(ScctHudStyle::Danger()), ScctHudStyle::EWeight::Regular, true)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateLambda([this] { OnBack.ExecuteIfBound(); return FReply::Handled(); }))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).IsEnabled(!Maps.IsEmpty())
						[
							Ui::Button(Ui::ToText(TEXT("CREATE SESSION")), FOnClicked::CreateLambda([this] { Create(); return FReply::Handled(); }), true, Ui::Accent(), 180.0f)
						]
					]
				])
		]
	];
	Refresh();

	// Unattended: a preset for the form, once per run (the page is built again when a visit ends).
	static bool bPresetApplied = false;
	FString Preset;
	if (!bPresetApplied && FParse::Value(FCommandLine::Get(), TEXT("ScctHostPreset="), Preset, false))
	{
		bPresetApplied = true;
		ApplyPreset(Preset);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: Create Session shows %d map(s); setup %s"), Maps.Num(), *Setup);

	// Unattended: CREATE pressed once per run, a moment after the page is up (default 2 s) so it can be seen first.
	static bool bSubmitArmed = false;
	float Seconds = 2.0f;
	if (!bSubmitArmed && (FParse::Value(FCommandLine::Get(), TEXT("ScctHostSubmit="), Seconds) || FParse::Param(FCommandLine::Get(), TEXT("ScctHostSubmit"))))
	{
		bSubmitArmed = true;
		SubmitAfter = FMath::Max(0.0f, Seconds);
	}
}

void SScctHostPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (SubmitAfter >= 0.0f)
	{
		SubmitAfter -= InDeltaTime;
		if (SubmitAfter < 0.0f)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctHostSubmit presses CREATE SESSION"));
			Create();
		}
	}
}

const FScctSetupRow* SScctHostPage::FindRow(const FString& Name) const
{
	return Rows.FindByPredicate([&Name](const FScctSetupRow& Row) { return Row.Row == Name; });
}

void SScctHostPage::SetSetup(const FString& Line)
{
	if (!Line.IsEmpty())
	{
		Setup = Line;
	}
	Refresh();
}

void SScctHostPage::Edit(const FString& Change)
{
	if (Change.StartsWith(TEXT("MaxPlayers ")))
	{
		bPlayersChosen = true;
	}
	SetSetup(ScctHostSetup::Text(EScctSetupText::Edit, Setup, Change, PlayersArgument()));
}

void SScctHostPage::EditAlternate(const FString& Change)
{
	SetSetup(ScctHostSetup::Text(EScctSetupText::EditAlternate, Setup, Change, PlayersArgument()));
}

void SScctHostPage::PickMap(const FString& Map)
{
	Edit(TEXT("Map ") + Map);
}

void SScctHostPage::Refresh()
{
	VoiceHint.Reset();
	BotHint.Reset();
	Rows = ScctHostSetup::Rows(Setup, PlayersArgument(), VoiceHint, BotHint);
	Description = ScctHostSetup::Text(EScctSetupText::Describe, Setup, FString(), PlayersArgument());
	if (const FScctSetupRow* Alternate = FindRow(TEXT("Alternate")); Alternate == nullptr || Alternate->Value == 0)
	{
		bAlternateOpen = false;
	}
	if (SettingsBox.IsValid())
	{
		SettingsBox->SetContent(bAlternateOpen ? BuildAlternate() : BuildSettings());
	}
}

// ---- the tabs -----------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctHostPage::RowWidget(const FString& Name)
{
	const FScctSetupRow* Found = FindRow(Name);
	if (Found == nullptr || !Found->bVisible)
	{
		return SNullWidget::NullWidget;
	}

	const FScctSetupRow Row = *Found;
	const TSharedRef<SWidget> Caption = Ui::Text(Ui::ToText(Row.Caption), 12.0f);
	TSharedRef<SWidget> Control = SNullWidget::NullWidget;
	switch (Row.Kind)
	{
	case FScctSetupRow::EKind::Number:
		Control = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).IsEnabled(Row.Value > Row.Min)
				[
					Ui::Button(Ui::ToText(TEXT("-")), FOnClicked::CreateLambda([this, Row] { Edit(FString::Printf(TEXT("%s %d"), *Row.Row, Row.Value - 1)); return FReply::Handled(); }))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(58.0f).HAlign(HAlign_Center)[ Ui::Text(Ui::ToText(Row.Shown), 13.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Bold) ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).IsEnabled(Row.Value < Row.Max)
				[
					Ui::Button(Ui::ToText(TEXT("+")), FOnClicked::CreateLambda([this, Row] { Edit(FString::Printf(TEXT("%s %d"), *Row.Row, Row.Value + 1)); return FReply::Handled(); }))
				]
			];
		break;
	case FScctSetupRow::EKind::Check:
		Control = Ui::Button(Ui::ToText(Row.Value != 0 ? TEXT("YES") : TEXT("NO")),
			FOnClicked::CreateLambda([this, Row] { Edit(FString::Printf(TEXT("%s %d"), *Row.Row, Row.Value != 0 ? 0 : 1)); return FReply::Handled(); }),
			Row.Value != 0, Ui::Accent(), 70.0f);
		break;
	case FScctSetupRow::EKind::Choice:
	{
		// A choice is a caption over its buttons; the page is too narrow to put them beside it.
		const TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
		for (int32 Index = 0; Index < Row.Choices.Num(); ++Index)
		{
			Buttons->AddSlot()
			[
				Ui::Button(Ui::ToText(Row.Choices[Index].ToUpper()),
					FOnClicked::CreateLambda([this, Row, Index] { Edit(FString::Printf(TEXT("%s %d"), *Row.Row, Index)); return FReply::Handled(); }),
					Index == Row.Value)
			];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 6.0f)[ Caption ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Buttons ];
	}
	}

	return SNew(SBox).Padding(0.0f, 2.0f, 0.0f, 5.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Caption ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Control ]
	];
}

void SScctHostPage::AddRows(const TSharedRef<SVerticalBox>& Box, std::initializer_list<const TCHAR*> Names)
{
	for (const TCHAR* Name : Names)
	{
		Box->AddSlot().AutoHeight()[ RowWidget(Name) ];
	}
}

TSharedRef<SWidget> SScctHostPage::Toggle(const FString& Caption, bool& Value, const TAttribute<bool>& Enabled)
{
	bool* const Flag = &Value;
	return SNew(SBox).Padding(0.0f, 2.0f, 0.0f, 5.0f).IsEnabled(Enabled)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Ui::Text(Ui::ToText(Caption), 12.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Ui::Button(TAttribute<FText>::CreateLambda([Flag] { return Ui::ToText(*Flag ? TEXT("YES") : TEXT("NO")); }),
				FOnClicked::CreateLambda([Flag] { *Flag = !*Flag; return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([Flag] { return *Flag; }), Ui::Accent(), 70.0f)
		]
	];
}

TSharedRef<SWidget> SScctHostPage::BuildSettings()
{
	const auto TabButton = [this](const TCHAR* Label, ETab Target)
	{
		return Ui::Button(Ui::ToText(Label), FOnClicked::CreateLambda([this, Target] { Tab = Target; Refresh(); return FReply::Handled(); }), Tab == Target, Ui::Accent(), 110.0f);
	};

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	switch (Tab)
	{
	case ETab::Game:
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Ui::Label(TEXT("Session name")) ];
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ NameBox.ToSharedRef() ];
		AddRows(Box, { TEXT("Mode"), TEXT("TimeLimit"), TEXT("Voice") });
		Box->AddSlot().AutoHeight()[ Note(Ui::ToText(VoiceHint)) ];
		Box->AddSlot().AutoHeight()[ Heading(TEXT("Players & bots")) ];
		AddRows(Box, { TEXT("MaxPlayers"), TEXT("Bots"), TEXT("BotsPerSide"), TEXT("SpyBots"), TEXT("MercBots"), TEXT("BotSkill") });
		Box->AddSlot().AutoHeight()[ Note(Ui::ToText(BotHint)) ];
		break;

	case ETab::Rules:
	{
		Box->AddSlot().AutoHeight()[ Heading(TEXT("Round & objectives")) ];
		AddRows(Box, { TEXT("SpyLives"), TEXT("MercLives"), TEXT("Respawn"), TEXT("ActiveDisks"), TEXT("DisksToTake") });
		Box->AddSlot().AutoHeight()[ Note(Ui::ToText(TEXT("Set lives to 0 for unlimited respawns."))) ];
		Box->AddSlot().AutoHeight()[ Heading(TEXT("Teams & rotation")) ];
		AddRows(Box, { TEXT("Revenge"), TEXT("GamesInARow"), TEXT("RandomMap"), TEXT("AutoBalance"), TEXT("VoiceInLobby") });
		Box->AddSlot().AutoHeight()[ Heading(TEXT("Extra rules")) ];
		AddRows(Box, { TEXT("Practice"), TEXT("Sidearms"), TEXT("Rifles"), TEXT("ResponsiveSmoke"), TEXT("Alternate") });
		const FScctSetupRow* Alternate = FindRow(TEXT("Alternate"));
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f).HAlign(HAlign_Left)
		[
			SNew(SBox).IsEnabled(Alternate != nullptr && Alternate->Value != 0)
			[
				Ui::Button(Ui::ToText(TEXT("CONFIGURE ALTERNATE RULES")), FOnClicked::CreateLambda([this] { bAlternateOpen = true; Refresh(); return FReply::Handled(); }))
			]
		];
		break;
	}

	case ETab::Hosting:
	{
		FString MasterUrl, InviteKey;
		UScctOnlineSubsystem::ReadMasterSettings(MasterUrl, InviteKey);
		const bool bMaster = !MasterUrl.IsEmpty();
		if (bLoopbackOnly)
		{
			Box->AddSlot().AutoHeight()[ Note(Ui::ToText(TEXT("Test run (-ScctHostPreset Loopback=1): this machine only, so no listing, router or LAN answers whatever is chosen below."))) ];
		}
		Box->AddSlot().AutoHeight()[ Heading(TEXT("Access & discovery")) ];
		Box->AddSlot().AutoHeight()[ Toggle(TEXT("Show in the server browser"), bListed, bMaster) ];
		Box->AddSlot().AutoHeight()
		[
			Note(TAttribute<FText>::CreateLambda([this, bMaster, MasterUrl]
			{
				return Ui::ToText(!bMaster ? FString(TEXT("No master server is set (Options, Online): players join through LAN or by address."))
					: bListed ? FString::Printf(TEXT("Listed on %s: players find it in the server list."), *MasterUrl)
					: FString(TEXT("Not listed: players join through LAN or by address.")));
			}))
		];
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 6.0f)[ Ui::Label(TEXT("Password")) ];
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ PasswordBox.ToSharedRef() ];
		Box->AddSlot().AutoHeight()[ Toggle(TEXT("Signed-in players only"), bRequireAccounts, bMaster) ];
		if (!bMaster)
		{
			Box->AddSlot().AutoHeight()[ Note(Ui::ToText(TEXT("Signed-in players only needs a master server: players get their tickets from it."))) ];
		}
		Box->AddSlot().AutoHeight()[ Heading(TEXT("Connection")) ];
		Box->AddSlot().AutoHeight()[ Toggle(TEXT("Open the router port automatically (UPnP)"), bUpnp) ];
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 6.0f)[ Ui::Label(TEXT("UDP port")) ];
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ PortBox.ToSharedRef() ];
		Box->AddSlot().AutoHeight()[ Toggle(FString::Printf(TEXT("Answer LAN searches (port %d)"), LanPort), bLan) ];
		Box->AddSlot().AutoHeight()[ Heading(TEXT("Your recording")) ];
		Box->AddSlot().AutoHeight()[ Toggle(TEXT("Record demos"), bRecordDemos) ];
		Box->AddSlot().AutoHeight()[ Note(Ui::ToText(TEXT("Saves every round of this session as a demo in Saved/Demos."))) ];
		break;
	}
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ TabButton(TEXT("GAME"), ETab::Game) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ TabButton(TEXT("RULES"), ETab::Rules) ]
			+ SHorizontalBox::Slot().AutoWidth()[ TabButton(TEXT("HOSTING"), ETab::Hosting) ]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(0.0f, 0.0f, 14.0f, 0.0f)[ Box ]
		];
}

TSharedRef<SWidget> SScctHostPage::BuildAlternate()
{
	const TArray<FScctAlternateRow> Alternate = ScctHostSetup::AlternateRows(Setup, PlayersArgument());
	const auto Side = [this, &Alternate](int32 Team)
	{
		const TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
		Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Ui::Label(Team == 0 ? TEXT("Spies") : TEXT("Mercenaries"), ScctHudStyle::Accent(Team)) ];
		for (const FScctAlternateRow::EKind Kind : { FScctAlternateRow::EKind::Ability, FScctAlternateRow::EKind::Gadget, FScctAlternateRow::EKind::Slots })
		{
			if (Kind != FScctAlternateRow::EKind::Slots)
			{
				Column->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)[ Ui::Label(Kind == FScctAlternateRow::EKind::Ability ? TEXT("Abilities") : TEXT("Gadgets")) ];
			}
			for (const FScctAlternateRow& Row : Alternate)
			{
				if (Row.Kind != Kind || Row.Side != Team)
				{
					continue;
				}

				TSharedRef<SWidget> Control = SNullWidget::NullWidget;
				FString Caption = Row.Caption;
				if (Kind == FScctAlternateRow::EKind::Slots)
				{
					Caption = TEXT("Number of gadgets");
					Control = SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).IsEnabled(Row.Value > 0)
							[
								Ui::Button(Ui::ToText(TEXT("-")), FOnClicked::CreateLambda([this, Row] { EditAlternate(FString::Printf(TEXT("slots %d %d"), Row.Side, Row.Value - 1)); return FReply::Handled(); }))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox).WidthOverride(34.0f).HAlign(HAlign_Center)[ Ui::Text(Ui::ToText(FString::FromInt(Row.Value)), 12.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Bold) ]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).IsEnabled(Row.Value < Row.Most)
							[
								Ui::Button(Ui::ToText(TEXT("+")), FOnClicked::CreateLambda([this, Row] { EditAlternate(FString::Printf(TEXT("slots %d %d"), Row.Side, Row.Value + 1)); return FReply::Handled(); }))
							]
						];
				}
				else
				{
					const FString Change = Kind == FScctAlternateRow::EKind::Ability
						? FString::Printf(TEXT("ability %d %d"), Row.Id, Row.Value != 0 ? 0 : 1)
						: FString::Printf(TEXT("gadget %d %d %d"), Row.Side, Row.Id, Row.Value != 0 ? 0 : 1);
					Control = Ui::Button(Ui::ToText(Row.Value != 0 ? TEXT("ON") : TEXT("OFF")),
						FOnClicked::CreateLambda([this, Change] { EditAlternate(Change); return FReply::Handled(); }), Row.Value != 0, ScctHudStyle::Accent(Team), 56.0f);
				}
				Column->AddSlot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Ui::Text(Ui::ToText(Caption), 11.0f) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Control ]
				];
			}
		}
		return Column;
	};

	const FScctAlternateRow* Cinematic = Alternate.FindByPredicate([](const FScctAlternateRow& Row) { return Row.Kind == FScctAlternateRow::EKind::Ability && Row.Side == 2; });
	TSharedRef<SWidget> CinematicRow = SNullWidget::NullWidget;
	if (Cinematic != nullptr)
	{
		const FString Change = FString::Printf(TEXT("ability %d %d"), Cinematic->Id, Cinematic->Value != 0 ? 0 : 1);
		CinematicRow = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Ui::Text(Ui::ToText(Cinematic->Caption), 12.0f) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(Ui::ToText(Cinematic->Value != 0 ? TEXT("ON") : TEXT("OFF")), FOnClicked::CreateLambda([this, Change] { EditAlternate(Change); return FReply::Handled(); }),
					Cinematic->Value != 0, Ui::Accent(), 56.0f)
			];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Ui::Label(TEXT("Alternate rules"), Ui::Accent()) ]
		+ SVerticalBox::Slot().AutoHeight()[ Note(Ui::ToText(TEXT("Choose which abilities and gadgets each team can use."))) ]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(0.0f, 0.0f, 14.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 18.0f, 0.0f)[ Side(0) ]
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ Side(1) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ CinematicRow ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				Ui::Button(Ui::ToText(TEXT("DONE")), FOnClicked::CreateLambda([this] { bAlternateOpen = false; Refresh(); return FReply::Handled(); }), true, Ui::Accent(), 110.0f)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("STOCK VALUES")), FOnClicked::CreateLambda([this] { EditAlternate(TEXT("reset")); return FReply::Handled(); }))
			]
		];
}

// ---- unattended and CREATE -----------------------------------------------------------------------------------------

void SScctHostPage::ApplyPreset(const FString& Preset)
{
	TArray<FString> Pairs;
	Preset.ParseIntoArray(Pairs, TEXT("?"), true);
	TMap<FString, FString> Values;
	bool bRandomMap = false;
	for (const FString& Pair : Pairs)
	{
		FString Key, Value;
		if (Pair.Split(TEXT("="), &Key, &Value))
		{
			Values.Add(Key, Value);
		}
	}

	// The map and mode first: they reload the rules from the map, which the other values then change.
	const FString* Map = Values.Find(TEXT("MapName"));
	const FString* Mode = Values.Find(TEXT("Mode"));
	if (Map != nullptr || Mode != nullptr)
	{
		const FString Argument = FString::Printf(TEXT("?MapName=%s?Mode=%s"), Map != nullptr ? **Map : *ScctHostSetup::Option(Setup, TEXT("MapName")), Mode != nullptr ? **Mode : TEXT(""));
		Setup = ScctHostSetup::Text(EScctSetupText::Defaults, Setup, Argument, PlayersArgument());
	}

	FString Line = Setup;
	for (const TPair<FString, FString>& Pair : Values)
	{
		const FString& Key = Pair.Key;
		const FString& Value = Pair.Value;
		if (Key == TEXT("MapName") || Key == TEXT("Mode")) { continue; }
		else if (Key == TEXT("Name")) { NameBox->SetText(Ui::ToText(Value)); }
		else if (Key == TEXT("Password")) { PasswordBox->SetText(Ui::ToText(Value)); }
		else if (Key == TEXT("Port")) { PortBox->SetText(Ui::ToText(Value)); }
		else if (Key == TEXT("Players")) { bPlayersChosen = true; Line = WithOption(Line, TEXT("MaxPlayers"), Value); }
		else if (Key == TEXT("Listed")) { bListed = IsYes(Value); }
		else if (Key == TEXT("Upnp")) { bUpnp = IsYes(Value); }
		else if (Key == TEXT("Accounts")) { bRequireAccounts = IsYes(Value); }
		else if (Key == TEXT("Demos")) { bRecordDemos = IsYes(Value); }
		else if (Key == TEXT("Lan")) { bLan = IsYes(Value); }
		else if (Key == TEXT("Loopback")) { bLoopbackOnly = IsYes(Value); }
		else if (Key == TEXT("Tab") && Value.Equals(TEXT("alternate"), ESearchCase::IgnoreCase)) { Tab = ETab::Rules; bAlternateOpen = true; }
		else if (Key == TEXT("Tab")) { Tab = Value.Equals(TEXT("rules"), ESearchCase::IgnoreCase) ? ETab::Rules : Value.Equals(TEXT("hosting"), ESearchCase::IgnoreCase) ? ETab::Hosting : ETab::Game; }
		else if (Key == TEXT("Filter")) { if (Picker.IsValid()) { Picker->SetModeFilter(Value); } }
		else if (Key == TEXT("Random")) { bRandomMap = IsYes(Value); }
		else { Line = WithOption(Line, Key, Value); }
	}
	SetSetup(ScctHostSetup::Text(EScctSetupText::Normalize, Line, FString(), PlayersArgument()));
	if (bRandomMap && Picker.IsValid())
	{
		// RANDOM MAP pressed (after the filter, so the draw is among the maps it shows).
		Picker->PickRandom();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctHostPreset filled Create Session: %s (%s)"), *Setup, *Description);
}

bool SScctHostPage::Create()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || Setup.IsEmpty())
	{
		Error = TEXT("The game cannot host: the online service is not available.");
		return false;
	}

	const FString PortText = PortBox->GetText().ToString().TrimStartAndEnd();
	const int32 Port = FCString::Atoi(*PortText);
	if (!PortText.IsNumeric() || Port < 1024 || Port > 65535)
	{
		Error = TEXT("The UDP port must be a number from 1024 to 65535.");
		return false;
	}

	FString MasterUrl, InviteKey;
	UScctOnlineSubsystem::ReadMasterSettings(MasterUrl, InviteKey);
	FScctHostRequest Request = FScctHostRequest::Default();
	Request.Name = NameBox->GetText().ToString().TrimStartAndEnd().Left(32);
	if (Request.Name.IsEmpty())
	{
		Request.Name = Visit->GetPlayerName() + TEXT("'s game");
	}
	Request.Password = ScctHostSetup::FilterPassword(PasswordBox->GetText().ToString());
	Request.SetupOptions = Setup;
	Request.Port = Port;
	Request.MaxPlayers = FMath::Clamp(FCString::Atoi(*ScctHostSetup::Option(Setup, TEXT("MaxPlayers"))), 2, 24);
	Request.bListed = bListed && !MasterUrl.IsEmpty();
	Request.bUpnp = bUpnp;
	Request.bRequireAccounts = bRequireAccounts && !MasterUrl.IsEmpty();
	Request.bRecordDemos = bRecordDemos;
	Request.bLan = bLan;
	Request.LanPort = LanPort;
	Request.bLoopbackOnly = bLoopbackOnly;
	if (bLoopbackOnly)
	{
		// A test host on this machine only: nobody outside could reach it, so it asks neither the router nor the master.
		Request.bListed = Request.bUpnp = Request.bLan = false;
	}
	else
	{
		// Kept for the next time, as the Godot client keeps them: the hosting choices on the Online page, the session here.
		Request.Remember();
		GConfig->SetString(UserSection, KeyLastSetup, *Setup, GGameUserSettingsIni);
		GConfig->SetString(UserSection, KeyLastName, *Request.Name, GGameUserSettingsIni);
		GConfig->SetBool(UserSection, KeyLastPlayersChosen, bPlayersChosen, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: Create Session: hosting '%s' on UDP port %d%s: %s"), *Request.Name, Request.Port,
		bLoopbackOnly ? TEXT(" (loopback only)") : TEXT(""), *Description);
	FString Failure;
	if (!Visit->Host(Request, &Failure))
	{
		Error = Failure.IsEmpty() ? FString(TEXT("The game could not be hosted.")) : Failure;
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: Create Session failed: %s"), *Error);
		return false;
	}
	Error.Reset();
	return true;
}
