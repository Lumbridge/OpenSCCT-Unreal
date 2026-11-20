#include "Menu/SScctSessionOptions.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Menu/SScctMapPicker.h"
#include "Menu/ScctMenuNav.h"
#include "Menu/ScctMenuStyle.h"
#include "Menu/ScctSetupRowsView.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace Ui = ScctMenuStyle;

namespace
{
	constexpr float OptionsWidth = 1260.0f;

	/** An options line as its pairs, in order. */
	TArray<TPair<FString, FString>> Pairs(const FString& Line)
	{
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("?"), true);
		TArray<TPair<FString, FString>> Out;
		for (const FString& Part : Parts)
		{
			FString Key, Value;
			if (Part.Split(TEXT("="), &Key, &Value))
			{
				Out.Emplace(Key, Value);
			}
		}
		return Out;
	}

	/** Sets Key in an options line, replacing its value or adding it at the end. */
	FString LobbyWithOption(const FString& Line, const FString& Key, const FString& Value)
	{
		TArray<TPair<FString, FString>> List = Pairs(Line);
		bool bFound = false;
		for (TPair<FString, FString>& Pair : List)
		{
			if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase))
			{
				Pair.Value = Value;
				bFound = true;
			}
		}
		if (!bFound)
		{
			List.Emplace(Key, Value);
		}
		FString Out;
		for (const TPair<FString, FString>& Pair : List)
		{
			Out += TEXT("?") + Pair.Key + TEXT("=") + Pair.Value;
		}
		return Out;
	}

	/** What a setup value is called on the page (LobbySetup.ToOptions's names); the Alternate rules' many names are one. */
	FString CaptionOfOption(const FString& Key)
	{
		static const TMap<FString, FString> Captions = {
			{ TEXT("MapName"), TEXT("Map") }, { TEXT("GameMode"), TEXT("Game mode") }, { TEXT("TimeLimit"), TEXT("Game length") },
			{ TEXT("ScoreLimit"), TEXT("Score limit") }, { TEXT("LivesATT"), TEXT("Spy lives") }, { TEXT("LivesDEF"), TEXT("Mercenary lives") },
			{ TEXT("TimesRespawnsATT"), TEXT("Respawn") }, { TEXT("SvmDisks"), TEXT("Active disks") }, { TEXT("SvmDisksToWin"), TEXT("Disks to take") },
			{ TEXT("SvmDisksMax"), TEXT("Disks") }, { TEXT("Revenge"), TEXT("Revenge") }, { TEXT("GamesInARow"), TEXT("Games in a row") },
			{ TEXT("MaxPlayers"), TEXT("Player slots") }, { TEXT("SvmVoice"), TEXT("Voice chat") }, { TEXT("SvmVoiceLobby"), TEXT("Voice in lobby") },
			{ TEXT("SvmRandomMap"), TEXT("Random map") }, { TEXT("SvmBal"), TEXT("Auto balance") }, { TEXT("BalanceTeams"), TEXT("Auto balance") },
			{ TEXT("SvmBots"), TEXT("Bots") }, { TEXT("SvmBotsPerSide"), TEXT("Bots per side") }, { TEXT("SvmSpyBots"), TEXT("Spy bots") },
			{ TEXT("SvmMercBots"), TEXT("Mercenary bots") }, { TEXT("SvmBotSkill"), TEXT("Bot skill") }, { TEXT("SvmPr"), TEXT("Practice") },
		};
		const FString* Found = Captions.Find(Key);
		return Found != nullptr ? *Found : FString(TEXT("Alternate rules"));
	}
}

void SScctSessionOptions::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	OnClose = InArgs._OnClose;
	Dialogs = InArgs._Dialogs;
	Maps = ScctHostSetup::Maps();

	ChildSlot.HAlign(HAlign_Left)[ SAssignNew(Body, SBox) ];
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		ChangedHandle = Visit->OnChanged.AddSP(this, &SScctSessionOptions::HandleChanged);
	}
	SyncSetup();
	bControlShown = CanControl();
	Rebuild();
}

SScctSessionOptions::~SScctSessionOptions()
{
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		Visit->OnChanged.Remove(ChangedHandle);
	}
}

