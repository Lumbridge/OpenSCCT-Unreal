#include "Menu/ScctMenuKit.h"

#include "Menu/ScctMenuNav.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Fonts/FontMeasure.h"
#include "Input/HittestGrid.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	using ScctHudStyle::EWeight;

	FText DescriptionTitle;
	FText DescriptionBody;
	bool bPadActive = false;
	TArray<IScctDriven*> DrivenWidgets;

	const FSlateBrush* WhiteBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	/** A white rounded box; the draw call's tint is its colour. */
	const FSlateBrush* FillBrush(float Radius = ScctUi::Corner)
	{
		static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Brushes;
		TSharedPtr<FSlateRoundedBoxBrush>& Brush = Brushes.FindOrAdd(FMath::RoundToInt32(Radius));
		if (!Brush.IsValid())
		{
			Brush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, Radius);
		}
		return Brush.Get();
	}

	/** The outlines handed out by OutlineBrush, with their radius and width. */
	TMap<const FSlateBrush*, FVector2f>& OutlineShapes()
	{
		static TMap<const FSlateBrush*, FVector2f> Shapes;
		return Shapes;
	}

	/**
	 * A rounded outline with nothing inside: FPainter::Box draws it as a line of the draw call's colour. (A rounded
	 * box brush fills with the draw call's tint and takes its outline's colour from the brush, so drawn as a plain
	 * box it would be a solid plate of the colour with a white edge.)
	 */
	const FSlateBrush* OutlineBrush(float Radius = ScctUi::Corner, float Width = 1.0f)
	{
		static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Brushes;
		TSharedPtr<FSlateRoundedBoxBrush>& Brush = Brushes.FindOrAdd(FMath::RoundToInt32(Radius) * 16 + FMath::RoundToInt32(Width));
		if (!Brush.IsValid())
		{
			Brush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::Transparent, Radius, FLinearColor::White, Width);
			OutlineShapes().Add(Brush.Get(), FVector2f(Radius, Width));
		}
		return Brush.Get();
	}

	FVector2D Measure(const FString& String, const FSlateFontInfo& Font)
	{
		return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, Font));
	}

	/** Draws into one widget's geometry; every colour is faded by the widget's own opacity. */
	struct FPainter
	{
		FSlateWindowElementList& Out;
		const FGeometry& Geometry;
		int32 Layer;
		FLinearColor Tint;

		FLinearColor Mix(const FLinearColor& Colour) const { return Colour * Tint; }

		void Box(const FVector2D& Position, const FVector2D& Size, const FSlateBrush* Brush, const FLinearColor& Colour, int32 Above = 0) const
		{
			if (Colour.A > 0.002f && Size.X > 0.0 && Size.Y > 0.0)
			{
				const FPaintGeometry Paint = Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position)));
				if (const FVector2f* Shape = OutlineShapes().Find(Brush))
				{
					// The outline in the colour; the fill (the element's tint) left clear. The element copies the
					// brush, so one on the stack is enough.
					const FSlateRoundedBoxBrush Line(FLinearColor::White, Shape->X, Mix(Colour), Shape->Y);
					FSlateDrawElement::MakeBox(Out, Layer + Above, Paint, &Line, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
				}
				else
				{
					FSlateDrawElement::MakeBox(Out, Layer + Above, Paint, Brush, ESlateDrawEffect::None, Mix(Colour));
				}
			}
		}

		void Text(const FVector2D& Position, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour, int32 Above = 1) const
		{
			FSlateDrawElement::MakeText(Out, Layer + Above, Geometry.ToPaintGeometry(FVector2f(Measure(String, Font)), FSlateLayoutTransform(FVector2f(Position))), String, Font, ESlateDrawEffect::None, Mix(Colour));
		}

		/** Text centred on a point. */
		void TextAt(const FVector2D& Centre, const FString& String, const FSlateFontInfo& Font, const FLinearColor& Colour, int32 Above = 1) const
		{
			Text(Centre - Measure(String, Font) * 0.5, String, Font, Colour, Above);
		}

		/** A fill that fades from Left to Right across the box. */
		void Gradient(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Left, const FLinearColor& Right, int32 Above = 0) const
		{
			TArray<FSlateGradientStop> Stops;
			Stops.Emplace(FVector2D::ZeroVector, Mix(Left));
			Stops.Emplace(Size, Mix(Right));
			FSlateDrawElement::MakeGradient(Out, Layer + Above, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position))), Stops, Orient_Vertical, ESlateDrawEffect::None);
		}
	};

	float FrameSeconds()
	{
		return FMath::Min(FSlateApplication::Get().GetDeltaTime(), 0.1f);
	}

	bool IsAccept(const FKeyEvent& Event)
	{
		return FSlateApplication::Get().GetNavigationActionFromKey(Event) == EUINavigationAction::Accept;
	}

	const FButtonStyle& BareButton()
	{
		static const FButtonStyle Style = FButtonStyle()
			.SetNormal(FSlateNoResource())
			.SetHovered(FSlateNoResource())
			.SetPressed(FSlateNoResource())
			.SetDisabled(FSlateNoResource())
			.SetNormalPadding(FMargin(0.0f))
			.SetPressedPadding(FMargin(0.0f));
		return Style;
	}

	const FEditableTextBoxStyle& TextBoxStyle()
	{
		static const FEditableTextBoxStyle Style = []
		{
			FEditableTextBoxStyle Made = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
			const FLinearColor Fill(0.0f, 0.0f, 0.0f, 0.4f);
			Made.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Fill, ScctUi::Corner, FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), 1.0f));
			Made.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Fill, ScctUi::Corner, FLinearColor(1.0f, 1.0f, 1.0f, 0.34f), 1.0f));
			Made.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Fill, ScctUi::Corner, ScctUi::Accent(), 1.5f));
			Made.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Fill, ScctUi::Corner));
			Made.SetBackgroundColor(FLinearColor::White);
			Made.SetForegroundColor(ScctHudStyle::Text());
			Made.SetFocusedForegroundColor(ScctHudStyle::Text());
			Made.SetPadding(FMargin(14.0f, 10.0f));
			return Made;
		}();
		return Style;
	}
}

// ---- the namespace's pieces -------------------------------------------------------------------------------------

namespace
{
	/** Every control under Widget that can take the focus and is on screen (collapsed and hidden parts are left out). */
	void GatherFocusable(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SWidget>>& Out)
	{
		if (!Widget->GetVisibility().IsVisible() || !Widget->IsEnabled())
		{
			return;
		}
		if (Widget->SupportsKeyboardFocus())
		{
			Out.Add(Widget);
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			GatherFocusable(Children->GetChildAt(Index), Out);
		}
	}

