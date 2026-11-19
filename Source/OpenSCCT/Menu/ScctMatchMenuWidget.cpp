#include "Menu/ScctMatchMenuWidget.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctLoadingSubsystem.h"
#include "Menu/SScctGearPage.h"
#include "Menu/SScctMatchMenu.h"
#include "Menu/SScctPlayErrorPage.h"
#include "Menu/SScctRoundWaitList.h"
#include "OpenSCCT.h"
#include "Menu/SScctStrategicMapPage.h"
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
 * The layers of the match overlay: what the visit is waiting for (the loading page as a cover while travelling and
 * joining; once a network round waits for its players, the server's loading picture under the list of players it
 * waits for, Menu/SScctRoundWaitList.h, in its place), the menu with the round's results (Menu/SScctMatchMenu.h), and
 * the play error page over everything. Nothing here takes the mouse while the menu is closed and no error is shown:
 * the cover, the waiting list and the results only show.
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

			// What the visit is waiting for, while the round is not being played: the loading page (the map, the
			// side's tip, the map's picture) with the wait in place of "LOADING", over the level that has no round yet.
			+ SOverlay::Slot()
			[
				SAssignNew(CoverBox, SBox)
				.Visibility(EVisibility::Collapsed)
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

			// What stopped play, over everything (Menu/SScctPlayErrorPage.h).
			+ SOverlay::Slot()
			[
				SAssignNew(ErrorBox, SBox)
				.Visibility(EVisibility::Collapsed)
			]
		];
		World = InArgs._World;
		OnLeave = InArgs._OnLeave;
	}

	void SetMenuOpen(bool bOpen) { Menu->SetOpen(bOpen); }
	TSharedPtr<SScctMatchMenu> GetMenu() const { return Menu; }

	void ShowPlayError(const FScctPlayError& Error)
	{
		SAssignNew(ErrorPage, SScctPlayErrorPage, Error)
			.bOnline(bOnlineLevel)
			.OnLeave(OnLeave);
		ErrorBox->SetContent(ErrorPage.ToSharedRef());
		ErrorBox->SetVisibility(EVisibility::Visible);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocus(ErrorPage, EFocusCause::SetDirectly);
		}
	}

	void LeaveFromPlayError()
	{
		if (ErrorPage.IsValid())
		{
			ErrorPage->Leave();
		}
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		UpdateCover();
	}

private:
	/** What the visit waits for while this online level has no round to show; empty when nothing (or an error) is shown. */
	FString GetCoverStatus() const
	{
		const UScctOnlineSubsystem* Visit = Online.Get();
		// The waiting list (with the server's loading picture) takes the cover's place once it shows.
		if (!bOnlineLevel || Visit == nullptr || ErrorPage.IsValid() || (WaitList.IsValid() && WaitList->IsShowing()))
		{
			return FString();
		}
		switch (Visit->GetState())
		{
		case EScctOnlineState::Travelling: return TEXT("TRAVELLING");
		case EScctOnlineState::Loading:
			return Visit->HasFlag(EScctOnlineStatusFlags::ReportedWaiting) ? TEXT("WAITING FOR THE OTHER PLAYERS") : TEXT("JOINING THE ROUND");
		default: return FString();
		}
	}

	void UpdateCover()
	{
		const FString Status = GetCoverStatus();
		if (Status == CoverStatus)
		{
			return;
		}
		CoverStatus = Status;
		if (Status.IsEmpty())
		{
			CoverBox->SetContent(SNullWidget::NullWidget);
			CoverBox->SetVisibility(EVisibility::Collapsed);
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadingCover: the round is up, the cover is gone"));
			return;
		}

		const UScctOnlineSubsystem* Visit = Online.Get();
		const UWorld* Level = World.Get();
		UScctLoadingSubsystem* Loading = Level != nullptr && Level->GetGameInstance() != nullptr ? Level->GetGameInstance()->GetSubsystem<UScctLoadingSubsystem>() : nullptr;
		const FString Map = Visit->GetMapToLoad();
		if (Loading != nullptr && (Map != CoverMap || !bHasCoverPage))
		{
			// The same tip and picture all through one round's wait: only the status changes.
			const int32 Team = Visit->GetStatus().LocalTeam;
			const int32 Side = Team == 0 || Team == 1 ? Team : -1;
			const TCHAR* SideName = Side < 0 ? TEXT("SPECTATOR") : (Side == 1 ? TEXT("MERCENARY") : TEXT("SPY"));
			const FString Server = Visit->GetServerName().IsEmpty() ? FString(TEXT("ONLINE")) : Visit->GetServerName();
			CoverPage = Loading->DescribeRound(Map, Side, FString::Printf(TEXT("%s   /   %s"), *Server, SideName), Status);
			CoverMap = Map;
			bHasCoverPage = true;
		}
		CoverPage.Status = Status;
		CoverBox->SetContent(UScctLoadingSubsystem::MakePage(CoverPage));
		CoverBox->SetVisibility(EVisibility::HitTestInvisible);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctLoadingCover: %s on %s ('%s', tip \"%s\")"), *Status, *Map, *CoverPage.Title, *CoverPage.Tip.Left(80));
	}

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<SScctMatchMenu> Menu;
	TSharedPtr<SScctRoundWaitList> WaitList;
	TSharedPtr<SBox> CoverBox;
	TSharedPtr<SBox> ErrorBox;
	TSharedPtr<SScctPlayErrorPage> ErrorPage;
	FSimpleDelegate OnLeave;
	FScctLoadingPage CoverPage;
	FString CoverMap;
	FString CoverStatus;
	bool bHasCoverPage = false;
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
	SetMenuOpen(bMenuOpen);
	if (PlayErrorSerial != 0)
	{
		Overlay->ShowPlayError(ShownPlayError);
	}
	return Overlay.ToSharedRef();
}