void SScctSessionOptions::Open()
{
	SyncSetup();
	bControlShown = CanControl();
	Rebuild();
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby Session Options open (%s), %d hostable map(s) here"), CanControl() ? TEXT("may change the setup") : TEXT("read-only"), Maps.Num());
}

TSharedPtr<SWidget> SScctSessionOptions::GetInitialControl() const
{
	if (const TSharedPtr<SWidget> Active = ActiveTabButton.Pin())
	{
		return Active;
	}
	return ScctMenuNav::FindFirstFocusable(RowsBox);
}

void SScctSessionOptions::FocusInitialControl() const
{
	if (const TSharedPtr<SWidget> Focus = GetInitialControl())
	{
		FSlateApplication::Get().SetAllUserFocus(Focus, EFocusCause::SetDirectly);
	}
}

void SScctSessionOptions::HandleChanged()
{
	// Raised from the visit's frame, before Slate lays the page out, so rows made here are measured before they are drawn.
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || Visit->GetRevision() == SeenRevision)
	{
		return;
	}
	SeenRevision = Visit->GetRevision();
	const bool bSetupChanged = SyncSetup();
	if (CanControl() != bControlShown)
	{
		// Control came or went (the host left, an admin logged in): the page is drawn again for the new role.
		bControlShown = CanControl();
		Rebuild();
		return;
	}
	if (bSetupChanged)
	{
		LoadPicture(ShownMap());
		RefreshOptions();
	}
}

bool SScctSessionOptions::CanControl() const
{
	return Online.IsValid() && Online->HasFlag(EScctOnlineStatusFlags::CanControl);
}

const FScctHostMap* SScctSessionOptions::FindMap(const FString& Id) const
{
	return Maps.FindByPredicate([&Id](const FScctHostMap& Map) { return Map.Id.Equals(Id, ESearchCase::IgnoreCase); });
}

FString SScctSessionOptions::ServerMap() const
{
	return Online.IsValid() ? Online->GetSetupMap() : FString();
}

FString SScctSessionOptions::ShownMap() const
{
	return CanControl() && !Draft.IsEmpty() ? ScctHostSetup::Option(Draft, TEXT("MapName")) : ServerMap();
}

int32 SScctSessionOptions::PlayersOf(const FString& Line) const
{
	// The lobby's player slots are the setup's own: the model keeps them rather than following the map's.
	return FMath::Max(0, FCString::Atoi(*ScctHostSetup::Option(Line, TEXT("MaxPlayers"))));
}

void SScctSessionOptions::Rebuild()
{
	RowsBox.Reset();
	Picker.Reset();
	LoadPicture(ShownMap());
	Body->SetContent(BuildOptions());
	SeenRevision = Online.IsValid() ? Online->GetRevision() : -1;
	RefreshOptions();
}

// ---- the setup and its draft ------------------------------------------------------------------------------------

bool SScctSessionOptions::SyncSetup()
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	const FString Now = Visit != nullptr ? Visit->GetSetupOptions() : FString();
	if (Now == ServerSetup)
	{
		return false;
	}
	ServerSetup = Now;

	// Through the model, as the draft is: values it would only reformat are not changes. A map the model does not know
	// (not installed here) would be swapped for another, so that setup is taken as the server wrote it.
	FString Normalized = Now;
	if (!Now.IsEmpty() && FindMap(ScctHostSetup::Option(Now, TEXT("MapName"))) != nullptr)
	{
		const FString Line = ScctHostSetup::Text(EScctSetupText::Normalize, Now, FString(), PlayersOf(Now));
		Normalized = Line.IsEmpty() ? Now : Line;
	}

	const bool bHadChanges = IsDirty();
	Baseline = Normalized;
	if (bAwaitingEcho || !bHadChanges)
	{
		Draft = Baseline;
		bDraftConflict = false;
	}
	else if (Draft != Baseline)
	{
		bDraftConflict = true;
	}
	bAwaitingEcho = false;
	return true;
}

void SScctSessionOptions::SetDraft(const FString& Line)
{
	if (!Line.IsEmpty())
	{
		Draft = Line;
	}
	RefreshOptions();
}

void SScctSessionOptions::Edit(const FString& Change)
{
	SetDraft(ScctHostSetup::Text(EScctSetupText::Edit, Draft, Change, PlayersOf(Draft)));
}