	/**
	 * The nearest control that way by where the controls were last drawn: what the hit test grid's search finds on
	 * a screen, for when it finds nothing (an off-screen run draws the interface without filling the window's grid).
	 */
	TSharedPtr<SWidget> NearestThatWay(const TSharedRef<SWidget>& Root, const TSharedRef<SWidget>& From, EUINavigation Type)
	{
		const FSlateRect Start = From->GetPaintSpaceGeometry().GetLayoutBoundingRect();
		if (Start.GetSize().IsNearlyZero())
		{
			return nullptr;
		}
		TArray<TSharedRef<SWidget>> Candidates;
		GatherFocusable(Root, Candidates);
		TSharedPtr<SWidget> Best;
		double BestScore = TNumericLimits<double>::Max();
		const bool bVertical = Type == EUINavigation::Up || Type == EUINavigation::Down;
		for (const TSharedRef<SWidget>& Candidate : Candidates)
		{
			if (Candidate == From)
			{
				continue;
			}
			const FSlateRect Rect = Candidate->GetPaintSpaceGeometry().GetLayoutBoundingRect();
			if (Rect.GetSize().IsNearlyZero())
			{
				continue;
			}
			// How far it lies that way (it must start beyond the control's edge), and how far off to the side.
			double Ahead = 0.0;
			switch (Type)
			{
			case EUINavigation::Up: Ahead = Start.Top - Rect.Bottom; break;
			case EUINavigation::Down: Ahead = Rect.Top - Start.Bottom; break;
			case EUINavigation::Left: Ahead = Start.Left - Rect.Right; break;
			default: Ahead = Rect.Left - Start.Right; break;
			}
			if (Ahead < -2.0)
			{
				continue;
			}
			const double Aside = bVertical
				? FMath::Max(0.0, FMath::Max(Rect.Left - Start.Right, Start.Left - Rect.Right))
				: FMath::Max(0.0, FMath::Max(Rect.Top - Start.Bottom, Start.Top - Rect.Bottom));
			const double Score = FMath::Max(Ahead, 0.0) + 3.0 * Aside;
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Candidate;
			}
		}
		return Best;
	}

	/** ScctUi::Navigate's search, from the control that has the focus. */
	TSharedPtr<SWidget> NavigateFromFocus(EUINavigation Type)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		const TSharedPtr<SWidget> Focused = Slate.GetKeyboardFocusedWidget();
		FWidgetPath Path;
		if (!Focused.IsValid() || !Slate.FindPathToWidget(Focused.ToSharedRef(), Path) || Path.Widgets.Num() == 0)
		{
			return nullptr;
		}
		const FArrangedWidget& Root = Path.Widgets[0];
		const TSharedPtr<SWidget> Found = Path.GetWindow()->GetHittestGrid().FindNextFocusableWidget(Path.Widgets.Last(), Type, FNavigationReply::Escape(), Root, 0);
		if (Found.IsValid())
		{
			return Found;
		}
		if (const TSharedPtr<SWidget> Near = NearestThatWay(Root.Widget, Focused.ToSharedRef(), Type))
		{
			return Near;
		}
		// Nothing at all that way: the page's order, so the focus is never stuck.
		const EUINavigation Order = Type == EUINavigation::Down || Type == EUINavigation::Right ? EUINavigation::Next : EUINavigation::Previous;
		const FWidgetPath Next = FWeakWidgetPath(Path).ToNextFocusedPath(Order, FNavigationReply::Escape(), Root);
		return Next.IsValid() && Next.Widgets.Num() > 0 && Next.Widgets.Last().Widget != Focused ? TSharedPtr<SWidget>(Next.Widgets.Last().Widget) : nullptr;
	}
}

namespace ScctUi
{
	FNavigationReply Navigate(const FNavigationEvent& Event)
	{
		switch (Event.GetNavigationType())
		{
		case EUINavigation::Up:
		case EUINavigation::Down:
		case EUINavigation::Left:
		case EUINavigation::Right:
			return FNavigationReply::Custom(FNavigationDelegate::CreateStatic(&NavigateFromFocus));
		default:
			return FNavigationReply::Escape();
		}
	}

	FSlateFontInfo Display(float Size) { return ScctHudStyle::Font(EWeight::Light, Size, 120); }
	FSlateFontInfo H1() { return ScctHudStyle::Font(EWeight::Light, 30.0f, 180); }
	FSlateFontInfo H2() { return ScctHudStyle::Font(EWeight::Regular, 19.0f, 120); }
	FSlateFontInfo Body(bool bBold) { return ScctHudStyle::Font(bBold ? EWeight::Bold : EWeight::Regular, 15.0f); }
	FSlateFontInfo Small() { return ScctHudStyle::Font(EWeight::Regular, 13.0f); }
	FSlateFontInfo Caps(float Size) { return ScctHudStyle::Font(EWeight::Bold, Size, 150); }

	FLinearColor Accent() { return ScctHudStyle::Accent(0); }
	FLinearColor Ink(float Alpha) { return FLinearColor(0.004f, 0.008f, 0.013f, Alpha); }
	FLinearColor White(float Alpha) { return FLinearColor(1.0f, 1.0f, 1.0f, Alpha); }

	void Play(EScctUiSound Sound)
	{
		if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent())
		{
			Menu->PlaySound(Sound);
		}
	}

	void Describe(const FText& Title, const FText& InBody)
	{
		DescriptionTitle = Title;
		DescriptionBody = InBody;
	}

	const FText& GetDescriptionTitle() { return DescriptionTitle; }
	const FText& GetDescriptionBody() { return DescriptionBody; }

	bool IsPadActive() { return bPadActive; }

	void NoteInput(const FKey& Key)
	{
		bPadActive = Key.IsGamepadKey();
	}

	int32 AdjustDirection(const FKey& Key)
	{
		// The left stick is not here: it reaches a row as Slate's analog navigation (OnNavigation), and its key copies
		// (Gamepad_LeftStick_Left) would step the value a second time.
		if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)
		{
			return -1;
		}
		if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)
		{
			return 1;
		}
		return 0;
	}

	TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& Content, const FMargin& Padding, float Tint)
	{
		static const FSlateRoundedBoxBrush Edge(FLinearColor::White, PanelCorner, FLinearColor(1.0f, 1.0f, 1.0f, 0.11f), 1.0f);
		return SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBackgroundBlur)
				.BlurStrength(18.0f)
				.CornerRadius(FVector4(PanelCorner, PanelCorner, PanelCorner, PanelCorner))
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(&Edge)
				.BorderBackgroundColor(Ink(Tint))
				.Padding(Padding)
				[
					Content
				]
			];
	}

	TSharedRef<SWidget> Text(const TAttribute<FText>& InText, const FSlateFontInfo& Font, const TAttribute<FSlateColor>& Colour, bool bWrap)
	{
		return SNew(STextBlock).Text(InText).Font(Font).ColorAndOpacity(Colour).AutoWrapText(bWrap);
	}

	TSharedRef<SWidget> Label(const FString& InText, const FLinearColor& Colour, float Size)
	{
		return SNew(STextBlock).Text(FText::FromString(InText.ToUpper())).Font(Caps(Size)).ColorAndOpacity(Colour);
	}

	TSharedRef<SWidget> Rule(float Alpha)
	{
		return SNew(SBox).HeightOverride(1.0f)[ SNew(SImage).Image(WhiteBrush()).ColorAndOpacity(White(Alpha)) ];
	}

	TSharedRef<SWidget> Picture(const FSlateBrush* Brush, float Width, float Height)
	{
		static const FSlateRoundedBoxBrush Plate(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), Corner, FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);
		const TSharedRef<SBox> Box = SNew(SBox).Clipping(EWidgetClipping::ClipToBounds);
		if (Width > 0.0f) { Box->SetWidthOverride(Width); }
		if (Height > 0.0f) { Box->SetHeightOverride(Height); }
		if (Brush != nullptr)
		{
			Box->SetContent(SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[ SNew(SImage).Image(Brush) ]);
		}
		else
		{
			Box->SetContent(SNew(SImage).Image(&Plate));
		}
		return Box;
	}

	TSharedRef<SWidget> KeyHint(const FString& Key, const FString& PadKey, const FString& What)
	{
		static const FSlateRoundedBoxBrush Cap(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 3.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.32f), 1.0f);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(&Cap).Padding(FMargin(8.0f, 3.0f))
				[
					SNew(STextBlock)
					.Font(ScctHudStyle::Font(EWeight::Bold, 11.0f, 60))
					.ColorAndOpacity(ScctHudStyle::Text())
					.Text(TAttribute<FText>::CreateLambda([Key, PadKey] { return FText::FromString(IsPadActive() ? PadKey : Key); }))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 22.0f, 0.0f)
			[
				SNew(STextBlock).Font(Small()).ColorAndOpacity(ScctHudStyle::TextDim()).Text(FText::FromString(What))
			];
	}

	TSharedRef<SWidget> Section(const FString& Title)
	{
		return SNew(SBox).Padding(FMargin(4.0f, 20.0f, 0.0f, 8.0f))[ Label(Title, Accent(), 11.5f) ];
	}

	TSharedRef<SWidget> ScaleForSmallScreens(const TSharedRef<SWidget>& Content)
	{
		return SNew(SDPIScaler)
			.DPIScale(TAttribute<float>::CreateLambda([]
			{
				FVector2D Size(1920.0, 1080.0);
				if (GEngine != nullptr && GEngine->GameViewport != nullptr)
				{
					GEngine->GameViewport->GetViewportSize(Size);
				}
				return FMath::Clamp(1.0f + (1080.0f - static_cast<float>(Size.Y)) / 360.0f * 0.2f, 1.0f, 1.25f);
			}))
			[
				Content
			];
	}

	bool IsTyping()
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return Focused.IsValid() && (Focused->GetType() == FName(TEXT("SEditableText")) || Focused->GetType() == FName(TEXT("SMultiLineEditableText")));
	}

	TSharedRef<SEditableTextBox> TextBox(const FString& Hint, const FString& Initial, bool bPassword, FOnTextCommitted OnCommitted)
	{
		return SNew(SEditableTextBox)
			.Style(&TextBoxStyle())
			.Font(Body())
			.HintText(FText::FromString(Hint))
			.Text(FText::FromString(Initial))
			.IsPassword(bPassword)
			// Moving onto a field with the keys or the pad selects what is in it, so typing replaces it (an address
			// typed onto the last one ran the two together).
			.SelectAllTextWhenFocused(true)
			.OnTextCommitted(OnCommitted)
			.MinDesiredWidth(200.0f);
	}
}

