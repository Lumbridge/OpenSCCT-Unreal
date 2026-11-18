#include "Menu/SScctArmouryOverlay.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctCareerPages.h"
#include "Menu/ScctMenuNav.h"
#include "OpenSCCT.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"

namespace
{
	const FName ArmouryOverlayType(TEXT("SScctArmouryOverlay"));
}

TSharedRef<SScctArmouryOverlay> SScctArmouryOverlay::Open(const TSharedRef<SOverlay>& Layer, const FScctMenuHost& Host, const FString& Where, FSimpleDelegate OnClosed)
{
	if (const TSharedPtr<SScctArmouryOverlay> Already = FindOpen(Layer))
	{
		return Already.ToSharedRef();
	}
	TSharedRef<SScctArmouryOverlay> Overlay = SNew(SScctArmouryOverlay, Host, Where).OnClosed(OnClosed);
	Overlay->Layer = Layer;
	Layer->AddSlot()[ Overlay ];
	UE_LOG(LogOpenSCCT, Log, TEXT("Armoury: opened over %s"), *Where);
	ScctUi::Play(EScctUiSound::Open);
	// The first control of the page, else the overlay itself, so Escape and the arrows arrive here.
	const TSharedPtr<SWidget> Focus = Overlay->Page.IsValid() ? Overlay->Page->GetDefaultFocus() : nullptr;
	FSlateApplication::Get().SetKeyboardFocus(Focus.IsValid() ? Focus : TSharedPtr<SWidget>(Overlay), EFocusCause::SetDirectly);
	return Overlay;
}

TSharedPtr<SScctArmouryOverlay> SScctArmouryOverlay::FindOpen(const TSharedRef<SOverlay>& Layer)
{
	FChildren* Children = Layer->GetChildren();
	for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
	{
		const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
		if (Child->GetType() == ArmouryOverlayType)
		{
			return StaticCastSharedRef<SScctArmouryOverlay>(Child);
		}
	}
	return nullptr;
}

bool SScctArmouryOverlay::Back(const TSharedPtr<SOverlay>& Layer)
{
	const TSharedPtr<SScctArmouryOverlay> Overlay = Layer.IsValid() ? FindOpen(Layer.ToSharedRef()) : nullptr;
	if (!Overlay.IsValid())
	{
		return false;
	}
	ScctUi::Play(EScctUiSound::Back);
	Overlay->Close();
	return true;
}

void SScctArmouryOverlay::Construct(const FArguments& InArgs, const FScctMenuHost& Host, const FString& InWhere)
{
	OnClosed = InArgs._OnClosed;
	Where = InWhere;

	// The page as the menus show it, without its links to the other career pages (they would leave what is underneath).
	FScctMenuHost PageHost = Host;
	PageHost.bInMatch = true;
	PageHost.Open = [](const FString&) {};
	PageHost.Back = [this] { Close(); };
	Page = ScctCareerPages::MakeArmoury(PageHost);

	static const FSlateColorBrush Cover(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			// Covers what is underneath, and takes the mouse so nothing behind is pressed.
			SNew(SImage).Image(&Cover).ColorAndOpacity(ScctUi::Ink(0.96f))
		]
		+ SOverlay::Slot().Padding(40.0f, 28.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(ScctUi::Caps(11.0f)).ColorAndOpacity(ScctUi::Accent()).Text(FText::FromString(Page->GetKicker().ToUpper()))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(ScctUi::H1()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Page->GetTitle().ToUpper()))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("BACK")))
					.Description(FText::FromString(FString::Printf(TEXT("Back to %s. Your look is saved as you pick it."), *Where)))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { Close(); }))
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).HAlign(HAlign_Center)[ Page.ToSharedRef() ]
		]
	];
}

FReply SScctArmouryOverlay::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Escape and B go on to the host, whose Back closes this page (ScctArmouryOverlay::Back).
	const int32 Direction = ScctUi::IsTyping() ? 0 : ScctMenuNav::TabDirection(InKeyEvent.GetKey());
	if (Direction != 0 && Page.IsValid() && Page->HasTabs())
	{
		Page->StepTab(Direction);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SScctArmouryOverlay::Close()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	const TSharedRef<SScctArmouryOverlay> Self = SharedThis(this);
	if (Page.IsValid())
	{
		Page->OnClosing();
	}
	if (const TSharedPtr<SOverlay> Host = Layer.Pin())
	{
		Host->RemoveSlot(Self);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Armoury: closed, back to %s"), *Where);
	OnClosed.ExecuteIfBound();
}