void SScctSessionOptions::EditAlternate(const FString& Change)
{
	SetDraft(ScctHostSetup::Text(EScctSetupText::EditAlternate, Draft, Change, PlayersOf(Draft)));
}

FString SScctSessionOptions::ChangedOptions(TArray<FString>* OutNames) const
{
	TMap<FString, FString> Before;
	for (const TPair<FString, FString>& Pair : Pairs(Baseline))
	{
		Before.Add(Pair.Key, Pair.Value);
	}

	FString Out;
	for (const TPair<FString, FString>& Pair : Pairs(Draft))
	{
		const FString* Old = Before.Find(Pair.Key);
		if (Old == nullptr || !Old->Equals(Pair.Value, ESearchCase::CaseSensitive))
		{
			Out += TEXT("?") + Pair.Key + TEXT("=") + Pair.Value;
			if (OutNames != nullptr)
			{
				OutNames->AddUnique(CaptionOfOption(Pair.Key));
			}
		}
	}
	return Out;
}

void SScctSessionOptions::FillDraft(const FString& Line)
{
	SyncSetup();
	FString Next = Draft.IsEmpty() ? Baseline : Draft;

	// The map and mode first: they load their own rules, which the other values then change.
	FString Mode;
	for (const TPair<FString, FString>& Pair : Pairs(Line))
	{
		if (Pair.Key.Equals(TEXT("MapName"), ESearchCase::IgnoreCase))
		{
			if (const FScctHostMap* Entry = FindMap(Pair.Value))
			{
				Next = ScctHostSetup::MapDefaults(*Entry, Next, PlayersOf(Next));
			}
		}
		else if (Pair.Key.Equals(TEXT("Mode"), ESearchCase::IgnoreCase))
		{
			Mode = Pair.Value;
		}
		else if (Pair.Key.Equals(TEXT("GameMode"), ESearchCase::IgnoreCase))
		{
			Mode = Pair.Value.IsNumeric() ? ScctHostSetup::ModeKeyOf(FCString::Atoi(*Pair.Value)) : Pair.Value;
		}
	}
	if (!Mode.IsEmpty())
	{
		Next = ScctHostSetup::Text(EScctSetupText::Edit, Next, TEXT("Mode ") + Mode, PlayersOf(Next));
	}
	for (const TPair<FString, FString>& Pair : Pairs(Line))
	{
		const FString& Key = Pair.Key;
		if (Key.Equals(TEXT("MapName"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("Mode"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("GameMode"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Key.Equals(TEXT("Tab"), ESearchCase::IgnoreCase))
		{
			Tab = Pair.Value.Equals(TEXT("game"), ESearchCase::IgnoreCase) ? ETab::Game : ETab::Rules;
			bAlternateOpen = Pair.Value.Equals(TEXT("alternate"), ESearchCase::IgnoreCase);
			continue;
		}
		Next = LobbyWithOption(Next, Key, Pair.Value);
	}
	Next = ScctHostSetup::Text(EScctSetupText::Normalize, Next, FString(), PlayersOf(Next));
	if (!Next.IsEmpty())
	{
		Draft = Next;
	}
	Rebuild();

	TArray<FString> Names;
	const FString Changes = ChangedOptions(&Names);
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyApply put into the Session Options draft: %s (%s): %s"), *FString::Join(Names, TEXT(", ")), *DraftDescription, *Changes);
}

bool SScctSessionOptions::Apply()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !CanControl() || !IsDirty())
	{
		return false;
	}

	TArray<FString> Names;
	const FString Changes = ChangedOptions(&Names);
	if (!Visit->SetSetupOptions(Changes))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: Session Options could not send the setup (the visit is not in the lobby)"));
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: Session Options APPLY sent %d change(s): %s"), Names.Num(), *FString::Join(Names, TEXT(", ")));
	bAwaitingEcho = true;
	bDraftConflict = false;
	OnClose.ExecuteIfBound();
	return true;
}

void SScctSessionOptions::Discard()
{
	Draft = Baseline;
	bDraftConflict = false;
	OnClose.ExecuteIfBound();
}

void SScctSessionOptions::RandomMap()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || !CanControl())
	{
		return;
	}

	// A map that offers the mode being set up, never the current one; it comes with its own rules (SetMatch).
	const FString ModeKey = ScctHostSetup::ModeKeyOf(Visit->GetStatus().Mode);
	TArray<FString> Candidates;
	for (const FScctHostMap& Map : Maps)
	{
		if (!Map.Id.Equals(ServerMap(), ESearchCase::IgnoreCase) && Map.Modes.ContainsByPredicate([&ModeKey](const FString& Mode) { return Mode.Equals(ModeKey, ESearchCase::IgnoreCase); }))
		{
			Candidates.Add(Map.Id);
		}
	}
	if (Candidates.IsEmpty())
	{
		return;
	}

	// Within the map lists' group when it holds one of them (the Godot client's RandomMapDialog opens on the saved group).
	const FScctMapGroup Group = ScctMapBrowser::ActiveGroupEntry();
	const TArray<FString> InGroup = Candidates.FilterByPredicate([&Group](const FString& Id) { return Group.Holds(Id); });
	if (!InGroup.IsEmpty())
	{
		Candidates = InGroup;
	}
	const TArray<FScctHostMap> Choices = Maps.FilterByPredicate([&](const FScctHostMap& Map) { return Candidates.Contains(Map.Id); });
	SScctMapPicker::ShowDraw(Dialogs, Choices, FOnScctMapPicked::CreateSPLambda(this, [this](const FString& Id)
	{
		if (Online.IsValid() && CanControl()) Online->SetMatch(Id, -1);
	}));
}