// ---- unattended driving -----------------------------------------------------------------------------------------

void IScctDriven::Register()
{
	DrivenWidgets.Add(this);
}

void IScctDriven::Unregister()
{
	DrivenWidgets.Remove(this);
}

IScctDriven* IScctDriven::Find(const FString& Label)
{
	// The newest first: a dialog's button wins over the page's button of the same name under it.
	for (int32 Index = DrivenWidgets.Num() - 1; Index >= 0; --Index)
	{
		if (DrivenWidgets[Index]->GetDrivenLabel().Equals(Label, ESearchCase::IgnoreCase))
		{
			return DrivenWidgets[Index];
		}
	}
	for (int32 Index = DrivenWidgets.Num() - 1; Index >= 0; --Index)
	{
		if (DrivenWidgets[Index]->GetDrivenLabel().StartsWith(Label, ESearchCase::IgnoreCase))
		{
			return DrivenWidgets[Index];
		}
	}
	return nullptr;
}

IScctDriven* IScctDriven::FindByWidget(const SWidget* Widget)
{
	for (int32 Index = DrivenWidgets.Num() - 1; Widget != nullptr && Index >= 0; --Index)
	{
		if (&DrivenWidgets[Index]->GetDrivenWidget().Get() == Widget)
		{
			return DrivenWidgets[Index];
		}
	}
	return nullptr;
}

// ---- SScctButton --------------------------------------------------------------------------------------------------

void SScctButton::Construct(const FArguments& InArgs)
{
	Caption = InArgs._Text;
	Selected = InArgs._Selected;
	Look = InArgs._Look;
	Colour = Look == EScctButtonLook::Danger ? ScctHudStyle::Danger() : InArgs._Colour;
	DescriptionText = InArgs._Description;
	OnPressedDelegate = InArgs._OnPressed;

	const bool bMenu = Look == EScctButtonLook::Menu;
	const bool bSmall = Look == EScctButtonLook::Chip;
	const FSlateFontInfo Font = bMenu ? ScctHudStyle::Font(EWeight::Light, 25.0f, 90) : ScctUi::Caps(bSmall ? 12.0f : 13.0f);
	const FMargin Padding = bMenu ? FMargin(24.0f, InArgs._Compact ? 6.0f : 11.0f, 28.0f, InArgs._Compact ? 6.0f : 11.0f) : bSmall ? FMargin(14.0f, 8.0f) : Look == EScctButtonLook::Tab ? FMargin(20.0f, 12.0f) : FMargin(24.0f, 13.0f);

	// The caption's colour follows what the button is doing.
	const TAttribute<FSlateColor> TextColour = TAttribute<FSlateColor>::CreateLambda([this]
	{
		const float Alpha = IsEnabled() ? 1.0f : 0.35f;
		if (Look == EScctButtonLook::Primary)
		{
			return FSlateColor(ScctUi::Ink(Alpha));
		}
		const bool bQuiet = (Look == EScctButtonLook::Menu || Look == EScctButtonLook::Tab || Look == EScctButtonLook::Chip) && !Selected.Get() && Lit < 0.5f;
		return FSlateColor(ScctHudStyle::Fade(ScctHudStyle::Text(), Alpha * (bQuiet ? 0.66f : 1.0f)));
	});

	const TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(bMenu ? HAlign_Left : HAlign_Center)
		[
			SNew(STextBlock).Text(Caption).Font(Font).ColorAndOpacity(TextColour)
		];
	if (InArgs._SubText.IsSet())
	{
		Lines->AddSlot().AutoHeight().Padding(1.0f, 3.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(InArgs._SubText).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).Visibility(InArgs._SubTextVisibility)
		];
	}

	SButton::Construct(SButton::FArguments()
		.ButtonStyle(&BareButton())
		.ContentPadding(Padding)
		.IsFocusable(true)
		.HAlign(bMenu || InArgs._FillWidth ? HAlign_Fill : HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked(FOnClicked::CreateSP(this, &SScctButton::HandleClicked))
		[
			SNew(SBox).MinDesiredWidth(InArgs._MinWidth).HAlign(bMenu ? HAlign_Left : HAlign_Center)
			[
				Lines
			]
		]);
	Register();
}

SScctButton::~SScctButton()
{
	Unregister();
}

bool SScctButton::IsLit() const
{
	return IsEnabled() && (HasKeyboardFocus() || IsHovered() || bDrivenHover);
}

FReply SScctButton::HandleClicked()
{
	ScctUi::Play(EScctUiSound::Accept);
	// The delegate may close the page this button is on: nothing of the button is used after it.
	const FSimpleDelegate Pressed = OnPressedDelegate;
	Pressed.ExecuteIfBound();
	return FReply::Handled();
}

FReply SScctButton::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (InFocusEvent.GetCause() != EFocusCause::SetDirectly)
	{
		ScctUi::Play(EScctUiSound::Move);
	}
	ScctUi::Describe(Caption.Get(), DescriptionText);
	return SButton::OnFocusReceived(MyGeometry, InFocusEvent);
}

void SScctButton::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SButton::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsEnabled() && !HasKeyboardFocus() && !ScctUi::IsTyping())
	{
		FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::Mouse);
	}
}

FReply SScctButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	ScctUi::NoteInput(InKeyEvent.GetKey());
	return SButton::OnKeyDown(MyGeometry, InKeyEvent);
}

int32 SScctButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float Delta = FrameSeconds();
	Lit = ScctHudStyle::Ease(Lit, IsLit() ? 1.0f : 0.0f, Delta, 18.0f);
	Press = ScctHudStyle::Ease(Press, IsPressed() ? 1.0f : 0.0f, Delta, 30.0f);

	const FPainter Painter{ OutDrawElements, AllottedGeometry, LayerId, InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1.0f, 1.0f, 1.0f, IsEnabled() ? 1.0f : 0.4f) };
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const bool bSelected = Selected.Get();
	const bool bFocused = HasKeyboardFocus();
	switch (Look)
	{
	case EScctButtonLook::Primary:
		// A solid plate of the accent that brightens, with a soft edge of light when lit.
		Painter.Box(FVector2D(-3.0, -3.0), Size + FVector2D(6.0, 6.0), FillBrush(ScctUi::Corner + 3.0f), ScctHudStyle::Fade(Colour, 0.22f * Lit));
		Painter.Box(FVector2D::ZeroVector, Size, FillBrush(), FMath::Lerp(ScctHudStyle::Fade(Colour, 0.86f), FMath::Lerp(Colour, FLinearColor::White, 0.35f), Lit) * (1.0f - 0.15f * Press));
		break;
	case EScctButtonLook::Menu:
		// Nothing until it is lit: then a wash that fades to the right and a bar of the accent.
		Painter.Gradient(FVector2D::ZeroVector, Size, ScctHudStyle::Fade(Colour, 0.26f * Lit), ScctHudStyle::Fade(Colour, 0.0f));
		Painter.Box(FVector2D::ZeroVector, FVector2D(3.0, Size.Y), WhiteBrush(), ScctHudStyle::Fade(Colour, Lit), 1);
		break;
	case EScctButtonLook::Tab:
		Painter.Box(FVector2D::ZeroVector, Size, FillBrush(), ScctUi::White(0.07f * Lit));
		Painter.Box(FVector2D(0.0, Size.Y - 2.0), FVector2D(Size.X, 2.0), WhiteBrush(), bSelected ? Colour : ScctUi::White(0.12f + 0.2f * Lit), 1);
		break;
	case EScctButtonLook::Chip:
		Painter.Box(FVector2D::ZeroVector, Size, FillBrush(), bSelected ? ScctHudStyle::Fade(Colour, 0.3f + 0.14f * Lit) : ScctUi::White(0.06f + 0.1f * Lit));
		Painter.Box(FVector2D::ZeroVector, Size, OutlineBrush(), bSelected ? ScctHudStyle::Fade(Colour, 0.9f) : ScctUi::White(0.1f + 0.3f * Lit), 1);
		break;
	default:
		Painter.Box(FVector2D::ZeroVector, Size, FillBrush(), ScctHudStyle::Fade(FMath::Lerp(ScctUi::White(0.07f), ScctHudStyle::Fade(Colour, 0.34f), Lit), 1.0f + 0.5f * Press));
		Painter.Box(FVector2D::ZeroVector, Size, OutlineBrush(), FMath::Lerp(ScctUi::White(0.14f), ScctHudStyle::Fade(Colour, 0.95f), Lit), 1);
		break;
	}
	if (bFocused && Look != EScctButtonLook::Menu)
	{
		// The keyboard's and the controller's place: a second, outer line.
		Painter.Box(FVector2D(-3.0, -3.0), Size + FVector2D(6.0, 6.0), OutlineBrush(ScctUi::Corner + 3.0f), ScctHudStyle::Fade(Look == EScctButtonLook::Primary ? FLinearColor::White : Colour, 0.75f * Lit), 1);
	}
	else if (bFocused && ScctUi::IsPadActive())
	{
		// A menu entry reached with the controller: its wash and bar look as under the pointer, so the controller's
		// place gets a hairline round the entry as well (there is no pointer to say where it is).
		Painter.Box(FVector2D::ZeroVector, Size, OutlineBrush(), ScctHudStyle::Fade(Colour, 0.55f * Lit), 1);
	}
	return SButton::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

