#include "Menu/SScctPlayErrorPage.h"
#include "Menu/ScctMenuNav.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudStyle.h"
#include "InputCoreTypes.h"
#include "Menu/ScctMenuStyle.h"
#include "Runtime/ScctFeatures.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The page's backdrop: the level behind it has nothing left to show. */
	const FSlateColorBrush PlayErrorBackdropBrush(FLinearColor::White);
}

FString SScctPlayErrorPage::MapTitleOf(const FString& Map)
{
	if (Map.IsEmpty())
	{
		return FString();
	}
	if (const FScctPlayApi* Play = ScctFeature<FScctPlayApi>())
	{
		TCHAR Buffer[256];
		const int32 Length = Play->GetMapTitle(*Map, Buffer, UE_ARRAY_COUNT(Buffer));
		if (Length > 0 && Length < static_cast<int32>(UE_ARRAY_COUNT(Buffer)))
		{
			return FString(Buffer);
		}
	}
	return Map;
}

FString SScctPlayErrorPage::Describe(const FScctPlayError& Error, const FString& MapTitle)
{
	const FString Where = MapTitle.IsEmpty() ? FString() : FString::Printf(TEXT(" '%s'"), *MapTitle);
	switch (Error.Kind)
	{
	case EScctPlayErrorKind::StartFailed:
		return FString::Printf(TEXT("Could not start play%s: %s"), MapTitle.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on '%s'"), *MapTitle), *Error.Reason);
	case EScctPlayErrorKind::Disconnected:
		return FString::Printf(TEXT("Could not play%s: %s. Press Escape to leave."), *Where, *Error.Reason.TrimChar(TEXT('.')));
	case EScctPlayErrorKind::ServerStopped:
		return FString::Printf(TEXT("Server stopped: %s"), *Error.Reason);
	default:
		return Error.Reason;
	}
}

void SScctPlayErrorPage::Construct(const FArguments& InArgs, const FScctPlayError& Error)
{
	using namespace ScctHudStyle;
	OnLeave = InArgs._OnLeave;

	const FString MapTitle = MapTitleOf(Error.Map);
	const TCHAR* Heading = TEXT("PLAY STOPPED");
	switch (Error.Kind)
	{
	case EScctPlayErrorKind::StartFailed: Heading = TEXT("COULD NOT START PLAY"); break;
	case EScctPlayErrorKind::Disconnected: Heading = Error.bRejected ? TEXT("REFUSED BY THE SERVER") : TEXT("CONNECTION LOST"); break;
	case EScctPlayErrorKind::ServerStopped: Heading = TEXT("SERVER STOPPED"); break;
	default: break;
	}
	const FString LeaveLabel = InArgs._bOnline ? TEXT("LEAVE THE SERVER") : TEXT("LEAVE");

	const TSharedRef<SVerticalBox> Words = SNew(SVerticalBox);
	Words->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Heading))
		.Font(Label(11.0f))
		.ColorAndOpacity(Danger())
	];
	if (!MapTitle.IsEmpty())
	{
		Words->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(MapTitle.ToUpper()))
			.Font(Font(EWeight::Light, 38.0f, 160))
			.ColorAndOpacity(Text())
			.AutoWrapText(true)
		];
	}
	Words->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 30.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Describe(Error, MapTitle)))
		.Font(Font(EWeight::Regular, 15.0f))
		.ColorAndOpacity(Text())
		.AutoWrapText(true)
	];
	Words->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			ScctMenuStyle::Button(FText::FromString(LeaveLabel), FOnClicked::CreateSP(this, &SScctPlayErrorPage::OnLeaveClicked), false, Danger(), 220.0f)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("ESC  LEAVE")))
			.Font(Label(9.0f))
			.ColorAndOpacity(TextDim())
		]
	];

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(&PlayErrorBackdropBrush).ColorAndOpacity(FLinearColor(0.004f, 0.006f, 0.009f, 0.94f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16.0f)
		[
			SNew(SBox)
			.WidthOverride(760.0f)
			[
				ScctMenuStyle::Glass(Words, FMargin(40.0f, 34.0f), 0.8f)
			]
		]
	];
}

FReply SScctPlayErrorPage::OnLeaveClicked()
{
	Leave();
	return FReply::Handled();
}

FReply SScctPlayErrorPage::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (ScctMenuNav::IsBackKey(InKeyEvent.GetKey()))
	{
		Leave();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SScctPlayErrorPage::Leave()
{
	if (bLeft)
	{
		return;
	}
	bLeft = true;
	OnLeave.ExecuteIfBound();
}
