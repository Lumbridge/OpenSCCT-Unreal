#include "Menu/SScctMapPicker.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Menu/ScctMenuStyle.h"
#include "Menu/ScctMenuKit.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace Ui = ScctMenuStyle;

namespace
{
	class SScctMapTile : public SScctRingButton
	{
	public:
		FSimpleDelegate Confirm;

		virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Confirm.IsBound())
			{
				Confirm.Execute();
				return FReply::Handled();
			}
			return SScctRingButton::OnMouseButtonDoubleClick(Geometry, Event);
		}
	};

	// The modes in the order the original's mode box lists them, then the ones only custom maps have.
	const TCHAR* const ModeOrder[] = { TEXT("Story"), TEXT("DiskHunt"), TEXT("Deathmatch"), TEXT("MercDeathMatch"), TEXT("Extraction"), TEXT("Sabotage"), TEXT("Neutralization") };

	/** A tile: a white box the button's colour paints (as the menu buttons do), with room around the picture. */
	const FButtonStyle& TileStyle()
	{
		static const FButtonStyle Style = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 4.0f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, 4.0f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor::White, 4.0f))
			.SetNormalPadding(FMargin(5.0f))
			.SetPressedPadding(FMargin(5.0f, 6.0f, 5.0f, 4.0f));
		return Style;
	}

	bool Offers(const FScctHostMap& Map, const FString& ModeKey)
	{
		return ModeKey.IsEmpty() || Map.Modes.ContainsByPredicate([&ModeKey](const FString& Mode) { return Mode.Equals(ModeKey, ESearchCase::IgnoreCase); });
	}
}