// ---- SScctOptionRow -----------------------------------------------------------------------------------------------

SScctOptionRow::~SScctOptionRow()
{
	Unregister();
}

void SScctOptionRow::BuildRow(const FText& InCaption, const FText& InDescription, const TSharedRef<SWidget>& Control)
{
	Caption = InCaption;
	DescriptionText = InDescription;
	ChildSlot
	[
		// A caption too long for its place wraps and the row grows: a caption is never cut off.
		SNew(SBox).MinDesiredHeight(ScctUi::RowHeight)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(18.0f, 4.0f, 12.0f, 4.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(Caption)
				.Font(ScctUi::Body())
				.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(ScctHudStyle::Fade(ScctHudStyle::Text(), (IsEnabled() ? 1.0f : 0.4f) * (0.78f + 0.22f * Lit))); }))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(ControlWidth)[ Control ]
			]
		]
	];
	Register();
}

bool SScctOptionRow::IsLit() const
{
	return IsEnabled() && (HasKeyboardFocus() || IsHovered() || HasFocusedDescendants());
}

FVector2D SScctOptionRow::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const FVector2D Size = SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
	return FVector2D(FMath::Max(Size.X, ControlWidth + 200.0), FMath::Max<double>(Size.Y, ScctUi::RowHeight));
}

FSlateRect SScctOptionRow::GetControlRect(const FGeometry& Geometry) const
{
	const FVector2D Size = Geometry.GetLocalSize();
	return FSlateRect(Size.X - 16.0f - ControlWidth, 0.0f, Size.X - 16.0f, Size.Y);
}

FReply SScctOptionRow::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (InFocusEvent.GetCause() != EFocusCause::SetDirectly)
	{
		ScctUi::Play(EScctUiSound::Move);
	}
	ScctUi::Describe(Caption, DescriptionText);
	return SCompoundWidget::OnFocusReceived(MyGeometry, InFocusEvent);
}

void SScctOptionRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsEnabled() && !HasKeyboardFocus() && !ScctUi::IsTyping())
	{
		FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::Mouse);
	}
}

FReply SScctOptionRow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	ScctUi::NoteInput(InKeyEvent.GetKey());
	if (!HasKeyboardFocus())
	{
		// A key typed into the row's own text field.
		return FReply::Unhandled();
	}
	const int32 Direction = ScctUi::AdjustDirection(InKeyEvent.GetKey());
	if (Direction != 0 && Adjust(Direction))
	{
		ScctUi::Play(EScctUiSound::Move);
		return FReply::Handled();
	}
	if (IsAccept(InKeyEvent))
	{
		ScctUi::Play(EScctUiSound::Accept);
		Press();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FNavigationReply SScctOptionRow::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// A stick pushed left or right arrives here (the keys were taken in OnKeyDown).
	const EUINavigation Type = InNavigationEvent.GetNavigationType();
	if (HasKeyboardFocus() && (Type == EUINavigation::Left || Type == EUINavigation::Right) && Adjust(Type == EUINavigation::Left ? -1 : 1))
	{
		ScctUi::Play(EScctUiSound::Move);
		return FNavigationReply::Stop();
	}
	return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

FReply SScctOptionRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FSlateRect Rect = GetControlRect(MyGeometry);
	ScctUi::Play(EScctUiSound::Accept);
	if (Local.X >= Rect.Left && Local.X <= Rect.Right)
	{
		ClickControl((Local.X - Rect.Left) / FMath::Max(Rect.Right - Rect.Left, 1.0f));
	}
	else
	{
		Press();
	}
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

int32 SScctOptionRow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	Lit = ScctHudStyle::Ease(Lit, IsLit() ? 1.0f : 0.0f, FrameSeconds(), 18.0f);
	const FPainter Painter{ OutDrawElements, AllottedGeometry, LayerId, InWidgetStyle.GetColorAndOpacityTint() };
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	Painter.Box(FVector2D(0.0, 1.0), Size - FVector2D(0.0, 2.0), FillBrush(), FMath::Lerp(ScctUi::White(0.035f), ScctHudStyle::Fade(ScctUi::Accent(), 0.17f), Lit));
	Painter.Box(FVector2D(0.0, 1.0), Size - FVector2D(0.0, 2.0), OutlineBrush(), ScctHudStyle::Fade(ScctUi::Accent(), 0.7f * Lit), 1);
	Painter.Box(FVector2D(0.0, 9.0), FVector2D(3.0, Size.Y - 18.0), WhiteBrush(), ScctHudStyle::Fade(ScctUi::Accent(), Lit), 1);
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

// ---- SScctStepper -------------------------------------------------------------------------------------------------

void SScctStepper::Construct(const FArguments& InArgs)
{
	Options = InArgs._Options;
	GetOptions = InArgs._GetOptions;
	Get = InArgs._Get;
	Set = InArgs._Set;
	bWrap = InArgs._Wrap;
	BuildRow(InArgs._Label, InArgs._Description, SNullWidget::NullWidget);
}

TArray<FString> SScctStepper::CurrentOptions() const
{
	return GetOptions ? GetOptions() : Options;
}

bool SScctStepper::Adjust(int32 Direction)
{
	const int32 Count = CurrentOptions().Num();
	if (Count < 2 || !Get || !Set)
	{
		return Count > 0;
	}
	const int32 Now = FMath::Clamp(Get(), 0, Count - 1);
	const int32 Next = bWrap ? (Now + Direction + Count) % Count : FMath::Clamp(Now + Direction, 0, Count - 1);
	if (Next != Now)
	{
		Set(Next);
	}
	return true;
}

int32 SScctStepper::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = SScctOptionRow::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FPainter Painter{ OutDrawElements, AllottedGeometry, Layer, InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1.0f, 1.0f, 1.0f, IsEnabled() ? 1.0f : 0.4f) };
	const FSlateRect Rect = GetControlRect(AllottedGeometry);
	const TArray<FString> Shown = CurrentOptions();
	const int32 Index = Get && Shown.Num() > 0 ? FMath::Clamp(Get(), 0, Shown.Num() - 1) : 0;
	const float Middle = (Rect.Top + Rect.Bottom) * 0.5f;

	Painter.Box(FVector2D(Rect.Left, Middle - 15.0f), FVector2D(Rect.Right - Rect.Left, 30.0f), FillBrush(), FLinearColor(0.0f, 0.0f, 0.0f, 0.32f));
	const FSlateFontInfo Arrow = ScctHudStyle::Font(EWeight::Bold, 15.0f);
	const bool bLeft = bWrap || Index > 0;
	const bool bRight = bWrap || Index < Shown.Num() - 1;
	const FLinearColor ArrowColour = FMath::Lerp(ScctHudStyle::TextFaint(), ScctUi::Accent(), Lit);
	Painter.TextAt(FVector2D(Rect.Left + 16.0f, Middle - 1.0f), TEXT("‹"), Arrow, ScctHudStyle::Fade(ArrowColour, bLeft && Shown.Num() > 1 ? 1.0f : 0.25f));
	Painter.TextAt(FVector2D(Rect.Right - 16.0f, Middle - 1.0f), TEXT("›"), Arrow, ScctHudStyle::Fade(ArrowColour, bRight && Shown.Num() > 1 ? 1.0f : 0.25f));
	if (Shown.IsValidIndex(Index))
	{
		Painter.TextAt(FVector2D((Rect.Left + Rect.Right) * 0.5f, Middle), Shown[Index].ToUpper(), ScctUi::Caps(12.5f), ScctHudStyle::Text());
	}
	return Layer + 2;
}

