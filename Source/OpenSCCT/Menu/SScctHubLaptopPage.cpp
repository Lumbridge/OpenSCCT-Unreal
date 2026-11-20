#include "Menu/SScctHubLaptopPage.h"
#include "Menu/ScctMenuNav.h"

#include "Engine/Texture2D.h"
#include "Hub/ScctHubPads.h"
#include "Menu/ScctMenuStyle.h"
#include "Menu/ScctSelectBox.h"
#include "Misc/PackageName.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace LaptopUi = ScctMenuStyle;

namespace
{
	constexpr int32 MaxRows = 24;
	constexpr int32 MaxMaps = 96;
	constexpr float MapTileWidth = 150.0f;
	constexpr float MapPictureHeight = 84.0f;

	FString Fixed(const TCHAR* Text, int32 Capacity)
	{
		int32 Length = 0;
		while (Length < Capacity && Text[Length] != 0)
		{
			++Length;
		}
		return FString::ConstructFromPtrSize(Text, Length);
	}

	FString PadTitle(const FString& PadId)
	{
		return PadId.StartsWith(TEXT("Pad")) && PadId.Len() > 3 ? TEXT("Pad ") + PadId.Mid(3) : PadId;
	}
}

void SScctHubLaptopPage::Construct(const FArguments& InArgs)
{
	Pads = InArgs._Pads;
	MapGroups = ScctMapBrowser::Groups();
	MapDetails = ScctMapBrowser::Details();
	const FString SavedGroup = ScctMapBrowser::ActiveGroup();
	MapGroupIndex = FMath::Max(MapGroups.IndexOfByPredicate([&SavedGroup](const FScctMapGroup& Group)
	{
		return Group.Name.Equals(SavedGroup, ESearchCase::IgnoreCase);
	}), 0);
	TArray<FString> MapGroupLabels;
	for (const FScctMapGroup& Group : MapGroups)
	{
		MapGroupLabels.Add(Group.Title);
	}
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(1120.0f)
		.MaxDesiredHeight(760.0f)
		[
			LaptopUi::Glass(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					LaptopUi::Title(TAttribute<FText>::CreateLambda([this] { return LaptopUi::ToText(Title); }), 26.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 14.0f)
				[
					LaptopUi::Text(TAttribute<FText>::CreateLambda([this] { return LaptopUi::ToText(Status); }), 12.5f, FSlateColor(LaptopUi::Accent()))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 20.0f, 0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							// The map group and the search of the other map lists (the Godot client's MapPickerView on its laptop panel).
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)[ LaptopUi::Label(TEXT("Map")) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
							[
								SNew(SScctSelectBox)
								.Options(MoveTemp(MapGroupLabels))
								.Prefix(TEXT("Group: "))
								.FieldLabel(LaptopUi::ToText(TEXT("Map group")))
								.EmptyText(LaptopUi::ToText(TEXT("All installed maps")))
								.Get([this] { return MapGroupIndex; })
								.Set([this](int32 Index)
								{
									MapGroupIndex = Index;
									ScctMapBrowser::SetActiveGroup(MapGroups[MapGroupIndex].Name);
									MapsKey.Reset();
									RebuildMaps();
								})
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
							[
								LaptopUi::TextBox(TEXT("Search maps"), FString(), false, FOnTextCommitted(), FOnTextChanged::CreateLambda([this](const FText& Text)
								{
									const FString Typed = Text.ToString().TrimStartAndEnd();
									if (Typed != MapSearch)
									{
										MapSearch = Typed;
										MapsKey.Reset();
										RebuildMaps();
									}
								}))
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								LaptopUi::Text(TAttribute<FText>::CreateLambda([this] { return LaptopUi::ToText(MapsShown == 1 ? FString(TEXT("1 map")) : FString::Printf(TEXT("%d maps"), MapsShown)); }),
									10.0f, FSlateColor(ScctHudStyle::TextDim()))
							]
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()[ SAssignNew(MapBox, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f)) ]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).WidthOverride(400.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ LaptopUi::Label(TEXT("Session")) ]
							+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(RowBox, SVerticalBox) ]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 12.0f)
				[
					LaptopUi::Text(TAttribute<FText>::CreateLambda([this] { return LaptopUi::ToText(Summary); }), 11.5f, FSlateColor(ScctHudStyle::TextDim()),
						ScctHudStyle::EWeight::Regular, true)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f)
					[
						LaptopUi::Button(LaptopUi::ToText(TEXT("MAP DEFAULTS")), FOnClicked::CreateLambda([this]
						{
							if (const TSharedPtr<FScctHubPads> Shared = Pads.Pin(); Shared.IsValid() && Shared->GetApi() != nullptr)
							{
								Shared->GetApi()->LaptopDefaults();
								Refresh(true);
							}
							return FReply::Handled();
						}), false, LaptopUi::Accent(), 150.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f)
					[
						LaptopUi::Button(LaptopUi::ToText(TEXT("APPLY")), FOnClicked::CreateLambda([this]
						{
							if (const TSharedPtr<FScctHubPads> Shared = Pads.Pin(); Shared.IsValid() && Shared->GetApi() != nullptr)
							{
								Shared->GetApi()->ApplyLaptop();
								Refresh(true);
							}
							return FReply::Handled();
						}), true, LaptopUi::Accent(), 150.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f)
					[
						LaptopUi::Button(LaptopUi::ToText(TEXT("CLOSE (ESC)")), FOnClicked::CreateLambda([this]
						{
							if (const TSharedPtr<FScctHubPads> Shared = Pads.Pin())
							{
								Shared->CloseLaptop();
							}
							return FReply::Handled();
						}), false, LaptopUi::Accent(), 150.0f)
					]
				],
				FMargin(28.0f), 0.8f)
		]
	];
	Refresh(true);
}