// ---- the picture ------------------------------------------------------------------------------------------------

void SScctSessionOptions::LoadPicture(const FString& MapId)
{
	if (MapId.Equals(PictureMap, ESearchCase::IgnoreCase))
	{
		return;
	}
	PictureMap = MapId;
	PictureBrush.Reset();
	PictureTexture.Reset();

	const FScctHostMap* Entry = FindMap(MapId);
	if (Entry == nullptr || Entry->Picture.IsEmpty() || !FPaths::FileExists(Entry->Picture))
	{
		return;
	}
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Entry->Picture);
	if (Texture == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: the picture of %s could not be read (%s)"), *MapId, *Entry->Picture);
		return;
	}
	PictureTexture.Reset(Texture);
	PictureBrush = MakeShared<FSlateBrush>();
	PictureBrush->SetResourceObject(Texture);
	PictureBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	PictureBrush->DrawAs = ESlateBrushDrawType::Image;
}

TSharedRef<SWidget> SScctSessionOptions::MapPicture(float Width)
{
	static const FSlateColorBrush Back(FLinearColor::White);
	return SNew(SBox).WidthOverride(Width).HeightOverride(FMath::RoundToFloat(Width * 0.75f)).Clipping(EWidgetClipping::ClipToBounds)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SImage).Image(&Back).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.05f)) ]
		// No picture (a map without one, or one this game does not have): its code where it would be.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(PictureBrush.IsValid() ? FString() : PictureMap); }), 12.0f, FSlateColor(ScctHudStyle::TextFaint()))
		]
		+ SOverlay::Slot()
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFill)
			[
				SNew(SImage)
				.Image(TAttribute<const FSlateBrush*>::CreateLambda([this] { return PictureBrush.Get(); }))
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return PictureBrush.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
			]
		]
	];
}