// ---- SScctSlider --------------------------------------------------------------------------------------------------

void SScctSlider::Construct(const FArguments& InArgs)
{
	Min = InArgs._Min;
	Max = InArgs._Max;
	Step = InArgs._Step;
	Get = InArgs._Get;
	Set = InArgs._Set;
	Format = InArgs._Format;
	BuildRow(InArgs._Label, InArgs._Description, SNullWidget::NullWidget);
}

bool SScctSlider::Adjust(int32 Direction)
{
	if (Get && Set)
	{
		// Whole steps from the bottom of the range, so a value never drifts off them.
		const float Next = FMath::Clamp(Min + FMath::RoundToFloat((Get() - Min) / Step + Direction) * Step, Min, Max);
		Set(Next);
	}
	return true;
}

FSlateRect SScctSlider::GetTrackRect(const FGeometry& Geometry) const
{
	const FSlateRect Rect = GetControlRect(Geometry);
	return FSlateRect(Rect.Left + 8.0f, Rect.Top, Rect.Right - 78.0f, Rect.Bottom);
}

void SScctSlider::SetFromPointer(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	const FSlateRect Track = GetTrackRect(Geometry);
	const float Alpha = FMath::Clamp((Geometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X - Track.Left) / FMath::Max(Track.Right - Track.Left, 1.0f), 0.0f, 1.0f);
	if (Set)
	{
		Set(FMath::Clamp(Min + FMath::RoundToFloat(Alpha * (Max - Min) / Step) * Step, Min, Max));
	}
}

FReply SScctSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const FSlateRect Rect = GetControlRect(MyGeometry);
	if (MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X >= Rect.Left)
	{
		bDragging = true;
		SetFromPointer(MyGeometry, MouseEvent);
		return FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared(), EFocusCause::Mouse);
	}
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SScctSlider::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging)
	{
		bDragging = false;
		ScctUi::Play(EScctUiSound::Move);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SScctSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging)
	{
		SetFromPointer(MyGeometry, MouseEvent);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

int32 SScctSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = SScctOptionRow::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FPainter Painter{ OutDrawElements, AllottedGeometry, Layer, InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1.0f, 1.0f, 1.0f, IsEnabled() ? 1.0f : 0.4f) };
	const FSlateRect Rect = GetControlRect(AllottedGeometry);
	const FSlateRect Track = GetTrackRect(AllottedGeometry);
	const float Value = Get ? Get() : Min;
	const float Alpha = FMath::Clamp((Value - Min) / FMath::Max(Max - Min, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float Middle = (Rect.Top + Rect.Bottom) * 0.5f;
	const float Width = Track.Right - Track.Left;
	const FLinearColor Colour = FMath::Lerp(ScctHudStyle::Fade(ScctHudStyle::Text(), 0.7f), ScctUi::Accent(), Lit);

	Painter.Box(FVector2D(Track.Left, Middle - 2.0f), FVector2D(Width, 4.0f), FillBrush(2.0f), ScctUi::White(0.16f));
	Painter.Box(FVector2D(Track.Left, Middle - 2.0f), FVector2D(Width * Alpha, 4.0f), FillBrush(2.0f), Colour, 1);
	Painter.Box(FVector2D(Track.Left + Width * Alpha - 5.0f, Middle - 9.0f), FVector2D(10.0f, 18.0f), FillBrush(3.0f), FMath::Lerp(ScctHudStyle::Text(), FLinearColor::White, Lit), 2);

	const FString Shown = Format ? Format(Value) : FString::SanitizeFloat(Value);
	const FSlateFontInfo Font = ScctUi::Caps(12.5f);
	Painter.Text(FVector2D(Rect.Right - Measure(Shown, Font).X, Middle - Measure(Shown, Font).Y * 0.5), Shown, Font, ScctHudStyle::Text(), 1);
	return Layer + 3;
}

// ---- SScctToggle --------------------------------------------------------------------------------------------------

void SScctToggle::Construct(const FArguments& InArgs)
{
	Get = InArgs._Get;
	Set = InArgs._Set;
	BuildRow(InArgs._Label, InArgs._Description, SNullWidget::NullWidget);
}

bool SScctToggle::Adjust(int32 Direction)
{
	if (Get && Set && Get() != (Direction > 0))
	{
		Set(Direction > 0);
	}
	return true;
}

void SScctToggle::Press()
{
	if (Get && Set)
	{
		Set(!Get());
	}
}

int32 SScctToggle::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = SScctOptionRow::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FPainter Painter{ OutDrawElements, AllottedGeometry, Layer, InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1.0f, 1.0f, 1.0f, IsEnabled() ? 1.0f : 0.4f) };
	const FSlateRect Rect = GetControlRect(AllottedGeometry);
	const bool bOn = Get && Get();
	Shown = Shown < 0.0f ? (bOn ? 1.0f : 0.0f) : ScctHudStyle::Ease(Shown, bOn ? 1.0f : 0.0f, FrameSeconds(), 20.0f);
	const float Middle = (Rect.Top + Rect.Bottom) * 0.5f;
	const FVector2D Pill(46.0, 24.0);
	const FVector2D Origin(Rect.Right - Pill.X, Middle - Pill.Y * 0.5);

	Painter.Box(Origin, Pill, FillBrush(12.0f), FMath::Lerp(ScctUi::White(0.14f), ScctHudStyle::Fade(ScctUi::Accent(), 0.85f), Shown));
	Painter.Box(Origin, Pill, OutlineBrush(12.0f), ScctUi::White(0.18f + 0.3f * Lit), 1);
	Painter.Box(Origin + FVector2D(3.0 + (Pill.X - 24.0) * Shown, 3.0), FVector2D(18.0, 18.0), FillBrush(9.0f), FMath::Lerp(ScctHudStyle::Text(), ScctUi::Ink(), Shown), 2);

	const FString Word = bOn ? TEXT("ON") : TEXT("OFF");
	const FSlateFontInfo Font = ScctUi::Caps(12.5f);
	Painter.Text(FVector2D(Origin.X - 14.0 - Measure(Word, Font).X, Middle - Measure(Word, Font).Y * 0.5), Word, Font, bOn ? ScctHudStyle::Text() : ScctHudStyle::TextDim(), 1);
	return Layer + 3;
}

// ---- SScctKeyRow --------------------------------------------------------------------------------------------------

void SScctKeyRow::Construct(const FArguments& InArgs)
{
	GetKeys = InArgs._GetKeys;
	OnCaptured = InArgs._OnCaptured;
	BuildRow(InArgs._Label, InArgs._Description, SNullWidget::NullWidget);
}

void SScctKeyRow::Press()
{
	bCapturing = true;
}

void SScctKeyRow::Finish(const FKey& Key, bool bCommit)
{
	bCapturing = false;
	if (bCommit && OnCaptured)
	{
		OnCaptured(Key);
	}
	ScctUi::Play(bCommit ? EScctUiSound::Accept : EScctUiSound::Back);
}

void SScctKeyRow::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	bCapturing = false;
	SScctOptionRow::OnFocusLost(InFocusEvent);
}

FReply SScctKeyRow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bCapturing)
	{
		if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
		{
			Finish(FKey(), true);
			return FReply::Handled();
		}
		return SScctOptionRow::OnKeyDown(MyGeometry, InKeyEvent);
	}

	const FKey Key = InKeyEvent.GetKey();
	ScctUi::NoteInput(Key);
	if (Key == EKeys::Escape)
	{
		Finish(FKey(), false);
	}
	else if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		Finish(FKey(), true);
	}
	else if (!InKeyEvent.IsRepeat())
	{
		Finish(Key, true);
	}
	return FReply::Handled();
}

FReply SScctKeyRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bCapturing)
	{
		Finish(MouseEvent.GetEffectingButton(), true);
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	ScctUi::Play(EScctUiSound::Accept);
	Press();
	// The next click binds the mouse button wherever the pointer is.
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse).CaptureMouse(AsShared());
}

