#include "Menu/SScctBrightnessPage.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuStyle.h"
#include "OpenSCCT.h"
#include "Rendering/DrawElements.h"
#include "Settings/ScctSettings.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	const FName BrightnessKey(TEXT("Display.Brightness"));

	/** Above the menus and the match's own widgets. */
	constexpr int32 PageZOrder = 10000;

	/** The display gamma the 3D picture is drawn with at a brightness of 1 (UEngine's default). */
	constexpr float NeutralGamma = 2.2f;

	TWeakPtr<SScctBrightnessPage> OpenPage;

	/**
	 * The levels of the original picture (GUIContent calibrage, as Godot's BrightnessPattern decoded them): each
	 * square filled at 4 of 255 and framed at 12; the check at 13, the middle cross at 5, the right cross at 2.
	 */
	struct FSquareLevels { uint8 Fill; uint8 Frame; uint8 Mark; };
	constexpr FSquareLevels Levels[3] = { { 4, 12, 13 }, { 4, 12, 5 }, { 4, 12, 2 } };

	/**
	 * An sRGB level as the game's brightness would show it: the tonemapper raises the linear colour to 2.2 / gamma
	 * before it is written to the screen (PostProcessCombineLUTs, InverseGamma.y), so the pattern goes through the
	 * same curve. Slate colours are linear and written as sRGB, like the picture.
	 */
	FLinearColor Grey(uint8 Level)
	{
		const float Gamma = GEngine != nullptr && GEngine->DisplayGamma > 0.1f ? GEngine->DisplayGamma : NeutralGamma;
		const float Linear = FLinearColor::FromSRGBColor(FColor(Level, Level, Level)).R;
		const float Shown = FMath::Pow(FMath::Max(Linear, 0.0f), NeutralGamma / Gamma);
		return FLinearColor(Shown, Shown, Shown, 1.0f);
	}

	/** The three squares, drawn each frame at the brightness of the moment. */
	class SBrightnessPattern : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBrightnessPattern) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) {}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1000.0f, 300.0f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Out,
			int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
			const FVector2D Size = Geometry.GetLocalSize();
			// As the original lays them out: three squares as tall as fits, equal gaps either side and between.
			const float Side = FMath::Min(Size.Y, Size.X / 3.6f);
			const float Gap = (Size.X - Side * 3.0f) / 4.0f;
			const float Frame = FMath::Max(2.0f, Side * 0.02f);
			const float Stroke = FMath::Max(4.0f, Side * 0.12f);
			const float Half = Side * 0.28f;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FVector2D SwatchCorner(Gap + Index * (Side + Gap), (Size.Y - Side) * 0.5f);
				const FVector2D Centre = SwatchCorner + FVector2D(Side * 0.5f);
				FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(FVector2f(Side, Side), FSlateLayoutTransform(FVector2f(SwatchCorner))),
					White, ESlateDrawEffect::None, Grey(Levels[Index].Frame));
				FSlateDrawElement::MakeBox(Out, LayerId + 1,
					Geometry.ToPaintGeometry(FVector2f(Side - Frame * 2.0f, Side - Frame * 2.0f), FSlateLayoutTransform(FVector2f(SwatchCorner + FVector2D(Frame)))),
					White, ESlateDrawEffect::None, Grey(Levels[Index].Fill));

				const FLinearColor Mark = Grey(Levels[Index].Mark);
				if (Index == 0)
				{
					const TArray<FVector2D> Check = { Centre + FVector2D(-Half, 0.0f), Centre + FVector2D(-Half * 0.3f, Half * 0.8f), Centre + FVector2D(Half, -Half) };
					FSlateDrawElement::MakeLines(Out, LayerId + 2, Geometry.ToPaintGeometry(), Check, ESlateDrawEffect::None, Mark, true, Stroke);
				}
				else
				{
					const TArray<FVector2D> One = { Centre + FVector2D(-Half, -Half), Centre + FVector2D(Half, Half) };
					const TArray<FVector2D> Two = { Centre + FVector2D(-Half, Half), Centre + FVector2D(Half, -Half) };
					FSlateDrawElement::MakeLines(Out, LayerId + 2, Geometry.ToPaintGeometry(), One, ESlateDrawEffect::None, Mark, true, Stroke);
					FSlateDrawElement::MakeLines(Out, LayerId + 2, Geometry.ToPaintGeometry(), Two, ESlateDrawEffect::None, Mark, true, Stroke);
				}
			}
			return LayerId + 2;
		}
	};
}

