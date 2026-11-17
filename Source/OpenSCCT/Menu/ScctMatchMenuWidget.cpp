#include "Menu/ScctMatchMenuWidget.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Menu/SScctConfirm.h"
#include "Menu/SScctGearPage.h"
#include "Menu/SScctOptionsPage.h"
#include "Menu/SScctStrategicMapPage.h"
#include "Menu/ScctMenuStyle.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

/**
 * The layers of the match overlay. Nothing here takes the mouse while the menu is closed: the banner and the
 * results only show.
 */
class SScctMatchOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMatchOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		/** The level presents an online round (it was opened with the online option). */
		SLATE_ARGUMENT(bool, bOnlineLevel)
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnLeave)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Online = InArgs._Online;
		bOnlineLevel = InArgs._bOnlineLevel;
		OnResume = InArgs._OnResume;
		OnLeave = InArgs._OnLeave;
		const bool bOnline = bOnlineLevel;

		ChildSlot
		[
			SAssignNew(Root, SOverlay)
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

			// The round's results.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(620.0f)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bResultsShown && !bMenuOpen ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
				[
					Ui::Glass(SAssignNew(ResultRows, SVerticalBox))
				]
			]

			// The in-match menu.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(320.0f)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bMenuOpen && !Options.IsValid() && !bGearOnly && !bMapOnly ? EVisibility::Visible : EVisibility::Collapsed; }))
				[
					Ui::Glass(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ Ui::Title(Ui::ToText(TEXT("MENU")), 22.0f) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							Ui::Button(Ui::ToText(TEXT("RESUME")), FOnClicked::CreateLambda([this] { OnResume.ExecuteIfBound(); return FReply::Handled(); }), true)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							Ui::Button(Ui::ToText(TEXT("CHANGE GEAR")), FOnClicked::CreateLambda([this] { ShowGear(-1, false); return FReply::Handled(); }))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
							[
								Ui::Button(Ui::ToText(TEXT("STRATEGIC MAP")), FOnClicked::CreateLambda([this] { ShowStrategicMap(false); return FReply::Handled(); }))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
							[
								Ui::Button(Ui::ToText(TEXT("OPTIONS")), FOnClicked::CreateLambda([this] { ShowOptions(NAME_None); return FReply::Handled(); }))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							Ui::Button(Ui::ToText(bOnline ? TEXT("LEAVE THE SERVER") : TEXT("MAIN MENU")), FOnClicked::CreateLambda([this] { AskLeave(); return FReply::Handled(); }))
						])
				]
			]

			// The Options screen, in place of the menu while it is open; the match goes on underneath.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SAssignNew(OptionsBox, SBox)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bMenuOpen && Options.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; }))
			]
		];
	}

	void ShowOptions(FName Page)
	{
		// The viewport keeps the keyboard (the menu's keys are read from it): Escape reaches the screen through Back.
		SAssignNew(Options, SScctOptionsPage)
			.Online(Online)
			.InitialPage(Page)
			.bTakeKeyboard(false)
			.OnClose(FSimpleDelegate::CreateLambda([this] { CloseOptions(); }));
		OptionsBox->SetContent(Options.ToSharedRef());
	}

	void CloseOptions()
	{
		Options.Reset();
		OptionsBox->SetContent(SNullWidget::NullWidget);
	}

	/**
	 * The Equipment page over the menu. Opened by the ChangeGear binding (bOnly) its closing goes back to the game;
	 * from the menu's CHANGE GEAR it goes back to the menu. APPLY goes back to the game either way: the server's
	 * answer is the HUD's message line.
	 */
	TSharedPtr<SScctGearPage> ShowGear(int32 Side, bool bOnly)
	{
		bGearOnly = bOnly;
		return SScctGearPage::Open(Root.ToSharedRef(), Side, EScctGearMenuContext::Match, false,
			FSimpleDelegate::CreateLambda([this] { if (bMenuOpen) { OnResume.ExecuteIfBound(); } }),
			FSimpleDelegate::CreateLambda([this] { if (bGearOnly && bMenuOpen) { OnResume.ExecuteIfBound(); } bGearOnly = false; }));
	}

	TSharedPtr<SScctGearPage> GetGear() const { return SScctGearPage::FindOpen(Root.ToSharedRef()); }

	void CloseGear()
	{
		if (const TSharedPtr<SScctGearPage> Gear = GetGear())
		{
			Gear->Close();
		}
		bGearOnly = false;
	}

	/**
	 * The strategic map over the menu. Opened by the DisplayStrategicMap binding (bOnly) its closing goes back to the
	 * game; from the menu's STRATEGIC MAP it goes back to the menu.
	 */
	TSharedPtr<SScctStrategicMapPage> ShowStrategicMap(bool bOnly)
	{
		bMapOnly = bOnly;
		const TSharedPtr<SScctStrategicMapPage> Page = SScctStrategicMapPage::Open(Root.ToSharedRef(), EScctMapContext::Match, FString(), false,
			FSimpleDelegate::CreateLambda([this] { if (bMapOnly && bMenuOpen) { OnResume.ExecuteIfBound(); } bMapOnly = false; }));
		if (!Page.IsValid())
		{
			bMapOnly = false;
		}
		return Page;
	}

	TSharedPtr<SScctStrategicMapPage> GetStrategicMap() const { return SScctStrategicMapPage::FindOpen(Root.ToSharedRef()); }

	void CloseStrategicMap()
	{
		if (const TSharedPtr<SScctStrategicMapPage> Map = GetStrategicMap())
		{
			Map->Close();
		}
		bMapOnly = false;
	}

	/** The menu's last button asks first: leaving a match cannot be taken back. */
	void AskLeave()
	{
		SScctConfirm::Open(Root.ToSharedRef(), bOnlineLevel ? TEXT("Leave the server?") : TEXT("Leave the match and go back to the main menu?"),
			TEXT("LEAVE"), TEXT("STAY"), FSimpleDelegate::CreateLambda([this] { OnLeave.ExecuteIfBound(); }));
	}

	/** Escape or the menu key while the menu is open: answers an open question, or leaves the Options screen. False when neither is open. */
	bool Back()
	{
		if (const TSharedPtr<SScctConfirm> Open = SScctConfirm::FindOpen(Root.ToSharedRef()))
		{
			Open->AnswerNo();
			return true;
		}
		if (const TSharedPtr<SScctGearPage> Gear = GetGear())
		{
			Gear->RequestBack();
			return true;
		}
		if (const TSharedPtr<SScctStrategicMapPage> Map = GetStrategicMap())
		{
			Map->Close();
			return true;
		}
		if (Options.IsValid())
		{
			// Keep the screen alive through its own callback, which may drop it.
			const TSharedPtr<SScctOptionsPage> Screen = Options;
			Screen->RequestBack();
			return true;
		}
		return false;
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		const UScctOnlineSubsystem* Visit = Online.Get();
		const bool bShow = bOnlineLevel && Visit != nullptr && Visit->GetState() == EScctOnlineState::Results;
		if (bShow && (!bResultsShown || Visit->GetRevision() != SeenRevision))
		{
			SeenRevision = Visit->GetRevision();
			FillResults(*Visit);
		}
		bResultsShown = bShow && ResultRows->NumSlots() > 0;
	}

	TSharedRef<SOverlay> GetRoot() const { return Root.ToSharedRef(); }

	bool bMenuOpen = false;

	/** The Equipment page was opened on its own (the ChangeGear binding), not from the menu. */
	bool bGearOnly = false;

	/** The strategic map was opened on its own (the DisplayStrategicMap binding), not from the menu. */
	bool bMapOnly = false;

