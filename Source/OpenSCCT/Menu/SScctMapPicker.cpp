#include "Menu/SScctMapPicker.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Math/RandomStream.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
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
	TileWidth = InArgs._TileWidth;

	// The filter: All, then every mode some map offers, in the menu's order.
	FilterRow = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
	TArray<FString> Filters = { FString() };
	for (const TCHAR* Mode : ModeOrder)
	{
		if (Maps.ContainsByPredicate([Mode](const FScctHostMap& Map) { return Offers(Map, Mode); }))
		{
			Filters.Add(Mode);
		}
	}
	for (const FString& Filter : Filters)
	{
		FilterRow->AddSlot()
		[
			Ui::Button(Ui::ToText(Filter.IsEmpty() ? TEXT("ALL MAPS") : ScctHostSetup::ModeLabel(Filter).ToUpper()),
				FOnClicked::CreateLambda([this, Filter] { SetModeFilter(Filter); return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this, Filter] { return ModeFilter == Filter; }))
		];
	}
	FilterRow->AddSlot()
	[
		Ui::Button(Ui::ToText(TEXT("RANDOM MAP")), FOnClicked::CreateLambda([this] { PickRandom(); return FReply::Handled(); }), false, ScctHudStyle::Good())
	];

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ FilterRow.ToSharedRef() ]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()[ SAssignNew(Tiles, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f)) ]
		]
	];
	Refill();
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: map picker shows %d map(s), %d with a picture"), Maps.Num(), Pictures.Num());
}

void SScctMapPicker::SetModeFilter(const FString& ModeKey)
{
	if (ModeFilter != ModeKey)
	{
		ModeFilter = ModeKey;
		Refill();
	}
}

FString SScctMapPicker::PickRandom()
{
	const FString Current = Selected.Get();
	TArray<FString> Candidates;
	for (const FScctHostMap& Map : Maps)
	{
		if (Offers(Map, ModeFilter) && !Map.Id.Equals(Current, ESearchCase::IgnoreCase))
		{
			Candidates.Add(Map.Id);
		}
	}
	if (Candidates.IsEmpty())
	{
		return FString();
	}

	static FRandomStream Stream(FPlatformTime::Cycles());
	const FString Drawn = Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: random map drew %s of %d"), *Drawn, Candidates.Num());
	OnPicked.ExecuteIfBound(Drawn);
	return Drawn;
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

void SScctMapPicker::Refill()
{
	if (!Tiles.IsValid())
	{
		return;
	}

	Tiles->ClearChildren();
	const float PictureHeight = FMath::RoundToFloat(TileWidth * 0.75f);
	for (const FScctHostMap& Map : Maps)
	{
		if (!Offers(Map, ModeFilter))
		{
			continue;
		}

		const FString Id = Map.Id;
		const FSlateBrush* Brush = PictureOf(Map);
		const TSharedRef<SWidget> Picture = Brush != nullptr
			? StaticCastSharedRef<SWidget>(SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[ SNew(SImage).Image(Brush) ])
			// No picture: the map's code where it would be.
			: StaticCastSharedRef<SWidget>(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Ui::Label(Map.Id, ScctHudStyle::TextFaint()) ]);

		const TSharedRef<SButton> Tile = SNew(SButton)
			.ButtonStyle(&TileStyle())
			.IsFocusable(false)
			.ToolTipText(Ui::ToText(FString::Printf(TEXT("%s (%s)"), *Map.Name, *Map.Id)))
			.OnClicked(FOnClicked::CreateLambda([this, Id] { OnPicked.ExecuteIfBound(Id); return FReply::Handled(); }))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).WidthOverride(TileWidth).HeightOverride(PictureHeight).Clipping(EWidgetClipping::ClipToBounds)[ Picture ]
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
				]
			];

		// The selected map is lit in the accent, as a picked button is.
		const TWeakPtr<SButton> Weak = Tile;
		Tile->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this, Weak, Id]
		{
			const TSharedPtr<SButton> Pinned = Weak.Pin();
			const bool bHovered = Pinned.IsValid() && Pinned->IsHovered();
			if (Selected.Get().Equals(Id, ESearchCase::IgnoreCase))
			{
				return FSlateColor(ScctHudStyle::Fade(Ui::Accent(), bHovered ? 0.75f : 0.6f));
			}
			return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, bHovered ? 0.16f : 0.06f));
		}));
		Tiles->AddSlot()[ Tile ];
	}
}
