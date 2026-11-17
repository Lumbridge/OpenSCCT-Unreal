#include "Menu/SScctConfirm.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuStyle.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	const FName ConfirmType(TEXT("SScctConfirm"));
}

TSharedRef<SScctConfirm> SScctConfirm::Open(const TSharedRef<SOverlay>& Host, const FString& Message, const FString& YesLabel,
	const FString& NoLabel, FSimpleDelegate OnYes, FSimpleDelegate OnNo)
{
	if (const TSharedPtr<SScctConfirm> Already = FindOpen(Host))
	{
		return Already.ToSharedRef();
	}

	TSharedRef<SScctConfirm> Box = SNew(SScctConfirm)
		.Message(Message)
		.YesLabel(YesLabel)
		.NoLabel(NoLabel)
		.OnYes(OnYes)
		.OnNo(OnNo);
	Box->Host = Host;
	if (FSlateApplication::IsInitialized())
	{
		Box->FocusBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}
	Host->AddSlot()[ Box ];
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: asking \"%s\" (%s / %s)"), *Message, *YesLabel, *NoLabel);
	return Box;
}

TSharedPtr<SScctConfirm> SScctConfirm::FindOpen(const TSharedRef<SOverlay>& Host)
{
	FChildren* Children = Host->GetChildren();
	for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
	{
		const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
		if (Child->GetType() == ConfirmType)
		{
			return StaticCastSharedRef<SScctConfirm>(Child);
		}
	}
	return nullptr;
}

void SScctConfirm::Construct(const FArguments& InArgs)
{
	OnYes = InArgs._OnYes;
	OnNo = InArgs._OnNo;

	static const FSlateColorBrush Dim(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		// The page behind is dimmed and, being covered, takes no clicks while the question is open.
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(&Dim)
			.ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(440.0f)
			[
				Ui::Glass(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 24.0f)
					[
						Ui::Text(Ui::ToText(InArgs._Message), 15.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							Ui::Button(Ui::ToText(InArgs._NoLabel), FOnClicked::CreateLambda([this] { AnswerNo(); return FReply::Handled(); }), false, Ui::Accent(), 120.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Ui::Button(Ui::ToText(InArgs._YesLabel), FOnClicked::CreateLambda([this] { AnswerYes(); return FReply::Handled(); }),
								true, ScctHudStyle::Danger(), 120.0f)
						]
					], FMargin(30.0f, 26.0f), 0.85f)
			]
		]
	];
}

void SScctConfirm::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// The keyboard can only be given to a widget that is on screen, which the box is by its first tick.
	if (!bFocusTaken && FSlateApplication::IsInitialized())
	{
		bFocusTaken = true;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

FReply SScctConfirm::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		AnswerNo();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SScctConfirm::Answer(bool bYes)
{
	if (bAnswered)
	{
		return;
	}
	bAnswered = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: answered %s"), bYes ? TEXT("yes") : TEXT("no"));

	// The box goes first (the answer may open another question on the same overlay), kept alive until this returns.
	const TSharedRef<SScctConfirm> Self = SharedThis(this);
	if (const TSharedPtr<SOverlay> Overlay = Host.Pin())
	{
		Overlay->RemoveSlot(Self);
	}
	if (const TSharedPtr<SWidget> Before = FocusBefore.Pin(); Before.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Before, EFocusCause::SetDirectly);
	}
	(bYes ? OnYes : OnNo).ExecuteIfBound();
}