private:
	EVisibility GetBannerVisibility() const
	{
		return bMenuOpen || bResultsShown || GetBannerText().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
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

	void FillResults(const UScctOnlineSubsystem& Visit)
	{
		ResultRows->ClearChildren();
		FScctOnlineResults Results;
		TArray<FScctOnlineResultRow> Rows;
		if (!Visit.GetResults(Results, Rows))
		{
			return;
		}

		const FString Winner = Results.WinningTeam == 0 ? TEXT("THE SPIES WIN") : Results.WinningTeam == 1 ? TEXT("THE MERCENARIES WIN") : TEXT("ROUND OVER");
		ResultRows->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(Ui::ToText(Winner))
			.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 26.0f, 220))
			.ColorAndOpacity(ScctHudStyle::Accent(Results.WinningTeam))
		];
		const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
		ResultRows->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 14.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([Weak, GamesLeft = Results.GamesLeft]
			{
				const int32 Left = Weak.IsValid() ? FMath::CeilToInt32(Weak->GetStatus().Countdown) : 0;
				return FText::FromString(FString::Printf(TEXT("%s in %d s"), GamesLeft > 0 ? TEXT("Next game") : TEXT("Back to the lobby"), Left));
			}), 11.5f, FSlateColor(ScctHudStyle::TextDim()))
		];
		ResultRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Ui::Rule() ];

		Rows.StableSort([](const FScctOnlineResultRow& A, const FScctOnlineResultRow& B) { return A.Team != B.Team ? A.Team < B.Team : A.Score > B.Score; });
		const auto Cell = [](const FString& String, const FLinearColor& Colour, float Width) -> TSharedRef<SWidget>
		{
			return SNew(SBox).WidthOverride(Width).HAlign(HAlign_Right)[ Ui::Text(Ui::ToText(String), 12.5f, FSlateColor(Colour)) ];
		};
		const auto Line = [&](const FString& Name, const FString& Score, const FString& Kills, const FString& Deaths, const FLinearColor& Colour)
		{
			ResultRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[ Ui::Text(Ui::ToText(Name), 12.5f, FSlateColor(Colour)) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Cell(Score, Colour, 80.0f) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Cell(Kills, Colour, 80.0f) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Cell(Deaths, Colour, 80.0f) ]
			];
		};
		Line(TEXT("PLAYER"), TEXT("SCORE"), TEXT("KILLS"), TEXT("DEATHS"), ScctHudStyle::TextFaint());
		for (const FScctOnlineResultRow& Row : Rows)
		{
			Line(Row.Name, FString::FromInt(Row.Score), FString::FromInt(Row.Kills), FString::FromInt(Row.Deaths),
				Row.Id == Visit.GetStatus().LocalId ? ScctHudStyle::Accent(Row.Team) : ScctHudStyle::Text());
		}
	}

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SOverlay> Root;
	TSharedPtr<SBox> OptionsBox;
	TSharedPtr<SScctOptionsPage> Options;
	FSimpleDelegate OnResume;
	FSimpleDelegate OnLeave;
	TSharedPtr<SVerticalBox> ResultRows;
	bool bOnlineLevel = false;
	bool bResultsShown = false;
	int32 SeenRevision = -1;
};