void SScctMapPicker::Construct(const FArguments& InArgs)
{
	Maps = InArgs._Maps;
	Selected = InArgs._Selected;
	OnPicked = InArgs._OnPicked;
	OnConfirmed = InArgs._OnConfirmed;
	Dialogs = InArgs._Dialogs;
	TileWidth = InArgs._TileWidth;

	// The catalog's groups with the one picked last, the search words, and how the lists are drawn.
	Groups = ScctMapBrowser::Groups();
	Details = ScctMapBrowser::Details();
	View = ScctMapBrowser::View();
	const FString Saved = ScctMapBrowser::ActiveGroup();
	GroupIndex = FMath::Max(Groups.IndexOfByPredicate([&Saved](const FScctMapGroup& Group) { return Group.Name.Equals(Saved, ESearchCase::IgnoreCase); }), 0);

	// The mode filter: any, then every mode some map offers, in the menu's order.
	ModeFilters = { FString() };
	for (const TCHAR* Mode : ModeOrder)
	{
		if (Maps.ContainsByPredicate([Mode](const FScctHostMap& Map) { return Offers(Map, Mode); }))
		{
			ModeFilters.Add(Mode);
		}
	}

	SearchBox = Ui::TextBox(TEXT("Search by map name or code..."), FString(), false, FOnTextCommitted(), FOnTextChanged::CreateLambda([this](const FText& Text)
	{
		const FString Typed = Text.ToString().TrimStartAndEnd();
		if (Typed != Search)
		{
			Search = Typed;
			Refill();
		}
	}));
	SearchBox->SetToolTipText(Ui::ToText(TEXT("Search installed maps by name or map code")));

	// Each of the three choices is one button that steps to the next value: the pages are too narrow for a row of buttons each.
	FilterRow = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
	const TSharedRef<SButton> GroupButton = Ui::Button(
		TAttribute<FText>::CreateLambda([this] { return Ui::ToText(TEXT("GROUP: ") + (Groups.IsValidIndex(GroupIndex) ? Groups[GroupIndex].Title.ToUpper() : FString(TEXT("ALL INSTALLED MAPS")))); }),
		FOnClicked::CreateLambda([this]
		{
			if (Groups.Num() > 1)
			{
				SetGroup(Groups[(GroupIndex + 1) % Groups.Num()].Name);
			}
			return FReply::Handled();
		}),
		TAttribute<bool>::CreateLambda([this] { return Groups.IsValidIndex(GroupIndex) && !Groups[GroupIndex].bAll; }));
	GroupButton->SetToolTipText(TAttribute<FText>::CreateLambda([this]
	{
		// Every group with its number of maps here; the one shown is marked.
		FString Text = TEXT("Filter maps by group. A press shows the next group.");
		for (int32 Index = 0; Index < Groups.Num(); ++Index)
		{
			const FScctMapGroup& Group = Groups[Index];
			const int32 Count = Maps.FilterByPredicate([&Group](const FScctHostMap& Map) { return Group.Holds(Map.Id); }).Num();
			Text += FString::Printf(TEXT("\n%s %s (%d): %s"), Index == GroupIndex ? TEXT(">") : TEXT("  "), *Group.Title, Count, *Group.Description);
		}
		return Ui::ToText(Text);
	}));
	FilterRow->AddSlot()[ GroupButton ];

	const TSharedRef<SButton> ViewButton = Ui::Button(
		TAttribute<FText>::CreateLambda([this] { return Ui::ToText(TEXT("VIEW: ") + ScctMapBrowser::ViewTitle(View).ToUpper()); }),
		FOnClicked::CreateLambda([this]
		{
			SetView(View == EScctMapView::PictureGrid ? EScctMapView::List : View == EScctMapView::List ? EScctMapView::ThumbnailList : EScctMapView::PictureGrid);
			return FReply::Handled();
		}));
	ViewButton->SetToolTipText(Ui::ToText(TEXT("Map display: picture grid, list or thumbnail list. Kept for every map list.")));
	FilterRow->AddSlot()[ ViewButton ];

	if (ModeFilters.Num() > 2)
	{
		const TSharedRef<SButton> ModeButton = Ui::Button(
			TAttribute<FText>::CreateLambda([this] { return Ui::ToText(TEXT("MODE: ") + (ModeFilter.IsEmpty() ? FString(TEXT("ANY")) : ScctHostSetup::ModeLabel(ModeFilter).ToUpper())); }),
			FOnClicked::CreateLambda([this]
			{
				const int32 At = FMath::Max(ModeFilters.IndexOfByPredicate([this](const FString& Mode) { return Mode.Equals(ModeFilter, ESearchCase::IgnoreCase); }), 0);
				SetModeFilter(ModeFilters[(At + 1) % ModeFilters.Num()]);
				return FReply::Handled();
			}),
			TAttribute<bool>::CreateLambda([this] { return !ModeFilter.IsEmpty(); }));
		ModeButton->SetToolTipText(Ui::ToText(TEXT("Show only the maps that offer a mode. A press shows the next mode.")));
		FilterRow->AddSlot()[ ModeButton ];
	}
	FilterRow->AddSlot()
	[
		Ui::Button(Ui::ToText(TEXT("RANDOM MAP")), FOnClicked::CreateLambda([this] { PickRandom(); return FReply::Handled(); }), false, ScctHudStyle::Good())
	];

	const TSharedRef<SButton> ResetButton = Ui::Button(Ui::ToText(TEXT("RESET FILTERS")), FOnClicked::CreateLambda([this] { ResetFilters(); return FReply::Handled(); }));
	ResetButton->SetToolTipText(Ui::ToText(TEXT("Clear the search and show all installed maps")));
	ResetButton->SetVisibility(TAttribute<EVisibility>::CreateLambda([this] { return IsFiltered() ? EVisibility::Visible : EVisibility::Hidden; }));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ SearchBox.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ FilterRow.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(ShownCount == 1 ? FString(TEXT("1 map available")) : FString::Printf(TEXT("%d maps available"), ShownCount)); }),
					10.0f, FSlateColor(ScctHudStyle::TextDim()))
			]
			+ SHorizontalBox::Slot().AutoWidth()[ ResetButton ]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(Scroll, SScrollBox)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return ShownCount > 0 ? EVisibility::Visible : EVisibility::Collapsed; }))
				+ SScrollBox::Slot()[ SAssignNew(ListHost, SBox) ]
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).Visibility(TAttribute<EVisibility>::CreateLambda([this] { return ShownCount > 0 ? EVisibility::Collapsed : EVisibility::HitTestInvisible; }))
				[
					SNew(STextBlock)
					.Text(Ui::ToText(TEXT("No maps match these filters.\nTry another name, map code or group.")))
					.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 11.0f, 60))
					.ColorAndOpacity(ScctHudStyle::TextDim())
					.Justification(ETextJustify::Center)
				]
			]
		]
	];
	Refill();
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: map picker shows %d of %d map(s), group \"%s\" of %d, %s, %d with search words"), ShownCount, Maps.Num(),
		Groups.IsValidIndex(GroupIndex) ? *Groups[GroupIndex].Name : TEXT("All"), Groups.Num(), *ScctMapBrowser::ViewTitle(View), Details.Num());
}

