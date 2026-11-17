#include "Menu/ScctMatchMenuWidget.h"

#include "Engine/World.h"
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
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bMenuOpen ? EVisibility::Visible : EVisibility::Collapsed; }))
				[
					Ui::Glass(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ Ui::Title(Ui::ToText(TEXT("MENU")), 22.0f) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							Ui::Button(Ui::ToText(TEXT("RESUME")), FOnClicked::CreateLambda([this] { OnResume.ExecuteIfBound(); return FReply::Handled(); }), true)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							Ui::Button(Ui::ToText(bOnline ? TEXT("LEAVE THE SERVER") : TEXT("MAIN MENU")), FOnClicked::CreateLambda([this] { OnLeave.ExecuteIfBound(); return FReply::Handled(); }))
						])
				]
			]
		];
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

	bool bMenuOpen = false;

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
	if (Overlay.IsValid())
	{
		Overlay->bMenuOpen = bOpen;
	}
}