// ---- Session Options --------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctSessionOptions::BuildOptions()
{
	const bool bControl = CanControl();
	const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	Left->AddSlot().AutoHeight()
	[
		Ui::Title(TAttribute<FText>::CreateLambda([this]
		{
			const FString Map = ShownMap();
			const FScctHostMap* Entry = FindMap(Map);
			return Ui::ToText(Entry != nullptr ? FString::Printf(TEXT("%s  (%s)"), *Entry->Name, *Entry->Id) : Map);
		}), 18.0f)
	];
	Left->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 12.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(DraftDescription); }), 10.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];
	if (bControl && !Maps.IsEmpty())
	{
		// The controller picks from the maps this game can show; a pick loads that map's rules into the draft.
		Left->AddSlot().FillHeight(1.0f)
		[
			SAssignNew(Picker, SScctMapPicker)
			.Dialogs(Dialogs)
			.OnConfirmed(FSimpleDelegate::CreateSPLambda(this, [this] { if (IsDirty()) Apply(); else OnClose.ExecuteIfBound(); }))
			.Maps(Maps)
			.TileWidth(124.0f)
			.Selected(TAttribute<FString>::CreateLambda([this] { return ShownMap(); }))
			.OnPicked(FOnScctMapPicked::CreateLambda([this](const FString& Map)
			{
				if (const FScctHostMap* Entry = FindMap(Map))
				{
					SetDraft(ScctHostSetup::MapDefaults(*Entry, Draft, PlayersOf(Draft)));
				}
			}))
		];
	}
	else
	{
		Left->AddSlot().AutoHeight()[ MapPicture(420.0f) ];
	}

	return SNew(SBox).WidthOverride(OptionsWidth)
	[
		Ui::Glass(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Ui::Title(Ui::ToText(bControl ? TEXT("SESSION OPTIONS") : TEXT("MATCH SETTINGS")), 24.0f) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 16.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([this, bControl]
				{
					return Ui::ToText(!bControl ? FString(TEXT("These are the host's current match settings."))
						: bDraftConflict ? FString(TEXT("The match changed while you were editing. Review your changes before applying."))
						: IsDirty() ? FString(TEXT("You have unapplied changes. APPLY updates the match for everyone and un-readies the players."))
						: FString(TEXT("Choose the map and rules, then apply your changes. Changing map or mode loads its starting rules.")));
				}), 11.5f, TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(bDraftConflict ? ScctHudStyle::Danger() : ScctHudStyle::TextDim()); }),
					ScctHudStyle::EWeight::Regular, true)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.54f).Padding(0.0f, 0.0f, 26.0f, 0.0f)[ Left ]
				+ SHorizontalBox::Slot().FillWidth(0.46f)[ SAssignNew(RowsBox, SBox) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f)[ Ui::Rule() ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					Ui::Text(TAttribute<FText>::CreateLambda([this]
					{
						TArray<FString> Names;
						ChangedOptions(&Names);
						return Ui::ToText(IsDirty() ? TEXT("Changed: ") + FString::Join(Names, TEXT(", ")) : FString());
					}), 11.5f, FSlateColor(Ui::Accent()), ScctHudStyle::EWeight::Regular, true)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					Ui::Button(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(IsDirty() && CanControl() ? TEXT("DISCARD") : TEXT("CLOSE")); }),
						FOnClicked::CreateLambda([this] { Discard(); return FReply::Handled(); }))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility(bControl ? EVisibility::Visible : EVisibility::Collapsed)
					.IsEnabled(TAttribute<bool>::CreateLambda([this] { return IsDirty(); }))
					[
						Ui::Button(Ui::ToText(TEXT("APPLY")), FOnClicked::CreateLambda([this] { Apply(); return FReply::Handled(); }), true, Ui::Accent(), 150.0f)
					]
				]
			])
	];
}

void SScctSessionOptions::RefreshOptions()
{
	const FString& Shown = CanControl() ? Draft : Baseline;
	VoiceHint.Reset();
	BotHint.Reset();
	Rows = ScctHostSetup::Rows(Shown, PlayersOf(Shown), VoiceHint, BotHint);
	DraftDescription = ScctHostSetup::Text(EScctSetupText::Describe, Shown, FString(), PlayersOf(Shown));
	if (!CanControl() && Online.IsValid())
	{
		// The server's own words for its setup (the model may not know a map this game does not have).
		DraftDescription = Online->GetSetupDescription();
	}
	const FScctSetupRow* Alternate = Rows.FindByPredicate([](const FScctSetupRow& Row) { return Row.Row == TEXT("Alternate"); });
	if (Alternate == nullptr || Alternate->Value == 0)
	{
		bAlternateOpen = false;
	}
	if (RowsBox.IsValid())
	{
		RowsBox->SetContent(BuildRows());
	}
}