void SScctMapPicker::SetModeFilter(const FString& ModeKey)
{
	if (ModeFilter != ModeKey)
	{
		ModeFilter = ModeKey;
		Refill();
	}
}

void SScctMapPicker::SetGroup(const FString& Name, bool bSave)
{
	int32 Found = Groups.IndexOfByPredicate([&Name](const FScctMapGroup& Group) { return Group.Name.Equals(Name, ESearchCase::IgnoreCase); });
	if (Found == INDEX_NONE && !Name.IsEmpty())
	{
		Found = Groups.IndexOfByPredicate([&Name](const FScctMapGroup& Group) { return Group.Name.StartsWith(Name, ESearchCase::IgnoreCase); });
	}
	if (Found == INDEX_NONE && Name.IsNumeric())
	{
		// A number is the group's place in the list (-ScctHostPreset Group=1, for a check that does not know the names).
		Found = FCString::Atoi(*Name);
	}
	Found = FMath::Max(Found, 0);
	if (!Groups.IsValidIndex(Found))
	{
		return;
	}
	if (bSave)
	{
		ScctMapBrowser::SetActiveGroup(Groups[Found].Name);
	}
	if (Found != GroupIndex)
	{
		GroupIndex = Found;
		Refill();
		UE_LOG(LogOpenSCCT, Log, TEXT("Menu: map picker group \"%s\": %d map(s) shown"), *Groups[Found].Name, ShownCount);
	}
}

void SScctMapPicker::SetSearch(const FString& Text)
{
	if (SearchBox.IsValid())
	{
		// The box tells the list (its OnTextChanged).
		SearchBox->SetText(Ui::ToText(Text));
	}
	const FString Typed = Text.TrimStartAndEnd();
	if (Typed != Search)
	{
		Search = Typed;
		Refill();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: map picker search \"%s\": %d map(s) shown"), *Search, ShownCount);
}

void SScctMapPicker::SetView(EScctMapView NewView, bool bSave)
{
	if (bSave)
	{
		ScctMapBrowser::SetView(NewView);
	}
	if (NewView != View)
	{
		View = NewView;
		Refill();
	}
}

bool SScctMapPicker::IsFiltered() const
{
	return !Search.IsEmpty() || !ModeFilter.IsEmpty() || (Groups.IsValidIndex(GroupIndex) && !Groups[GroupIndex].bAll);
}

void SScctMapPicker::ResetFilters()
{
	ModeFilter.Reset();
	Search.Reset();
	if (SearchBox.IsValid())
	{
		SearchBox->SetText(FText::GetEmpty());
	}
	const int32 All = FMath::Max(Groups.IndexOfByPredicate([](const FScctMapGroup& Group) { return Group.bAll; }), 0);
	if (Groups.IsValidIndex(All))
	{
		ScctMapBrowser::SetActiveGroup(Groups[All].Name);
		GroupIndex = All;
	}
	Refill();
}

bool SScctMapPicker::Shows(const FScctHostMap& Map) const
{
	return Offers(Map, ModeFilter) && (!Groups.IsValidIndex(GroupIndex) || Groups[GroupIndex].Holds(Map.Id))
		&& ScctMapBrowser::Matches(Search, Map.Id, Map.Name, Details.Find(Map.Id.ToLower()));
}

void SScctMapPicker::PickRandom(bool bConfirm)
{
	TArray<FScctHostMap> Candidates = Maps.FilterByPredicate([this](const FScctHostMap& Map) { return Shows(Map); });
	const FString Current = Selected.Get();
	if (Candidates.Num() > 1) Candidates.RemoveAll([&](const FScctHostMap& Map) { return Map.Id.Equals(Current, ESearchCase::IgnoreCase); });
	if (Candidates.IsEmpty()) return;
	if (bConfirm)
	{
		ShowDraw(Dialogs, Candidates, FOnScctMapPicked::CreateSPLambda(this, [this](const FString& Map) { OnPicked.ExecuteIfBound(Map); }));
	}
	else
	{
		OnPicked.ExecuteIfBound(Candidates[FMath::RandHelper(Candidates.Num())].Id);
	}
}