FReply SScctKeyRow::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bCapturing)
	{
		return FReply::Unhandled();
	}
	Finish(MouseEvent.GetWheelDelta() > 0.0f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown, true);
	return FReply::Handled().ReleaseMouseCapture();
}

int32 SScctKeyRow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = SScctOptionRow::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FPainter Painter{ OutDrawElements, AllottedGeometry, Layer, InWidgetStyle.GetColorAndOpacityTint() };
	const FSlateRect Rect = GetControlRect(AllottedGeometry);
	const float Middle = (Rect.Top + Rect.Bottom) * 0.5f;
	const FSlateFontInfo Font = ScctUi::Caps(12.0f);

	if (bCapturing)
	{
		const float Pulse = 0.6f + 0.4f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 7.0f);
		Painter.Box(FVector2D(Rect.Left, Middle - 15.0f), FVector2D(Rect.Right - Rect.Left, 30.0f), FillBrush(), ScctHudStyle::Fade(ScctUi::Accent(), 0.16f));
		Painter.Box(FVector2D(Rect.Left, Middle - 15.0f), FVector2D(Rect.Right - Rect.Left, 30.0f), OutlineBrush(), ScctHudStyle::Fade(ScctUi::Accent(), Pulse), 1);
		Painter.TextAt(FVector2D((Rect.Left + Rect.Right) * 0.5f, Middle), TEXT("PRESS A KEY   ·   ESC CANCELS"), Font, ScctHudStyle::Text(), 2);
		return Layer + 3;
	}

	TArray<FString> Keys;
	(GetKeys ? GetKeys() : FString()).ParseIntoArray(Keys, TEXT("\n"));
	if (Keys.IsEmpty())
	{
		const FString None = TEXT("NOT BOUND");
		Painter.Text(FVector2D(Rect.Right - Measure(None, Font).X - 4.0, Middle - Measure(None, Font).Y * 0.5), None, Font, ScctHudStyle::TextFaint());
		return Layer + 2;
	}

	// Key caps, the first binding at the right edge.
	float X = Rect.Right;
	for (int32 Index = 0; Index < Keys.Num() && Index < 3; ++Index)
	{
		// "MiddleMouse" reads as "MIDDLE MOUSE".
		FString Name;
		const FString& Raw = Keys[Index];
		for (int32 At = 0; At < Raw.Len(); ++At)
		{
			if (At > 0 && FChar::IsUpper(Raw[At]) && FChar::IsLower(Raw[At - 1]))
			{
				Name.AppendChar(TEXT(' '));
			}
			Name.AppendChar(Raw[At]);
		}
		Name.ToUpperInline();
		const FVector2D TextSize = Measure(Name, Font);
		const float Width = FMath::Max(TextSize.X + 20.0, 34.0);
		X -= Width;
		if (X < Rect.Left)
		{
			break;
		}
		Painter.Box(FVector2D(X, Middle - 14.0), FVector2D(Width, 28.0), FillBrush(), ScctUi::White(0.1f + 0.06f * Lit));
		Painter.Box(FVector2D(X, Middle - 14.0), FVector2D(Width, 28.0), OutlineBrush(), ScctUi::White(0.3f + 0.3f * Lit), 1);
		Painter.TextAt(FVector2D(X + Width * 0.5, Middle), Name, Font, ScctHudStyle::Text(), 2);
		X -= 6.0f;
	}
	return Layer + 3;
}

// ---- SScctTextRow -------------------------------------------------------------------------------------------------

void SScctTextRow::Construct(const FArguments& InArgs)
{
	const TFunction<void(const FString&)> Changed = InArgs._OnChanged;
	const int32 MaxLength = InArgs._MaxLength;
	Box = ScctUi::TextBox(InArgs._Hint, InArgs._Initial, false, FOnTextCommitted::CreateLambda([this, Changed, MaxLength](const FText& NewText, ETextCommit::Type Commit)
	{
		if (Changed)
		{
			Changed(NewText.ToString().Left(MaxLength).TrimStartAndEnd());
		}
		if (Commit == ETextCommit::OnEnter)
		{
			FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
		}
	}));
	BuildRow(InArgs._Label, InArgs._Description, Box.ToSharedRef());
}

void SScctTextRow::Press()
{
	FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
}

// ---- SScctTabBar --------------------------------------------------------------------------------------------------

void SScctTabBar::Construct(const FArguments& InArgs)
{
	Count = InArgs._Tabs.Num();
	Tab = FMath::Clamp(InArgs._Initial, 0, FMath::Max(Count - 1, 0));
	OnChanged = InArgs._OnChanged;

	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ ScctUi::KeyHint(TEXT("Q"), TEXT("LB"), FString()) ];
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SScctButton)
			.Look(EScctButtonLook::Tab)
			.Text(FText::FromString(InArgs._Tabs[Index].ToUpper()))
			.Selected(TAttribute<bool>::CreateLambda([this, Index] { return Tab == Index; }))
			.OnPressed(FSimpleDelegate::CreateLambda([this, Index] { SetTab(Index); }))
		];
	}
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 0.0f, 0.0f, 0.0f)[ ScctUi::KeyHint(TEXT("E"), TEXT("RB"), FString()) ];
	ChildSlot[ Row ];
}

void SScctTabBar::SetTab(int32 NewTab)
{
	NewTab = FMath::Clamp(NewTab, 0, FMath::Max(Count - 1, 0));
	if (NewTab != Tab)
	{
		Tab = NewTab;
		ScctUi::Play(EScctUiSound::Open);
		OnChanged.ExecuteIfBound();
	}
}

// ---- SScctTable ---------------------------------------------------------------------------------------------------

void SScctListItem::Construct(const FArguments& InArgs)
{
	Selected = InArgs._Selected;
	OnFocused = InArgs._OnFocused;
	OnActivated = InArgs._OnActivated;
	ChildSlot.Padding(InArgs._Padding)[ InArgs._Content.Widget ];
}

FReply SScctListItem::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (InFocusEvent.GetCause() != EFocusCause::SetDirectly)
	{
		ScctUi::Play(EScctUiSound::Move);
	}
	OnFocused.ExecuteIfBound();
	return FReply::Handled();
}

FReply SScctListItem::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
		? FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse)
		: FReply::Unhandled();
}

FReply SScctListItem::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	ScctUi::Play(EScctUiSound::Accept);
	const FSimpleDelegate Activated = OnActivated;
	Activated.ExecuteIfBound();
	return FReply::Handled();
}

