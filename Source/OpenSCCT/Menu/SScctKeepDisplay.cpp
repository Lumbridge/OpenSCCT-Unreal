#include "Menu/SScctKeepDisplay.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
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
	/** Above the brightness page and everything else. */
	constexpr int32 BoxZOrder = 10010;

	TWeakPtr<SScctKeepDisplay> OpenBox;
}

bool SScctKeepDisplay::Open(const FString& Description, float Seconds, FSimpleDelegate OnKeep, FSimpleDelegate OnRevert)
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return false;
	}
	TWeakPtr<SWidget> FocusBefore;
	if (const TSharedPtr<SScctKeepDisplay> Already = OpenBox.Pin())
	{
		// The new question takes its place; the keyboard still goes back to what had it before the first.
		FocusBefore = Already->FocusBefore;
		Already->bAnswered = true;
		GEngine->GameViewport->RemoveViewportWidgetContent(Already.ToSharedRef());
	}
	else if (FSlateApplication::IsInitialized())
	{
		FocusBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}
	const TSharedRef<SScctKeepDisplay> Box = SNew(SScctKeepDisplay)
		.Seconds(Seconds)
		.Description(Description)
		.OnKeep(OnKeep)
		.OnRevert(OnRevert);
	Box->FocusBefore = FocusBefore;
	GEngine->GameViewport->AddViewportWidgetContent(Box, BoxZOrder);
	OpenBox = Box;
	UE_LOG(LogOpenSCCT, Log, TEXT("Display: asking to keep %s (going back in %.0f s)"), *Description, Seconds);
	return true;
}

bool SScctKeepDisplay::IsOpen()
{
	return OpenBox.IsValid();
}

void SScctKeepDisplay::Construct(const FArguments& InArgs)
{
	OnKeep = InArgs._OnKeep;
	OnRevert = InArgs._OnRevert;
	Deadline = FPlatformTime::Seconds() + InArgs._Seconds;

	static const FSlateColorBrush Dim(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(&Dim)
			.ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(480.0f)
			[
				Ui::Glass(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						Ui::Text(Ui::ToText(TEXT("Keep this display?")), 17.0f)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						Ui::Text(Ui::ToText(InArgs._Description), 13.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 22.0f)
					[
						Ui::Text(TAttribute<FText>::CreateLambda([this]
						{
							const int32 Left = FMath::Max(0, FMath::CeilToInt32(Deadline - FPlatformTime::Seconds()));
							return FText::FromString(FString::Printf(TEXT("Going back to the last display in %d s"), Left));
						}), 12.0f, FSlateColor(ScctHudStyle::TextDim()))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							Ui::Button(Ui::ToText(TEXT("GO BACK")), FOnClicked::CreateLambda([this] { Answer(false); return FReply::Handled(); }), false, Ui::Accent(), 130.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Ui::Button(Ui::ToText(TEXT("KEEP")), FOnClicked::CreateLambda([this] { Answer(true); return FReply::Handled(); }), true, Ui::Accent(), 130.0f)
						]
					], FMargin(30.0f, 26.0f), 0.85f)
			]
		]
	];
}

void SScctKeepDisplay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bFocusTaken && FSlateApplication::IsInitialized())
	{
		bFocusTaken = true;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
	if (!bAnswered && FPlatformTime::Seconds() >= Deadline)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Display: not kept in time"));
		Answer(false);
	}
}

FReply SScctKeepDisplay::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Answer(false);
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		Answer(true);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SScctKeepDisplay::Answer(bool bKeep)
{
	if (bAnswered)
	{
		return;
	}
	bAnswered = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("Display: %s"), bKeep ? TEXT("kept") : TEXT("going back"));

	const TSharedRef<SScctKeepDisplay> Self = SharedThis(this);
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Self);
	}
	if (const TSharedPtr<SWidget> Before = FocusBefore.Pin(); Before.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Before, EFocusCause::SetDirectly);
	}
	(bKeep ? OnKeep : OnRevert).ExecuteIfBound();
}