void UScctMatchMenuWidget::ShowPlayError(const FScctPlayError& Error)
{
	if (Error.Kind == EScctPlayErrorKind::None || Error.Serial == PlayErrorSerial)
	{
		return;
	}
	PlayErrorSerial = Error.Serial;
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ShownPlayError = Error;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctPlayError: page shown: \"%s\""), *SScctPlayErrorPage::Describe(Error, SScctPlayErrorPage::MapTitleOf(Error.Map)));
	if (Overlay.IsValid())
	{
		Overlay->ShowPlayError(Error);
	}
}

void UScctMatchMenuWidget::LeaveFromPlayError()
{
	if (Overlay.IsValid())
	{
		Overlay->LeaveFromPlayError();
	}
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
	// This full-screen UMG layer stays mounted for round results even with the menu closed.
	// While passive, its Slate descendants must let the HUD editor underneath receive pointer events.
	SetVisibility(bOpen || IsShowingPlayError() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
	if (Overlay.IsValid())
	{
		Overlay->SetMenuOpen(bOpen);
	}
}

bool UScctMatchMenuWidget::IsShowingLocalResults() const
{
	return Overlay.IsValid() && Overlay->GetMenu()->IsShowingLocalResults();
}

void UScctMatchMenuWidget::TickResults(float DeltaSeconds)
{
	if (Overlay.IsValid())
	{
		Overlay->GetMenu()->TickResults(DeltaSeconds);
	}
}

bool UScctMatchMenuWidget::ShowGear(int32 Side)
{
	if (!Overlay.IsValid())
	{
		return false;
	}
	// The game mode opens the menu (the keys and the mouse go to it); the page then opens over it on its own.
	const bool bWasOpen = bMenuOpen;
	if (!bWasOpen)
	{
		OnWantsOpen.ExecuteIfBound();
	}
	if (!bMenuOpen)
	{
		return false;
	}
	const bool bOpened = Overlay->GetMenu()->ShowGear(Side, !bWasOpen).IsValid();
	if (!bOpened && !bWasOpen)
	{
		// Nothing to show: the menu that was opened for it closes again.
		OnResumed.ExecuteIfBound();
	}
	return bOpened;
}

bool UScctMatchMenuWidget::ShowStrategicMap()
{
	if (!Overlay.IsValid())
	{
		return false;
	}
	const bool bWasOpen = bMenuOpen;
	if (!bWasOpen)
	{
		OnWantsOpen.ExecuteIfBound();
	}
	if (!bMenuOpen)
	{
		return false;
	}
	const bool bOpened = Overlay->GetMenu()->ShowStrategicMap(!bWasOpen).IsValid();
	if (!bOpened && !bWasOpen)
	{
		OnResumed.ExecuteIfBound();
	}
	return bOpened;
}

bool UScctMatchMenuWidget::ShowArmoury()
{
	if (!Overlay.IsValid())
	{
		return false;
	}
	const bool bWasOpen = bMenuOpen;
	if (!bWasOpen)
	{
		OnWantsOpen.ExecuteIfBound();
	}
	if (!bMenuOpen)
	{
		return false;
	}
	const bool bOpened = Overlay->GetMenu()->ShowArmoury(!bWasOpen);
	if (!bOpened && !bWasOpen)
	{
		OnResumed.ExecuteIfBound();
	}
	return bOpened;
}

TSharedPtr<SScctGearPage> UScctMatchMenuWidget::GetGearPage() const
{
	return Overlay.IsValid() ? Overlay->GetMenu()->GetGear() : nullptr;
}

TSharedPtr<SScctStrategicMapPage> UScctMatchMenuWidget::GetStrategicMapPage() const
{
	return Overlay.IsValid() ? Overlay->GetMenu()->GetStrategicMap() : nullptr;
}