TSharedRef<SWidget> SScctSessionOptions::BuildRows()
{
	// Rows are edited only on a map the model knows; on another the controller picks a map first.
	const bool bEditable = CanControl() && FindMap(ShownMap()) != nullptr;
	const FString& Shown = CanControl() ? Draft : Baseline;
	const ScctSetupRowsView::FOnEdit OnEdit = [this](const FString& Change) { Edit(Change); };
	const ScctSetupRowsView::FOnEdit OnEditAlternate = [this](const FString& Change) { EditAlternate(Change); };
	const auto FindRow = [this](const TCHAR* Name) { return Rows.FindByPredicate([Name](const FScctSetupRow& Row) { return Row.Row == Name; }); };
	const auto AddRows = [&](const TSharedRef<SVerticalBox>& Box, std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* Name : Names)
		{
			Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Row(FindRow(Name), OnEdit, bEditable) ];
		}
	};

	if (bAlternateOpen)
	{
		ActiveTabButton.Reset();
		const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				Ui::Button(Ui::ToText(TEXT("DONE")), FOnClicked::CreateLambda([this] { bAlternateOpen = false; RefreshOptions(); return FReply::Handled(); }), true, Ui::Accent(), 110.0f)
			];
		if (bEditable)
		{
			Buttons->AddSlot().AutoWidth()
			[
				Ui::Button(Ui::ToText(TEXT("STOCK VALUES")), FOnClicked::CreateLambda([this] { EditAlternate(TEXT("reset")); return FReply::Handled(); }))
			];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Ui::Label(TEXT("Alternate rules"), Ui::Accent()) ]
			+ SVerticalBox::Slot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(TEXT("Which abilities and gadgets each team can use."))) ]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot().Padding(0.0f, 0.0f, 14.0f, 0.0f)
				[
					ScctSetupRowsView::Alternate(ScctHostSetup::AlternateRows(Shown, PlayersOf(Shown)), OnEditAlternate, bEditable)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ Buttons ];
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (CanControl() && !bEditable)
	{
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(TEXT("This game does not have the setup's map: pick one of the maps on the left to change the rules."))) ];
	}
	if (Tab == ETab::Game)
	{
		AddRows(Box, { TEXT("Mode"), TEXT("TimeLimit"), TEXT("Voice") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(VoiceHint)) ];
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Players & bots")) ];
		AddRows(Box, { TEXT("MaxPlayers"), TEXT("Bots"), TEXT("BotsPerSide"), TEXT("SpyBots"), TEXT("MercBots"), TEXT("BotSkill") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(BotHint)) ];
	}
	else
	{
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Round & objectives")) ];
		AddRows(Box, { TEXT("SpyLives"), TEXT("MercLives"), TEXT("Respawn"), TEXT("ActiveDisks"), TEXT("DisksToTake") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Note(Ui::ToText(TEXT("Set lives to 0 for unlimited respawns."))) ];
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Teams & rotation")) ];
		AddRows(Box, { TEXT("Revenge"), TEXT("GamesInARow"), TEXT("RandomMap"), TEXT("AutoBalance"), TEXT("VoiceInLobby") });
		Box->AddSlot().AutoHeight()[ ScctSetupRowsView::Heading(TEXT("Extra rules")) ];
		AddRows(Box, { TEXT("Practice"), TEXT("Alternate") });
		const FScctSetupRow* Alternate = FindRow(TEXT("Alternate"));
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f).HAlign(HAlign_Left)
		[
			SNew(SBox).IsEnabled(Alternate != nullptr && Alternate->Value != 0)
			[
				Ui::Button(Ui::ToText(bEditable ? TEXT("CONFIGURE ALTERNATE RULES") : TEXT("VIEW ALTERNATE RULES")),
					FOnClicked::CreateLambda([this] { bAlternateOpen = true; RefreshOptions(); return FReply::Handled(); }))
			]
		];
	}

	const auto TabButton = [this](const TCHAR* Label, ETab Target)
	{
		const TSharedRef<SButton> Button = Ui::Button(Ui::ToText(Label), FOnClicked::CreateLambda([this, Target]
		{
			if (Tab != Target)
			{
				Tab = Target;
				RefreshOptions();
				FocusInitialControl();
			}
			return FReply::Handled();
		}), Tab == Target, Ui::Accent(), 110.0f);
		if (Tab == Target)
		{
			ActiveTabButton = Button;
		}
		return Button;
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[ TabButton(TEXT("GAME"), ETab::Game) ]
			+ SHorizontalBox::Slot().AutoWidth()[ TabButton(TEXT("RULES"), ETab::Rules) ]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(0.0f, 0.0f, 14.0f, 0.0f)[ Box ]
		];
}