TSharedRef<SWidget> UScctMatchMenuWidget::RebuildWidget()
{
	const UWorld* World = GetWorld();
	SAssignNew(Overlay, SScctMatchOverlay)
		.Online(UScctOnlineSubsystem::Get(this))
		.bOnlineLevel(World != nullptr && World->URL.HasOption(UScctOnlineSubsystem::OnlineOption))
		.OnResume(FSimpleDelegate::CreateWeakLambda(this, [this]
		{
			SetMenuOpen(false);
			OnResumed.ExecuteIfBound();
		}))
		.OnLeave(FSimpleDelegate::CreateUObject(this, &UScctMatchMenuWidget::LeaveMatch));
	Overlay->bMenuOpen = bMenuOpen;
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
	if (!Overlay.IsValid())
	{
		return;
	}
	Overlay->bMenuOpen = bOpen;
	if (!bOpen)
	{
		// Closed from outside (the game mode): what the Options screen had not applied is dropped with it.
		Overlay->CloseOptions();
		Overlay->CloseGear();
		Overlay->CloseStrategicMap();
		if (const TSharedPtr<SScctConfirm> Open = SScctConfirm::FindOpen(Overlay->GetRoot()))
		{
			Open->AnswerNo();
		}
		return;
	}

	// -ScctOptions / -ScctConfirmTest: the first time the menu opens (with -ScctMatchMenu), for screenshots.
	static bool bUnattendedDone = false;
	if (!bUnattendedDone)
	{
		bUnattendedDone = true;
		FString OptionsPage;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctOptions="), OptionsPage))
		{
			ShowOptions(FName(*OptionsPage));
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("ScctConfirmTest")))
		{
			Overlay->AskLeave();
		}
	}
}

void UScctMatchMenuWidget::ShowOptions(FName Page)
{
	if (Overlay.IsValid())
	{
		Overlay->ShowOptions(Page);
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
		OnOpenRequested.ExecuteIfBound();
	}
	if (!bMenuOpen)
	{
		return false;
	}
	return Overlay->ShowGear(Side, !bWasOpen).IsValid();
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
		OnOpenRequested.ExecuteIfBound();
	}
	if (!bMenuOpen)
	{
		return false;
	}
	const bool bOpened = Overlay->ShowStrategicMap(!bWasOpen).IsValid();
	if (!bOpened && !bWasOpen)
	{
		// Nothing to show: the menu that was opened for it closes again.
		Overlay->bMapOnly = false;
		OnResumed.ExecuteIfBound();
	}
	return bOpened;
}

TSharedPtr<SScctStrategicMapPage> UScctMatchMenuWidget::GetStrategicMapPage() const
{
	return Overlay.IsValid() ? Overlay->GetStrategicMap() : nullptr;
}

TSharedPtr<SScctGearPage> UScctMatchMenuWidget::GetGearPage() const
{
	return Overlay.IsValid() ? Overlay->GetGear() : nullptr;
}

bool UScctMatchMenuWidget::Back()
{
	return Overlay.IsValid() && Overlay->Back();
}