void SScctMapPicker::ShowDraw(TWeakPtr<SScctDialogLayer> Layer, const TArray<FScctHostMap>& Candidates, FOnScctMapPicked Use)
{
	const TSharedPtr<SScctDialogLayer> Dialogs = Layer.Pin();
	if (!Dialogs || Candidates.IsEmpty()) return;
	const int32 Winner = FMath::RandHelper(Candidates.Num());
	const double Start = FPlatformTime::Seconds();
	FScctDialog Dialog;
	Dialog.Title = TEXT("Random map");
	Dialog.LiveBody = [Candidates, Start, Winner]
	{
		const int32 StepsLeft = FMath::Max(0, 12 - FMath::FloorToInt32((FPlatformTime::Seconds() - Start) * 12));
		const FScctHostMap& Map = Candidates[(Winner + StepsLeft) % Candidates.Num()];
		return Map.Name.IsEmpty() ? Map.Id : Map.Name;
	};
	Dialog.Buttons.Add({ TEXT("Use map"), EScctButtonLook::Primary, [Use, Id = Candidates[Winner].Id](const FString&) { Use.ExecuteIfBound(Id); },
		TAttribute<bool>::CreateLambda([Start] { return FPlatformTime::Seconds() >= Start + 1.0; }) });
	Dialog.Buttons.Add({ TEXT("Spin again"), EScctButtonLook::Secondary, [Layer, Candidates, Use](const FString&) { ShowDraw(Layer, Candidates, Use); } });
	Dialog.Buttons.Add({ TEXT("Cancel"), EScctButtonLook::Secondary, {} });
	Dialogs->Show(Dialog);
}

void SScctMapPicker::Reveal(const FString& MapId)
{
	const TWeakPtr<SWidget>* Tile = TileOf.Find(MapId.ToLower());
	const TSharedPtr<SWidget> Pinned = Tile != nullptr ? Tile->Pin() : nullptr;
	if (Pinned.IsValid() && Scroll.IsValid())
	{
		Scroll->ScrollDescendantIntoView(Pinned, false, EDescendantScrollDestination::IntoView);
	}
}

void SScctMapPicker::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// A newly selected map (a pick, RANDOM MAP, the last session's map) is brought into view once, and again after
	// the list was made anew; the player's scrolling is left alone otherwise.
	const FString Current = Selected.Get();
	if (!Current.Equals(Revealed, ESearchCase::IgnoreCase) && AllottedGeometry.GetLocalSize().Y > 1.0f)
	{
		Revealed = Current;
		Reveal(Current);
	}
}

const FSlateBrush* SScctMapPicker::PictureOf(const FScctHostMap& Map)
{
	if (const FPicture* Found = Pictures.Find(Map.Id))
	{
		return Found->Brush.Get();
	}
	if (Map.Picture.IsEmpty() || MissingPictures.Contains(Map.Id) || !FPaths::FileExists(Map.Picture))
	{
		MissingPictures.Add(Map.Id);
		return nullptr;
	}

	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Map.Picture);
	if (Texture == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: the picture of %s could not be read (%s)"), *Map.Id, *Map.Picture);
		MissingPictures.Add(Map.Id);
		return nullptr;
	}

	FPicture& Picture = Pictures.Add(Map.Id);
	Picture.Texture.Reset(Texture);
	Picture.Brush = MakeShared<FSlateBrush>();
	Picture.Brush->SetResourceObject(Texture);
	Picture.Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	Picture.Brush->DrawAs = ESlateBrushDrawType::Image;
	return Picture.Brush.Get();
}

