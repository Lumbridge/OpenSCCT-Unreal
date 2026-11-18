#include "Menu/SScctHubLaptopPage.h"
#include "Menu/ScctMenuNav.h"

#include "Engine/Texture2D.h"
#include "Hub/ScctHubPads.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/PackageName.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
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
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ LaptopUi::Label(TEXT("Map")) ]
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
	FString Key;
	for (const FScctHubLaptopRow& Row : Rows)
	{
		Key += FString::Printf(TEXT("%s=%s:%d;"), Row.Key, Row.Value, Row.Enabled);
	}
	if (Key == RowsKey)
	{
		return;
	}
	RowsKey = Key;

	RowBox->ClearChildren();
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FScctHubLaptopRow& Row = Rows[Index];
		const bool bEnabled = Row.Enabled != 0;
		const FLinearColor Colour = bEnabled ? ScctHudStyle::Text() : ScctHudStyle::TextFaint();
		auto Step = [this, Index](int32 Delta)
		{
			return FOnClicked::CreateLambda([this, Index, Delta]
			{
				if (const TSharedPtr<FScctHubPads> Pinned = Pads.Pin(); Pinned.IsValid() && Pinned->GetApi() != nullptr)
				{
					Pinned->GetApi()->StepLaptopRow(Index, Delta);
					Refresh(false);
				}
				return FReply::Handled();
			});
		};
		RowBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				LaptopUi::Text(LaptopUi::ToText(Fixed(Row.Label, UE_ARRAY_COUNT(Row.Label))), 12.5f, FSlateColor(Colour))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).Visibility(bEnabled ? EVisibility::Visible : EVisibility::Hidden)[ LaptopUi::Button(LaptopUi::ToText(TEXT("<")), Step(-1), false, LaptopUi::Accent(), 34.0f) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(150.0f).HAlign(HAlign_Center)
				[
					LaptopUi::Text(LaptopUi::ToText(Fixed(Row.Value, UE_ARRAY_COUNT(Row.Value))), 12.5f, FSlateColor(bEnabled ? LaptopUi::Accent() : ScctHudStyle::TextFaint()),
						ScctHudStyle::EWeight::Bold)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).Visibility(bEnabled ? EVisibility::Visible : EVisibility::Hidden)[ LaptopUi::Button(LaptopUi::ToText(TEXT(">")), Step(1), false, LaptopUi::Accent(), 34.0f) ]
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
	for (const FScctHubLaptopMap& Map : Maps)
	{
		const FString Id = Fixed(Map.Id, UE_ARRAY_COUNT(Map.Id));
		const bool bSelected = (Map.Flags & EScctHubPadsFlags::Selected) != 0;
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