FReply SScctListItem::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	ScctUi::NoteInput(InKeyEvent.GetKey());
	if (IsAccept(InKeyEvent))
	{
		ScctUi::Play(EScctUiSound::Accept);
		const FSimpleDelegate Activated = OnActivated;
		Activated.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

int32 SScctListItem::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bSelected = Selected.Get();
	Lit = ScctHudStyle::Ease(Lit, HasKeyboardFocus() ? 1.0f : IsHovered() ? 0.5f : 0.0f, FrameSeconds(), 18.0f);
	const FPainter Painter{ OutDrawElements, AllottedGeometry, LayerId, InWidgetStyle.GetColorAndOpacityTint() };
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	Painter.Box(FVector2D(0.0, 1.0), Size - FVector2D(0.0, 2.0), FillBrush(), bSelected ? ScctHudStyle::Fade(ScctUi::Accent(), 0.16f + 0.1f * Lit) : ScctUi::White(0.025f + 0.08f * Lit));
	Painter.Box(FVector2D(0.0, 1.0), Size - FVector2D(0.0, 2.0), OutlineBrush(), ScctHudStyle::Fade(ScctUi::Accent(), HasKeyboardFocus() ? 0.85f : bSelected ? 0.35f : 0.0f), 1);
	Painter.Box(FVector2D(0.0, 7.0), FVector2D(3.0, Size.Y - 14.0), WhiteBrush(), ScctHudStyle::Fade(ScctUi::Accent(), bSelected ? 1.0f : 0.0f), 1);
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

namespace
{
	double LeadingNumber(const FString& Cell)
	{
		return FCString::Atod(*Cell);
	}
}

void SScctTable::Construct(const FArguments& InArgs)
{
	Columns = InArgs._Columns;
	SortColumn = InArgs._SortColumn;
	bSortDescending = InArgs._SortDescending;
	OnSelected = InArgs._OnSelected;
	OnActivated = InArgs._OnActivated;
	EmptyText = InArgs._EmptyText;

	// The heads: a click sorts.
	const TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < Columns.Num(); ++Index)
	{
		const FScctTableColumn& Column = Columns[Index];
		const TSharedRef<SWidget> Cell = SNew(SButton)
			.ButtonStyle(&BareButton())
			.IsFocusable(false)
			// A right-aligned column keeps a wider gap before the next one, so "0 / 24" and "In lobby" do not touch.
			.ContentPadding(FMargin(0.0f, 6.0f, Column.bRight ? 22.0f : 10.0f, 6.0f))
			.HAlign(Column.bRight ? HAlign_Right : HAlign_Left)
			.OnClicked(FOnClicked::CreateLambda([this, Index] { SortBy(Index); return FReply::Handled(); }))
			[
				SNew(STextBlock)
				.Font(ScctUi::Caps(11.0f))
				.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Index] { return FSlateColor(SortColumn == Index ? ScctUi::Accent() : ScctHudStyle::TextDim()); }))
				.Text(TAttribute<FText>::CreateLambda([this, Index]
				{
					const FString Mark = SortColumn != Index ? FString() : bSortDescending ? TEXT(" ↓") : TEXT(" ↑");
					return FText::FromString(Columns[Index].Title.ToUpper() + Mark);
				}))
			];
		if (Column.Fill > 0.0f)
		{
			Head->AddSlot().FillWidth(Column.Fill)[ Cell ];
		}
		else
		{
			Head->AddSlot().AutoWidth()[ SNew(SBox).WidthOverride(Column.Width)[ Cell ] ];
		}
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(14.0f, 0.0f, 26.0f, 4.0f)[ Head ]
		+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Rule(0.12f) ]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SAssignNew(Scroll, SScrollBox)
			.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
			+ SScrollBox::Slot().Padding(0.0f, 0.0f, 12.0f, 0.0f)[ SAssignNew(RowBox, SVerticalBox) ]
		]
	];
	Rebuild();
}

void SScctTable::SortBy(int32 Column)
{
	bSortDescending = SortColumn == Column ? !bSortDescending : Columns.IsValidIndex(Column) && Columns[Column].bNumeric;
	SortColumn = Column;
	ScctUi::Play(EScctUiSound::Move);
	Rebuild();
}

void SScctTable::SetRows(const TArray<FScctTableRow>& InRows)
{
	Rows = InRows;
	if (!Rows.ContainsByPredicate([this](const FScctTableRow& Row) { return Row.Id == SelectedId; }))
	{
		SelectedId = INDEX_NONE;
	}
	Rebuild();
}

void SScctTable::Select(int32 Id)
{
	if (SelectedId != Id)
	{
		SelectedId = Id;
		OnSelected.ExecuteIfBound(Id);
	}
}

void SScctTable::Rebuild()
{
	if (!RowBox.IsValid())
	{
		return;
	}
	// The focus stays on the list when its rows are written again under it.
	const bool bHadFocus = RowBox->HasFocusedDescendants();
	RowBox->ClearChildren();

	TArray<FScctTableRow> Sorted = Rows;
	if (Columns.IsValidIndex(SortColumn))
	{
		const bool bNumeric = Columns[SortColumn].bNumeric;
		Sorted.StableSort([this, bNumeric](const FScctTableRow& A, const FScctTableRow& B)
		{
			const FString Left = A.Cells.IsValidIndex(SortColumn) ? A.Cells[SortColumn] : FString();
			const FString Right = B.Cells.IsValidIndex(SortColumn) ? B.Cells[SortColumn] : FString();
			const bool bLess = bNumeric ? LeadingNumber(Left) < LeadingNumber(Right) : Left.Compare(Right, ESearchCase::IgnoreCase) < 0;
			const bool bMore = bNumeric ? LeadingNumber(Left) > LeadingNumber(Right) : Left.Compare(Right, ESearchCase::IgnoreCase) > 0;
			return bSortDescending ? bMore : bLess;
		});
	}

	if (Sorted.IsEmpty())
	{
		RowBox->AddSlot().AutoHeight().Padding(14.0f, 26.0f)
		[
			SNew(STextBlock).Text(EmptyText).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
		];
		return;
	}

	TSharedPtr<SWidget> FocusTarget;
	for (const FScctTableRow& Row : Sorted)
	{
		const TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (int32 Index = 0; Index < Columns.Num(); ++Index)
		{
			const FScctTableColumn& Column = Columns[Index];
			const bool bAccent = (Row.AccentCells & (1u << Index)) != 0;
			const FLinearColor Colour = ScctHudStyle::Fade(bAccent ? Row.Accent : ScctHudStyle::Text(), Row.bDim ? 0.42f : 1.0f);
			const TSharedRef<SWidget> Cell = SNew(SBox).HAlign(Column.bRight ? HAlign_Right : HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, Column.bRight ? 22.0f : 10.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row.Cells.IsValidIndex(Index) ? Row.Cells[Index] : FString()))
				.Font(Index == 0 ? ScctUi::Body() : ScctUi::Small())
				.ColorAndOpacity(Colour)
				// A long server or map name takes a second line rather than being cut off.
				.AutoWrapText(true)
			];
			if (Column.Fill > 0.0f)
			{
				Line->AddSlot().FillWidth(Column.Fill)[ Cell ];
			}
			else
			{
				Line->AddSlot().AutoWidth()[ SNew(SBox).WidthOverride(Column.Width)[ Cell ] ];
			}
		}

		const int32 Id = Row.Id;
		const TSharedRef<SWidget> Widget = SNew(SScctListItem)
			.Selected(TAttribute<bool>::CreateLambda([this, Id] { return SelectedId == Id; }))
			.OnFocused(FSimpleDelegate::CreateLambda([this, Id] { Select(Id); }))
			.OnActivated(FSimpleDelegate::CreateLambda([this, Id] { Select(Id); OnActivated.ExecuteIfBound(Id); }))
			[
				SNew(SBox).MinDesiredHeight(40.0f).Padding(FMargin(0.0f, 4.0f))[ Line ]
			];
		RowBox->AddSlot().AutoHeight()[ Widget ];
		if (Id == SelectedId || !FocusTarget.IsValid())
		{
			FocusTarget = Widget;
		}
	}
	if (bHadFocus && FocusTarget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(FocusTarget, EFocusCause::SetDirectly);
	}
}

// ---- SScctSpinner -------------------------------------------------------------------------------------------------

int32 SScctSpinner::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FPainter Painter{ OutDrawElements, AllottedGeometry, LayerId, InWidgetStyle.GetColorAndOpacityTint() };
	const FVector2D Centre = AllottedGeometry.GetLocalSize() * 0.5;
	const float Radius = Size * 0.5f - 2.0f;
	constexpr int32 Dots = 12;
	const float Turn = FMath::Fmod(static_cast<float>(FPlatformTime::Seconds()) * 1.1f, 1.0f);
	for (int32 Index = 0; Index < Dots; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / Dots;
		const float Behind = FMath::Fmod(Turn - static_cast<float>(Index) / Dots + 1.0f, 1.0f);
		const FVector2D Dot = Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		Painter.Box(Dot - FVector2D(1.6, 1.6), FVector2D(3.2, 3.2), FillBrush(2.0f), ScctHudStyle::Fade(ScctUi::Accent(), 0.12f + 0.88f * FMath::Square(1.0f - Behind)));
	}
	return LayerId + 1;
}

// ---- SScctDialogLayer ---------------------------------------------------------------------------------------------

void SScctDialogLayer::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SOverlay)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bOpen ? EVisibility::Visible : EVisibility::Collapsed; }))
			+ SOverlay::Slot()[ SNew(SBackgroundBlur).BlurStrength(TAttribute<float>::CreateLambda([this] { return 9.0f * Shown; })) ]
			+ SOverlay::Slot()[ SNew(SImage).Image(WhiteBrush()).ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(ScctUi::Ink(0.62f * Shown)); })) ]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SAssignNew(DialogBox, SBox).WidthOverride(560.0f)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.0f, 110.0f, 48.0f, 0.0f)
		[
			SAssignNew(ToastBox, SVerticalBox).Visibility(EVisibility::HitTestInvisible)
		]
	];
}