TSharedRef<SWidget> SScctMapPicker::MakeTile(const FScctHostMap& Map)
{
	const FString Id = Map.Id;
	FString Tip = FString::Printf(TEXT("%s (%s)"), *Map.Name, *Map.Id);
	if (const FScctMapDetails* Entry = Details.Find(Map.Id.ToLower()))
	{
		// What an authored map says about itself.
		if (!Entry->Description.IsEmpty()) { Tip += TEXT("\n") + Entry->Description; }
		if (!Entry->Credit.IsEmpty()) { Tip += TEXT("\n") + Entry->Credit; }
	}

	const auto PictureWidget = [this, &Map](bool bCodeWhenMissing) -> TSharedRef<SWidget>
	{
		if (const FSlateBrush* Brush = PictureOf(Map))
		{
			return SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[ SNew(SImage).Image(Brush) ];
		}
		// No picture: the map's code where it would be (the thumbnail list has the code beside it already).
		return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[ bCodeWhenMissing ? Ui::Label(Map.Id, ScctHudStyle::TextFaint()) : SNullWidget::NullWidget ];
	};

	TSharedPtr<SWidget> Content;
	if (View == EScctMapView::PictureGrid)
	{
		const float PictureHeight = FMath::RoundToFloat(TileWidth * 0.75f);
		Content = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(TileWidth).HeightOverride(PictureHeight).Clipping(EWidgetClipping::ClipToBounds)[ PictureWidget(true) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 6.0f, 2.0f, 2.0f)
			[
				SNew(SBox).WidthOverride(TileWidth)
				[
					SNew(STextBlock)
					.Text(Ui::ToText(Map.Name))
					.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 9.0f, 60))
					.ColorAndOpacity(ScctHudStyle::Text())
					.AutoWrapText(true)
				]
			];
	}
	else if (View == EScctMapView::List)
	{
		// One line per map: its name and its code.
		Content = SNew(SBox).Padding(FMargin(6.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(Ui::ToText(FString::Printf(TEXT("%s (%s)"), *Map.Name, *Map.Id)))
				.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 10.5f, 60))
				.ColorAndOpacity(ScctHudStyle::Text())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			];
	}
	else
	{
		// A small landscape picture, then the name over the code.
		Content = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(80.0f).HeightOverride(45.0f).Clipping(EWidgetClipping::ClipToBounds)[ PictureWidget(false) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(12.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Ui::ToText(Map.Name))
					.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 11.0f, 60))
					.ColorAndOpacity(ScctHudStyle::Text())
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Ui::ToText(Map.Id))
					.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 9.0f, 60))
					.ColorAndOpacity(ScctHudStyle::TextDim())
				]
			];
	}

	const TSharedRef<SScctMapTile> Tile = SNew(SScctMapTile)
		.ButtonStyle(&TileStyle())
		.IsFocusable(true)
		.HAlign(View == EScctMapView::PictureGrid ? HAlign_Center : HAlign_Fill)
		.ToolTipText(Ui::ToText(Tip))
		.OnClicked(FOnClicked::CreateLambda([this, Id] { OnPicked.ExecuteIfBound(Id); return FReply::Handled(); }))
		[
			Content.ToSharedRef()
		];

	if (OnConfirmed.IsBound())
	{
		Tile->Confirm = FSimpleDelegate::CreateSPLambda(this, [this, Id]
		{
			if (!Selected.Get().Equals(Id, ESearchCase::IgnoreCase)) OnPicked.ExecuteIfBound(Id);
			OnConfirmed.ExecuteIfBound();
		});
	}

	// The selected map is lit in the accent, as a picked button is.
	const TWeakPtr<SButton> Weak = Tile;
	Tile->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this, Weak, Id]
	{
		const TSharedPtr<SButton> Pinned = Weak.Pin();
		const bool bHovered = Pinned.IsValid() && (Pinned->IsHovered() || Pinned->HasKeyboardFocus());
		if (Selected.Get().Equals(Id, ESearchCase::IgnoreCase))
		{
			return FSlateColor(ScctHudStyle::Fade(Ui::Accent(), bHovered ? 0.75f : 0.6f));
		}
		return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, bHovered ? 0.16f : 0.06f));
	}));
	TileOf.Add(Map.Id.ToLower(), Tile);
	return Tile;
}

void SScctMapPicker::Refill()
{
	if (!ListHost.IsValid())
	{
		return;
	}

	TileOf.Reset();
	ShownCount = 0;
	const bool bGrid = View == EScctMapView::PictureGrid;
	const TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f));
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FScctHostMap& Map : Maps)
	{
		if (!Shows(Map))
		{
			continue;
		}

		++ShownCount;
		if (bGrid)
		{
			Grid->AddSlot()[ MakeTile(Map) ];
		}
		else
		{
			Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 8.0f, View == EScctMapView::List ? 3.0f : 6.0f)[ MakeTile(Map) ];
		}
	}
	ListHost->SetContent(bGrid ? StaticCastSharedRef<SWidget>(Grid) : StaticCastSharedRef<SWidget>(Rows));

	// The selected map again when it is among those shown (a filter that hid it and shows it again, another view).
	Revealed.Reset();
}