FReply SScctHubLaptopPage::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Key)
{
	if (ScctMenuNav::IsBackKey(Key.GetKey()))
	{
		if (const TSharedPtr<FScctHubPads> Shared = Pads.Pin())
		{
			Shared->CloseLaptop();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(Geometry, Key);
}

void SScctHubLaptopPage::Refresh(bool bForce)
{
	const TSharedPtr<FScctHubPads> Shared = Pads.Pin();
	if (!Shared.IsValid() || Shared->GetApi() == nullptr)
	{
		return;
	}
	Title = FString::Printf(TEXT("%s - NEXT MATCH"), *PadTitle(Shared->ReadText(EScctHubPadsText::LaptopPad)).ToUpper());
	Status = Shared->ReadText(EScctHubPadsText::LaptopStatus);
	Summary = Shared->ReadText(EScctHubPadsText::LaptopSummary);
	if (bForce)
	{
		RowsKey.Reset();
		MapsKey.Reset();
	}
	RebuildRows();
	RebuildMaps();
}

void SScctHubLaptopPage::RebuildRows()
{
	const TSharedPtr<FScctHubPads> Shared = Pads.Pin();
	const FScctHubPadsApi* Api = Shared.IsValid() ? Shared->GetApi() : nullptr;
	if (Api == nullptr || !RowBox.IsValid())
	{
		return;
	}
	TArray<FScctHubLaptopRow> Rows;
	Rows.SetNumZeroed(MaxRows);
	Rows.SetNum(FMath::Min(Api->GetLaptopRows(Rows.GetData(), Rows.Num()), MaxRows));
	FString Key = Shared->ReadText(EScctHubPadsText::LaptopMap);
	for (const FScctHubLaptopRow& Row : Rows)
	{
		Key += FString::Printf(TEXT("%s=%s:%d:%d:%d;"), Row.Key, Row.Value, Row.Enabled, Row.Index, Row.Count);
	}
	if (Key == RowsKey)
	{
		return;
	}
	RowsKey = Key;

	const FScctHubLaptopChoicesApi* ChoicesApi = ScctFeature<FScctHubLaptopChoicesApi>();
	RowBox->ClearChildren();
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FScctHubLaptopRow& Row = Rows[Index];
		const FText Label = LaptopUi::ToText(Fixed(Row.Label, UE_ARRAY_COUNT(Row.Label)));
		const FText Value = LaptopUi::ToText(Fixed(Row.Value, UE_ARRAY_COUNT(Row.Value)));
		TSharedRef<SWidget> Control = LaptopUi::Text(Value, 12.5f, FSlateColor(ScctHudStyle::TextFaint()), ScctHudStyle::EWeight::Bold);
		if (ChoicesApi && Row.Enabled != 0 && Row.Count > 1)
		{
			TArray<FScctHubLaptopChoice> Choices;
			Choices.SetNumZeroed(Row.Count);
			const int32 Count = ChoicesApi->GetChoices(Index, Choices.GetData(), Choices.Num());
			if (Count == Choices.Num())
			{
				TArray<FString> Labels;
				for (const FScctHubLaptopChoice& Choice : Choices)
				{
					Labels.Add(Fixed(Choice.Label, UE_ARRAY_COUNT(Choice.Label)));
				}
				Control = SNew(SScctSelectBox)
					.Options(MoveTemp(Labels))
					.FieldLabel(Label)
					.EmptyText(Value)
					.Get([Selected = Row.Index] { return Selected; })
					.Set([this, ChoicesApi, Index](int32 Selected)
					{
						if (ChoicesApi->Select(Index, Selected))
						{
							Refresh(false);
						}
					});
			}
		}
		const FLinearColor Colour = Row.Enabled != 0 ? ScctHudStyle::Text() : ScctHudStyle::TextFaint();
		RowBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				LaptopUi::Text(Label, 12.5f, FSlateColor(Colour))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).MinDesiredWidth(150.0f)[ Control ]
			]
		];
	}
}

