#include "Hub/SScctHubLoadingScreen.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Menu/ScctMenuStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** Seconds the fade out takes (HubLoadingScreen.FadeOut's tween). */
	constexpr float FadeSeconds = 0.35f;
}

void SScctHubLoadingScreen::Construct(const FArguments& InArgs)
{
	namespace Ui = ScctMenuStyle;
	static const FSlateColorBrush Solid(FLinearColor::White);
	const TAttribute<FText> HubName = InArgs._HubName;
	const FLinearColor SideColour = ScctHudStyle::Accent(InArgs._bMerc ? 1 : 0);

	if (InArgs._Picture)
	{
		PictureBrush.SetResourceObject(InArgs._Picture);
		PictureBrush.ImageSize = FVector2D(InArgs._Picture->GetSizeX(), InArgs._Picture->GetSizeY());
	}

	ChildSlot
	[
		SNew(SOverlay)
		// Opaque: what is behind it is a level that is not ready to be seen.
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(&Solid)
			.ColorAndOpacity(FLinearColor(0.004f, 0.008f, 0.014f, 1.0f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(760.0f)
			[
				Ui::Glass(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						Ui::Label(TEXT("Loading..."))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(TAttribute<FText>::CreateLambda([HubName]
						{
							const FString Name = HubName.Get().ToString();
							return Ui::ToText(FString(TEXT("ENTERING ")) + (Name.IsEmpty() ? FString(TEXT("THE HUB")) : Name.ToUpper()));
						}))
						.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 30.0f, 260))
						.ColorAndOpacity(ScctHudStyle::Text())
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 26.0f)
					[
						Ui::Label(InArgs._bMerc ? TEXT("You arrive as a mercenary") : TEXT("You arrive as a spy"), SideColour)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SBox).WidthOverride(320.f).HeightOverride(180.f)
						.Visibility(InArgs._Picture ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
						[ SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[ SNew(SImage).Image(&PictureBrush) ] ]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(12.f)
					[
						SNew(STextBlock).Text(InArgs._Tip).AutoWrapText(true)
						.Visibility(InArgs._Tip.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 18.0f)
					[
						SNew(SCircularThrobber)
						.Radius(14.0f)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._Status)
						.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 12.5f))
						.ColorAndOpacity(Ui::Accent())
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
					])
			]
		]
	];
}

void SScctHubLoadingScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bFading && Opacity > 0.0f)
	{
		Opacity = FMath::Max(0.0f, Opacity - InDeltaTime / FadeSeconds);
		SetRenderOpacity(Opacity);
	}
}

void SScctHubLoadingScreen::FadeOut()
{
	if (!bFading)
	{
		bFading = true;
		SetVisibility(EVisibility::HitTestInvisible);
	}
}
