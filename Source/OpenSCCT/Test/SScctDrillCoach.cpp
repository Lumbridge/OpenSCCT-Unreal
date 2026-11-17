#include "Test/SScctDrillCoach.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudStyle.h"
#include "Menu/ScctMenuStyle.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

namespace
{
	/** Where the panel sits: the Godot coach's corner, below the HUD's map and objectives box. */
	constexpr float PanelLeft = 40.0f;
	constexpr float PanelTop = 170.0f;
	constexpr float PanelWidth = 420.0f;

	/** The return portal's green (the Godot portal's albedo, sRGB). */
	FLinearColor PortalColour(float Alpha)
	{
		return FLinearColor::FromSRGBColor(FColor(46, 230, 102)).CopyWithNewOpacity(Alpha);
	}
}

void SScctDrillCoach::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	check(Model.IsValid());
	// The picture under the coach stays the game's: only the buttons are ever hit.
	SetVisibility(EVisibility::SelfHitTestInvisible);

	TSharedPtr<FScctDrillCoachModel> Shared = Model;
	const auto Click = [Shared](TFunction<void()> FScctDrillCoachModel::* Handler)
	{
		return FOnClicked::CreateLambda([Shared, Handler]()
		{
			if ((Shared.Get()->*Handler))
			{
				(Shared.Get()->*Handler)();
			}
			return FReply::Handled();
		});
	};
	const TAttribute<EVisibility> ButtonVisibility = TAttribute<EVisibility>::CreateLambda([Shared]()
	{
		return Shared->bButtonsLive ? EVisibility::Visible : EVisibility::HitTestInvisible;
	});
	const TAttribute<EVisibility> GrabOnly = TAttribute<EVisibility>::CreateLambda([Shared]()
	{
		return Shared->bSpyGrab ? (Shared->bButtonsLive ? EVisibility::Visible : EVisibility::HitTestInvisible) : EVisibility::Collapsed;
	});
	const auto Label = [Shared](FString FScctDrillCoachModel::* Field)
	{
		return TAttribute<FText>::CreateLambda([Shared, Field]() { return FText::FromString(Shared.Get()->*Field); });
	};

	TSharedRef<SButton> Retry = ScctMenuStyle::Button(Label(&FScctDrillCoachModel::RetryLabel), Click(&FScctDrillCoachModel::OnRetry));
	Retry->SetVisibility(ButtonVisibility);
	TSharedRef<SButton> Moving = ScctMenuStyle::Button(Label(&FScctDrillCoachModel::MovingLabel), Click(&FScctDrillCoachModel::OnMoving));
	Moving->SetVisibility(GrabOnly);
	TSharedRef<SButton> GrabArea = ScctMenuStyle::Button(Label(&FScctDrillCoachModel::GrabAreaLabel), Click(&FScctDrillCoachModel::OnGrabArea));
	GrabArea->SetVisibility(GrabOnly);

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			// The drill's name in the coach's green.
			ScctMenuStyle::Text(Label(&FScctDrillCoachModel::Title), 13.0f, FSlateColor(ScctHudStyle::Good()), ScctHudStyle::EWeight::Bold)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			ScctMenuStyle::Text(Label(&FScctDrillCoachModel::Feedback), 15.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			ScctMenuStyle::Text(Label(&FScctDrillCoachModel::Results), 14.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			ScctMenuStyle::Text(FText::FromString(TEXT("Use your normal bindings. Escape opens the menu and frees the mouse for these buttons; scct.DrillAction works from the console.")),
				12.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f) [ Retry ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f) [ Moving ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f) [ GrabArea ];

	ChildSlot
	[
		SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(PanelLeft, PanelTop, 0.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(PanelWidth).Visibility(EVisibility::SelfHitTestInvisible)
			[
				ScctMenuStyle::Glass(Column, FMargin(16.0f, 12.0f), 0.8f)
			]
		]
	];
}

void SScctDrillCoach::PaintPortal(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32& LayerId) const
{
	if (!Model->bPortal)
	{
		return;
	}

	// The ring on the floor, each piece left out when an end of it is behind the camera.
	for (int32 Index = 0; Index + 1 < FScctDrillCoachModel::RingPoints; ++Index)
	{
		if (Model->bRingInFront[Index] && Model->bRingInFront[Index + 1])
		{
			TArray<FVector2f> Points = { Model->Ring[Index], Model->Ring[Index + 1] };
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, PortalColour(0.95f), true, 4.0f);
		}
	}
	++LayerId;

	if (Model->bLabel)
	{
		const FSlateFontInfo Font = ScctHudStyle::Font(ScctHudStyle::EWeight::Bold, 13.0f, 80);
		const FString Text = TEXT("RETURN TO LEDGE - walk through");
		const FVector2f Size(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
		const FVector2f TopLeft = Model->Label - FVector2f(Size.X * 0.5f, Size.Y);
		FSlateDrawElement::MakeText(OutDrawElements, ++LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft + FVector2f(1.0f, 1.0f))),
			Text, Font, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
		FSlateDrawElement::MakeText(OutDrawElements, ++LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			Text, Font, ESlateDrawEffect::None, FLinearColor::FromSRGBColor(FColor(166, 255, 191)));
	}
}

int32 SScctDrillCoach::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// The portal is part of the scene, so it goes under the panel.
	PaintPortal(AllottedGeometry, OutDrawElements, LayerId);
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}