const FSlateBrush* SScctHubLaptopPage::PictureBrush(const FString& Path)
{
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Path))
	{
		return Found->Get();
	}
	const TSharedPtr<FScctHubPads> Shared = Pads.Pin();
	UTexture2D* Texture = Shared.IsValid() && !Path.IsEmpty() ? Shared->GetPicture(Path) : nullptr;
	if (Texture == nullptr)
	{
		return nullptr;
	}
	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(MapTileWidth, MapPictureHeight);
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brushes.Add(Path, Brush);
	return Brush.Get();
}

void SScctHubLaptopPage::RebuildMaps()
{
	const TSharedPtr<FScctHubPads> Shared = Pads.Pin();
	const FScctHubPadsApi* Api = Shared.IsValid() ? Shared->GetApi() : nullptr;
	if (Api == nullptr || !MapBox.IsValid())
	{
		return;
	}
	TArray<FScctHubLaptopMap> Maps;
	Maps.SetNumZeroed(MaxMaps);
	Maps.SetNum(FMath::Min(Api->GetLaptopMaps(Maps.GetData(), Maps.Num()), MaxMaps));
	FString Key;
	for (const FScctHubLaptopMap& Map : Maps)
	{
		Key += FString::Printf(TEXT("%s:%d;"), Map.Id, Map.Flags);
	}
	if (Key == MapsKey)
	{
		return;
	}
	MapsKey = Key;

	MapBox->ClearChildren();
	MapsShown = 0;
	for (const FScctHubLaptopMap& Map : Maps)
	{
		const FString Id = Fixed(Map.Id, UE_ARRAY_COUNT(Map.Id));
		const bool bSelected = (Map.Flags & EScctHubPadsFlags::Selected) != 0;
		if ((MapGroups.IsValidIndex(MapGroupIndex) && !MapGroups[MapGroupIndex].Holds(Id))
			|| !ScctMapBrowser::Matches(MapSearch, Id, Fixed(Map.Name, UE_ARRAY_COUNT(Map.Name)), MapDetails.Find(Id.ToLower())))
		{
			continue;
		}
		++MapsShown;
		// The match is played in this game's imported level of the map, or the runtime level built from its render
		// files; without either the online visit refuses it.
		const bool bDrawable = UScctOnlineSubsystem::IsPlayable(Id);
		const FSlateBrush* Brush = PictureBrush(Fixed(Map.Picture, UE_ARRAY_COUNT(Map.Picture)));
		const FLinearColor Edge = bSelected ? LaptopUi::Accent() : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f);
		MapBox->AddSlot()
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.OnClicked_Lambda([this, Id]
			{
				if (const TSharedPtr<FScctHubPads> Pinned = Pads.Pin(); Pinned.IsValid() && Pinned->GetApi() != nullptr)
				{
					Pinned->GetApi()->SetLaptopMap(*Id);
					Refresh(false);
				}
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Edge)
				.Padding(bSelected ? 3.0f : 1.0f)
				[
					SNew(SBox).WidthOverride(MapTileWidth)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox).HeightOverride(MapPictureHeight)
							[
								Brush != nullptr ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, bDrawable ? 1.0f : 0.35f)))
									: StaticCastSharedRef<SWidget>(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.08f, 0.9f)))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 3.0f)
						[
							LaptopUi::Text(LaptopUi::ToText(Fixed(Map.Name, UE_ARRAY_COUNT(Map.Name))), 10.5f, FSlateColor(bSelected ? LaptopUi::Accent() : ScctHudStyle::Text()),
								bSelected ? ScctHudStyle::EWeight::Bold : ScctHudStyle::EWeight::Regular)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 4.0f, 3.0f)
						[
							LaptopUi::Text(LaptopUi::ToText(bDrawable || (Map.Flags & EScctHubPadsFlags::Downloadable) != 0 ? Fixed(Map.Modes, UE_ARRAY_COUNT(Map.Modes)) : FString(TEXT("not imported here"))), 9.0f,
								FSlateColor(ScctHudStyle::TextFaint()))
						]
					]
				]
			]
		];
	}
}
