#include "Menu/ScctMatchMenuWidget.h"

#include "Engine/World.h"
#include "Menu/SScctMatchMenu.h"
#include "Menu/SScctRoundWaitList.h"
#include "Menu/ScctMenuStyle.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

/**
 * The layers of the match overlay: what the visit is waiting for (the banner, and while a network round loads the
 * server's loading picture under the list of players it waits for, Menu/SScctRoundWaitList.h), and the menu with the
 * round's results (Menu/SScctMatchMenu.h). Nothing here takes the mouse while the menu is closed: the banner, the
 * waiting list and the results only show.
 */
class SScctMatchOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMatchOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		/** The level presents an online round (it was opened with the online option). */
		SLATE_ARGUMENT(bool, bOnlineLevel)
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnLeave)
		SLATE_EVENT(FSimpleDelegate, OnWantsOpen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Online = InArgs._Online;
		bOnlineLevel = InArgs._bOnlineLevel;

		ChildSlot
		[
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)

			// What the visit is waiting for, while the round is not being played.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 150.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility(TAttribute<EVisibility>::CreateSP(this, &SScctMatchOverlay::GetBannerVisibility))
				[
					Ui::Glass(SNew(STextBlock)
						.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 18.0f, 220))
						.ColorAndOpacity(ScctHudStyle::Text())
						.Text(TAttribute<FText>::CreateSP(this, &SScctMatchOverlay::GetBannerText)), FMargin(30.0f, 14.0f))
				]
			]

			// The round's waiting list while a network round loads.
			+ SOverlay::Slot()
			[
				SAssignNew(WaitList, SScctRoundWaitList).Online(Online)
			]

			// The in-match menu and the round's results.
			+ SOverlay::Slot()
			[
				SAssignNew(Menu, SScctMatchMenu)
				.Online(InArgs._Online)
				.World(InArgs._World)
				.bOnlineLevel(InArgs._bOnlineLevel)
				.OnResume(InArgs._OnResume)
				.OnLeave(InArgs._OnLeave)
				.OnWantsOpen(InArgs._OnWantsOpen)
			]
		];
	}

	void SetMenuOpen(bool bOpen) { Menu->SetOpen(bOpen); }

private:
	EVisibility GetBannerVisibility() const
	{
		return Menu->IsOpen() || Menu->IsShowingResults() || WaitList->IsShowing() || GetBannerText().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
	}

	FText GetBannerText() const
	{
		const UScctOnlineSubsystem* Visit = Online.Get();
		if (!bOnlineLevel || Visit == nullptr)
		{
			return FText::GetEmpty();
		}
		switch (Visit->GetState())
		{
		case EScctOnlineState::Travelling: return Ui::ToText(TEXT("TRAVELLING"));
		case EScctOnlineState::Loading:
			return Ui::ToText(Visit->HasFlag(EScctOnlineStatusFlags::ReportedWaiting) ? TEXT("WAITING FOR THE OTHER PLAYERS") : TEXT("JOINING THE ROUND"));
		case EScctOnlineState::Failed: return Ui::ToText(TEXT("CONNECTION LOST"));
		default: return FText::GetEmpty();
		}
	}

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SScctMatchMenu> Menu;
	TSharedPtr<SScctRoundWaitList> WaitList;
	bool bOnlineLevel = false;
};

TSharedRef<SWidget> UScctMatchMenuWidget::RebuildWidget()
{
	UWorld* World = GetWorld();
	SAssignNew(Overlay, SScctMatchOverlay)
		.Online(UScctOnlineSubsystem::Get(this))
		.World(World)
		.bOnlineLevel(World != nullptr && World->URL.HasOption(UScctOnlineSubsystem::OnlineOption))
		.OnResume(FSimpleDelegate::CreateWeakLambda(this, [this]
		{
			SetMenuOpen(false);
			OnResumed.ExecuteIfBound();
		}))
		.OnLeave(FSimpleDelegate::CreateUObject(this, &UScctMatchMenuWidget::LeaveMatch))
		.OnWantsOpen(FSimpleDelegate::CreateWeakLambda(this, [this] { OnWantsOpen.ExecuteIfBound(); }));
	Overlay->SetMenuOpen(bMenuOpen);
	return Overlay.ToSharedRef();
}

void UScctMatchMenuWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Overlay.Reset();
}

void UScctMatchMenuWidget::LeaveMatch()
{
	UScctOnlineSubsystem* Visit = UScctOnlineSubsystem::Get(this);
	const UWorld* World = GetWorld();
	if (Visit == nullptr || World == nullptr)
	{
		return;
	}
	if (World->URL.HasOption(UScctOnlineSubsystem::OnlineOption))
	{
		// The front end opens by itself once the visit is over.
		Visit->Leave();
	}
	else
	{
		Visit->OpenFrontEnd();
	}
}

void UScctMatchMenuWidget::SetMenuOpen(bool bOpen)
{
	bMenuOpen = bOpen;
	if (Overlay.IsValid())
	{
		Overlay->SetMenuOpen(bOpen);
	}
}