void SScctDialogLayer::Show(const FScctDialog& Dialog)
{
	if (!bOpen)
	{
		FocusBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}
	Current = Dialog;
	bOpen = true;
	Shown = 0.0f;
	TimeLeft = Dialog.Timeout;
	TextField.Reset();
	ScctUi::Play(EScctUiSound::Open);

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[ ScctUi::Text(ScctUi::ToText(Dialog.Title.ToUpper()), ScctUi::H2()) ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)[ ScctUi::Rule() ];
	if (!Dialog.Body.IsEmpty() || Dialog.Timeout > 0.0f || Dialog.LiveBody)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 0.0f)
		[
			ScctUi::Text(TAttribute<FText>::CreateLambda([this]
			{
				const FString Shown = Current.LiveBody ? Current.LiveBody() : Current.Body;
				return FText::FromString(Current.Timeout > 0.0f ? FString::Printf(TEXT("%s %d"), *Shown, FMath::CeilToInt32(TimeLeft)) : Shown);
			}), ScctUi::Body(), FSlateColor(ScctHudStyle::TextDim()), true)
		];
	}
	if (Dialog.bTextField)
	{
		TextField = ScctUi::TextBox(Dialog.TextHint, FString(), Dialog.bPassword, FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type Commit)
		{
			if (Commit == ETextCommit::OnEnter && bOpen)
			{
				Answer(0);
			}
		}));
		Box->AddSlot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 0.0f)[ TextField.ToSharedRef() ];
	}

	const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	TSharedPtr<SWidget> Focus;
	for (int32 Index = 0; Index < Dialog.Buttons.Num(); ++Index)
	{
		TSharedPtr<SScctButton> Button;
		Buttons->AddSlot().AutoWidth().Padding(Index > 0 ? 10.0f : 0.0f, 0.0f, 0.0f, 0.0f)
		[
			SAssignNew(Button, SScctButton)
			.Look(Dialog.Buttons[Index].Look)
			.Text(FText::FromString(Dialog.Buttons[Index].Label.ToUpper()))
			.MinWidth(120.0f)
			.OnPressed(FSimpleDelegate::CreateSP(this, &SScctDialogLayer::Answer, Index))
		];
		if (Index == Dialog.FocusButton)
		{
			Focus = Button;
		}
	}
	Box->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 26.0f, 0.0f, 0.0f)[ Buttons ];
	DialogBox->SetContent(ScctUi::Panel(Box, FMargin(32.0f, 28.0f), 0.9f));
	FSlateApplication::Get().SetKeyboardFocus(TextField.IsValid() ? StaticCastSharedPtr<SWidget>(TextField) : Focus, EFocusCause::SetDirectly);
}

void SScctDialogLayer::Answer(int32 Index)
{
	if (!bOpen)
	{
		return;
	}
	if (Index < 0 || !Current.Buttons.IsValidIndex(Index))
	{
		Index = Current.Buttons.Num() - 1;
	}
	const TFunction<void(const FString&)> Pressed = Current.Buttons.IsValidIndex(Index) ? Current.Buttons[Index].OnPressed : TFunction<void(const FString&)>();
	const FString Typed = TextField.IsValid() ? TextField->GetText().ToString() : FString();
	bOpen = false;
	TextField.Reset();
	DialogBox->SetContent(SNullWidget::NullWidget);
	const TSharedPtr<SWidget> Before = FocusBefore.Pin();
	FocusBefore.Reset();
	if (Before.IsValid() && Before->GetVisibility().IsVisible() && Before->SupportsKeyboardFocus())
	{
		FSlateApplication::Get().SetKeyboardFocus(Before, EFocusCause::SetDirectly);
	}
	else if (const TSharedPtr<SWidget> Back = FocusAfter.Pin())
	{
		FSlateApplication::Get().SetKeyboardFocus(Back, EFocusCause::SetDirectly);
	}
	if (Pressed)
	{
		Pressed(Typed);
	}
}

FReply SScctDialogLayer::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bOpen)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	if (ScctMenuNav::IsBackKey(Key))
	{
		ScctUi::Play(EScctUiSound::Back);
		Answer(Current.CancelButton);
		return FReply::Handled();
	}
	// Nothing under an open dialog hears a key.
	return FReply::Handled();
}

FNavigationReply SScctDialogLayer::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (!bOpen)
	{
		return FNavigationReply::Escape();
	}
	// Left and right (arrows, pad, Tab) go from button to button, and the focus never leaves the dialog for the page
	// under it.
	const EUINavigation Type = InNavigationEvent.GetNavigationType();
	const int32 Step = Type == EUINavigation::Left || Type == EUINavigation::Previous ? -1
		: Type == EUINavigation::Right || Type == EUINavigation::Next ? 1 : 0;
	return FNavigationReply::Custom(FNavigationDelegate::CreateSPLambda(this, [this, Step](EUINavigation) -> TSharedPtr<SWidget>
	{
		int32 Focused = INDEX_NONE;
		for (int32 Index = 0; Index < Current.Buttons.Num(); ++Index)
		{
			IScctDriven* Button = IScctDriven::Find(Current.Buttons[Index].Label);
			if (Button != nullptr && Button->GetDrivenWidget()->HasKeyboardFocus())
			{
				Focused = Index;
			}
		}
		const int32 Wanted = Focused == INDEX_NONE ? Current.FocusButton : FMath::Clamp(Focused + Step, 0, Current.Buttons.Num() - 1);
		IScctDriven* Target = Current.Buttons.IsValidIndex(Wanted) ? IScctDriven::Find(Current.Buttons[Wanted].Label) : nullptr;
		return Target != nullptr ? TSharedPtr<SWidget>(Target->GetDrivenWidget()) : TSharedPtr<SWidget>();
	}));
}

void SScctDialogLayer::Toast(const FString& Message, const FLinearColor& Colour)
{
	ScctUi::Play(EScctUiSound::Notice);
	FToast& Made = Toasts.AddDefaulted_GetRef();
	Made.Widget = SNew(SBox).MaxDesiredWidth(420.0f)
	[
		ScctUi::Panel(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(3.0f)[ SNew(SImage).Image(WhiteBrush()).ColorAndOpacity(Colour) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				ScctUi::Text(ScctUi::ToText(Message), ScctUi::Small(), FSlateColor(ScctHudStyle::Text()), true)
			],
			FMargin(14.0f, 12.0f, 18.0f, 12.0f), 0.88f)
	];
	ToastBox->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Made.Widget.ToSharedRef() ];
	while (Toasts.Num() > 4)
	{
		ToastBox->RemoveSlot(Toasts[0].Widget.ToSharedRef());
		Toasts.RemoveAt(0);
	}
}

void SScctDialogLayer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Shown = ScctHudStyle::Ease(Shown, bOpen ? 1.0f : 0.0f, InDeltaTime, 14.0f);
	if (bOpen)
	{
		DialogBox->SetRenderOpacity(Shown);
		DialogBox->SetRenderTransform(FSlateRenderTransform(FVector2D(0.0, 14.0 * (1.0 - Shown))));
		// The focus stays in the dialog.
		if (!DialogBox->HasFocusedDescendants() && FSlateApplication::Get().IsActive())
		{
			if (IScctDriven* First = Current.Buttons.IsValidIndex(Current.FocusButton) ? IScctDriven::Find(Current.Buttons[Current.FocusButton].Label) : nullptr)
			{
				FSlateApplication::Get().SetKeyboardFocus(First->GetDrivenWidget(), EFocusCause::SetDirectly);
			}
		}
		if (Current.Timeout > 0.0f)
		{
			TimeLeft -= InDeltaTime;
			if (TimeLeft <= 0.0f)
			{
				Answer(Current.CancelButton);
			}
		}
	}

	constexpr float ToastLife = 4.5f;
	for (int32 Index = Toasts.Num() - 1; Index >= 0; --Index)
	{
		FToast& Toast = Toasts[Index];
		Toast.Age += InDeltaTime;
		const float In = ScctHudStyle::EaseOut(Toast.Age / 0.25f);
		const float Out = FMath::Clamp((ToastLife - Toast.Age) / 0.4f, 0.0f, 1.0f);
		Toast.Widget->SetRenderOpacity(In * Out);
		Toast.Widget->SetRenderTransform(FSlateRenderTransform(FVector2D(30.0 * (1.0 - In), 0.0)));
		if (Toast.Age >= ToastLife)
		{
			ToastBox->RemoveSlot(Toast.Widget.ToSharedRef());
			Toasts.RemoveAt(Index);
		}
	}
}