void SScctBrightnessPage::Open()
{
	if (OpenPage.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}
	const TSharedRef<SScctBrightnessPage> Page = SNew(SScctBrightnessPage);
	if (FSlateApplication::IsInitialized())
	{
		Page->FocusBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}
	GEngine->GameViewport->AddViewportWidgetContent(Page, PageZOrder);
	OpenPage = Page;
	UE_LOG(LogOpenSCCT, Log, TEXT("Brightness: pattern shown (brightness %s, display gamma %.2f)"),
		*FScctSettings::GetString(BrightnessKey), GEngine->DisplayGamma);
}

void SScctBrightnessPage::Construct(const FArguments& InArgs)
{
	static const FSlateColorBrush Black(FLinearColor::Black);
	const FScctSettingDesc* Desc = FScctSettings::Find(BrightnessKey);

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 22.0f)
		[
			Ui::Title(Ui::ToText(TEXT("BRIGHTNESS")), 30.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SBox).WidthOverride(760.0f)
			[
				Ui::Text(Ui::ToText(TEXT("Turn the brightness until you can just see the check in the left square. The crosses in the other two squares should stay black.")),
					16.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 30.0f)
		[
			SNew(SBox).WidthOverride(760.0f)
			[
				Ui::Text(Ui::ToText(TEXT("The squares are drawn with the game's brightness, so they change with the slider as the picture does. Your screen's own brightness counts too: set it first if the check never shows.")),
					12.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 36.0f)
		[
			SNew(SBox).WidthOverride(1000.0f).HeightOverride(300.0f)[ SNew(SBrightnessPattern) ]
		];

	if (Desc != nullptr)
	{
		const float StepSize = Desc->Step > 0.f ? Desc->Step : (Desc->Max - Desc->Min) / 100.f;
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 30.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)[ Ui::Label(TEXT("Brightness")) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Ui::Button(Ui::ToText(TEXT("<")), FOnClicked::CreateLambda([this] { Step(-1); return FReply::Handled(); }))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(360.0f)
				[
					SNew(SSlider)
					.MinValue(Desc->Min)
					.MaxValue(Desc->Max)
					.StepSize(StepSize)
					.MouseUsesStep(true)
					.IsFocusable(false)
					.SliderBarColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.25f))
					.SliderHandleColor(Ui::Accent())
					.Value(TAttribute<float>::CreateLambda([] { return FScctSettings::GetFloat(BrightnessKey); }))
					.OnValueChanged(FOnFloatValueChanged::CreateLambda([](float Value) { FScctSettings::SetFloat(BrightnessKey, Value); }))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Ui::Button(Ui::ToText(TEXT(">")), FOnClicked::CreateLambda([this] { Step(1); return FReply::Handled(); }))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(64.0f)
				[
					Ui::Text(TAttribute<FText>::CreateLambda([]
					{
						const FScctSettingDesc* Found = FScctSettings::Find(BrightnessKey);
						const float Value = FScctSettings::GetFloat(BrightnessKey);
						return Ui::ToText(Found != nullptr && Found->FormatValue ? Found->FormatValue(Value) : FString::Printf(TEXT("%.2f"), Value));
					}), 14.0f)
				]
			]
		];
	}

	Box->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateLambda([this] { Close(); return FReply::Handled(); }), true, Ui::Accent(), 220.0f)
	];

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SImage).Image(&Black) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ Box ]
	];
}

void SScctBrightnessPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// The keyboard can only be given to a widget that is on screen, which the page is by its first tick.
	if (!bFocusTaken && FSlateApplication::IsInitialized())
	{
		bFocusTaken = true;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

FReply SScctBrightnessPage::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Right)
	{
		Close();
		return FReply::Handled();
	}
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)
	{
		Step(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)
	{
		Step(1);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SScctBrightnessPage::Step(int32 By)
{
	if (const FScctSettingDesc* Desc = FScctSettings::Find(BrightnessKey))
	{
		const float StepSize = Desc->Step > 0.f ? Desc->Step : (Desc->Max - Desc->Min) / 20.f;
		FScctSettings::SetFloat(BrightnessKey, FScctSettings::GetFloat(BrightnessKey) + By * StepSize);
	}
}

void SScctBrightnessPage::Close()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	// The brightness was applied as it moved; leaving the page keeps it.
	FScctSettings::Save();
	UE_LOG(LogOpenSCCT, Log, TEXT("Brightness: pattern closed, brightness %s kept (display gamma %.2f)"),
		*FScctSettings::GetString(BrightnessKey), GEngine != nullptr ? GEngine->DisplayGamma : 0.0f);

	const TSharedRef<SScctBrightnessPage> Self = SharedThis(this);
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Self);
	}
	if (const TSharedPtr<SWidget> Before = FocusBefore.Pin(); Before.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Before, EFocusCause::SetDirectly);
	}
}
