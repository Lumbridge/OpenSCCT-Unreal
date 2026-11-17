#include "Menu/SScctHostPage.h"

#include "Menu/SScctMapPicker.h"
#include "Menu/ScctSetupRowsView.h"
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
	const TCHAR* const HostUserSection = TEXT("OpenSCCT.Online");
	const TCHAR* const KeyLastSetup = TEXT("HostSetup");
	const TCHAR* const KeyLastName = TEXT("HostName");
	const TCHAR* const KeyLastPlayersChosen = TEXT("HostPlayersChosen");

	constexpr float HostPageWidth = 1260.0f;
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

	using ScctSetupRowsView::Heading;
	using ScctSetupRowsView::Note;
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
	GConfig->GetString(HostUserSection, KeyLastSetup, Last, GGameUserSettingsIni);
	GConfig->GetString(HostUserSection, KeyLastName, LastName, GGameUserSettingsIni);
	GConfig->GetBool(HostUserSection, KeyLastPlayersChosen, bPlayersChosen, GGameUserSettingsIni);
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
		GConfig->GetString(HostUserSection, TEXT("PlayerName"), PlayerName, GGameUserSettingsIni);
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
		SNew(SBox).WidthOverride(HostPageWidth)
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
	return ScctSetupRowsView::Row(FindRow(Name), [this](const FString& Change) { Edit(Change); }, true);
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

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Ui::Label(TEXT("Alternate rules"), Ui::Accent()) ]
		+ SVerticalBox::Slot().AutoHeight()[ Note(Ui::ToText(TEXT("Choose which abilities and gadgets each team can use."))) ]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(0.0f, 0.0f, 14.0f, 0.0f)
			[
				ScctSetupRowsView::Alternate(Alternate, [this](const FString& Change) { EditAlternate(Change); }, true)
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
		GConfig->SetString(HostUserSection, KeyLastSetup, *Setup, GGameUserSettingsIni);
		GConfig->SetString(HostUserSection, KeyLastName, *Request.Name, GGameUserSettingsIni);
		GConfig->SetBool(HostUserSection, KeyLastPlayersChosen, bPlayersChosen, GGameUserSettingsIni);
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
