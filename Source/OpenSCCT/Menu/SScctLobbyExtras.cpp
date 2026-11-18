#include "Menu/SScctLobbyExtras.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuPage.h"
#include "Menu/ScctRankIcons.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Online/ScctLobbyExtras.h"
#include "OpenSCCT.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The dim over the lobby behind a dialog. */
	TSharedRef<SWidget> LobbyExtrasBackdrop(const TSharedRef<SWidget>& Content, float Width, float Height)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.62f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(24.0f))
			[
				SNew(SBox).WidthOverride(Width).MaxDesiredHeight(Height)
				[
					ScctUi::Panel(Content, FMargin(28.0f, 22.0f), 0.9f)
				]
			];
	}

	TSharedRef<SWidget> LobbyExtrasWords(const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Colour)
	{
		return SNew(STextBlock).Font(Font).ColorAndOpacity(Colour).AutoWrapText(true).Text(FText::FromString(Text))
			.Visibility(Text.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
	}

	/**
	 * A picture drawn whole in its box (kept to its shape and centred, not cropped), with place numbers over it at
	 * U across and V down of the drawn picture (BriefingPanel.PlanMarks); a dark plate when there is no picture.
	 */
	class SScctBriefingPicture : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctBriefingPicture) {}
			SLATE_ARGUMENT(const FSlateBrush*, Brush)
			SLATE_ARGUMENT(TArray<FScctLobbyBriefingMark>, Marks)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Brush = InArgs._Brush;
			Marks = InArgs._Marks;
			SetCanTick(false);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(320.0f, 240.0f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 LayerId,
			const FWidgetStyle&, bool) const override
		{
			static const FSlateRoundedBoxBrush Plate(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), ScctUi::Corner, FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);
			const FVector2f Size = Geometry.GetLocalSize();
			if (Brush == nullptr || Brush->ImageSize.X <= 0.0 || Brush->ImageSize.Y <= 0.0)
			{
				FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(), &Plate);
				return LayerId + 1;
			}
			const FVector2f Image(Brush->ImageSize.X, Brush->ImageSize.Y);
			const float Scale = FMath::Min(Size.X / Image.X, Size.Y / Image.Y);
			const FVector2f Drawn = Image * Scale;
			const FVector2f Origin = (Size - Drawn) * 0.5f;
			FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(Drawn, FSlateLayoutTransform(Origin)), Brush);
			if (Marks.IsEmpty())
			{
				return LayerId + 1;
			}
			FSlateFontInfo Font = ScctUi::Body(true);
			Font.Size = FMath::Max(10.0f, Drawn.X / 24.0f);
			Font.OutlineSettings.OutlineSize = 2;
			Font.OutlineSettings.OutlineColor = FLinearColor::Black;
			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			for (const FScctLobbyBriefingMark& Mark : Marks)
			{
				const FString Number = FString::FromInt(Mark.Number);
				const FVector2f Extent(Measure->Measure(Number, Font));
				const FVector2f At = Origin + FVector2f(Mark.U * Drawn.X, Mark.V * Drawn.Y) - Extent * 0.5f;
				FSlateDrawElement::MakeText(Out, LayerId + 1, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(At)), Number, Font,
					ESlateDrawEffect::None, ScctUi::Accent());
			}
			return LayerId + 2;
		}

	private:
		const FSlateBrush* Brush = nullptr;
		TArray<FScctLobbyBriefingMark> Marks;
	};

	/** The dialogs open over a lobby layer; each knows how to close itself. */
	class SScctLobbyExtrasDialog : public SCompoundWidget
	{
	public:
		virtual void Close()
		{
			if (const TSharedPtr<SOverlay> Owner = Layer.Pin())
			{
				Owner->RemoveSlot(AsShared());
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby closes the %s"), *What);
		}

		void FocusFirst()
		{
			if (FirstFocus.IsValid())
			{
				FSlateApplication::Get().SetAllUserFocus(FirstFocus, EFocusCause::SetDirectly);
			}
		}

	protected:
		TWeakPtr<SOverlay> Layer;
		TSharedPtr<SWidget> FirstFocus;
		FString What;
	};

	// ---- the match scoreboard ------------------------------------------------------------------------------------

	class SScctRoundScoreboardPanel : public SScctLobbyExtrasDialog
	{
	public:
		SLATE_BEGIN_ARGS(SScctRoundScoreboardPanel) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<SOverlay>& InLayer, FScctLastScoreboard&& InBoard)
		{
			Layer = InLayer;
			What = TEXT("match scoreboard");
			Board = MoveTemp(InBoard);
			// The breakdown opens on the local player, else the first row.
			Picked = FMath::Max(0, Board.Rows.IndexOfByPredicate([](const FScctLobbyScoreRow& Row) { return (Row.Flags & EScctLobbyScoreRowFlags::Local) != 0; }));

			const TSharedRef<SVerticalBox> Tables = SNew(SVerticalBox);
			// The local player's side first (the rows come in that order).
			const int32 First = Board.Rows.Num() > 0 && Board.Rows[0].Team == 1 ? 1 : 0;
			Tables->AddSlot().AutoHeight()[ TeamTable(First) ];
			Tables->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)[ TeamTable(1 - First) ];
			if (!Board.Spectators.IsEmpty())
			{
				Tables->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[ LobbyExtrasWords(Board.Spectators, ScctUi::Small(), ScctHudStyle::TextDim()) ];
			}
			if (Board.Rows.IsEmpty())
			{
				Tables->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[ LobbyExtrasWords(TEXT("No player scores were recorded."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
			}

			TArray<FString> Choices;
			for (const FScctLobbyScoreRow& Row : Board.Rows)
			{
				Choices.Add(FString::Printf(TEXT("%s · %s%s"), Row.Name, Row.Team == 0 ? TEXT("Spy") : TEXT("Mercenary"), (Row.Flags & EScctLobbyScoreRowFlags::LeftEarly) != 0 ? TEXT(" · Left early") : TEXT("")));
			}
			if (Choices.Num() > 0)
			{
				Tables->AddSlot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 6.0f)[ ScctUi::Label(TEXT("PLAYER BREAKDOWN"), ScctHudStyle::Text(), 13.0f) ];
				Tables->AddSlot().AutoHeight()
				[
					SNew(SScctStepper)
					.Label(FText::FromString(TEXT("Player")))
					.Description(FText::FromString(TEXT("Whose round totals the breakdown shows.")))
					.Options(Choices)
					.Get([this] { return Picked; })
					.Set([this](int32 Value) { Picked = Value; FillDetails(); })
				];
				Tables->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[ SAssignNew(DetailBox, SVerticalBox) ];
				Tables->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					LobbyExtrasWords(TEXT("Categories overlap: a headshot can also be a weapon kill. Neutralisations count completed knockouts and enemy kills; stuns and flashes are separate. — means not recorded."),
						ScctUi::Small(), ScctHudStyle::TextDim())
				];
			}

			TSharedPtr<SScctButton> CloseButton;
			ChildSlot
			[
				LobbyExtrasBackdrop(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Label(TEXT("MATCH SCOREBOARD"), ScctUi::Accent(), 12.0f) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)[ LobbyExtrasWords(Board.Title, ScctUi::H2(), ScctHudStyle::Text()) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[ LobbyExtrasWords(Board.Metadata, ScctUi::Small(), ScctHudStyle::TextDim()) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 12.0f)[ LobbyExtrasWords(Board.Timing, ScctUi::Small(), ScctHudStyle::TextDim()) ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SScrollBox)
						.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
						+ SScrollBox::Slot()[ Tables ]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 14.0f, 0.0f, 0.0f)
					[
						SAssignNew(CloseButton, SScctButton)
						.Look(EScctButtonLook::Primary)
						.MinWidth(160.0f)
						.Text(FText::FromString(TEXT("CLOSE")))
						.Description(FText::FromString(TEXT("Back to the lobby.")))
						.OnPressed(FSimpleDelegate::CreateLambda([this] { Close(); }))
					], 1180.0f, 820.0f)
			];
			FirstFocus = CloseButton;
			FillDetails();
		}

	private:
		TSharedRef<SWidget> Cell(const FString& Text, float Width, const FLinearColor& Colour, bool bRight, const FSlateFontInfo& Font)
		{
			return SNew(SBox).WidthOverride(Width)
				[
					SNew(STextBlock).Font(Font).ColorAndOpacity(Colour).Justification(bRight ? ETextJustify::Right : ETextJustify::Left)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text(FText::FromString(Text))
				];
		}

		/** One side's table: SPIES · n, the column heads, a line per player (Rank, Player, the side's columns). */
		TSharedRef<SWidget> TeamTable(int32 Side)
		{
			const FLinearColor SideColour = ScctHudStyle::Accent(Side);
			int32 Count = 0;
			for (const FScctLobbyScoreRow& Row : Board.Rows) { Count += Row.Team == Side ? 1 : 0; }
			const TArray<FString>& Columns = Board.Columns[Side];
			constexpr float RankWidth = 96.0f;
			constexpr float ColumnWidth = 100.0f;
			const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				ScctUi::Label(FString::Printf(TEXT("%s  ·  %d"), Side == 0 ? TEXT("SPIES") : TEXT("MERCENARIES"), Count), SideColour, 13.0f)
			];
			const TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox);
			Head->AddSlot().AutoWidth()[ Cell(TEXT("Rank"), RankWidth, ScctHudStyle::TextDim(), false, ScctUi::Caps(11.0f)) ];
			Head->AddSlot().FillWidth(1.0f)[ SNew(STextBlock).Font(ScctUi::Caps(11.0f)).ColorAndOpacity(ScctHudStyle::TextDim()).Text(FText::FromString(TEXT("Player"))) ];
			for (int32 Column = 0; Column < Columns.Num(); ++Column)
			{
				Head->AddSlot().AutoWidth()
				[
					SNew(SBox).ToolTipText(FText::FromString(Board.ColumnTips[Side].IsValidIndex(Column) ? Board.ColumnTips[Side][Column] : FString()))
					[
						Cell(Columns[Column], ColumnWidth, ScctHudStyle::TextDim(), true, ScctUi::Caps(11.0f))
					]
				];
			}
			Box->AddSlot().AutoHeight().Padding(10.0f, 2.0f)[ Head ];
			for (int32 Index = 0; Index < Board.Rows.Num(); ++Index)
			{
				const FScctLobbyScoreRow& Row = Board.Rows[Index];
				if (Row.Team != Side)
				{
					continue;
				}
				const bool bLocal = (Row.Flags & EScctLobbyScoreRowFlags::Local) != 0;
				const bool bLeft = (Row.Flags & EScctLobbyScoreRowFlags::LeftEarly) != 0;
				const FLinearColor TextColour = bLeft ? ScctHudStyle::TextDim() : ScctHudStyle::Text();
				const FString Badge = ScctLobbyExtras::RankBadge(Row.Rank);
				FString Name = Row.Name;
				if ((Row.Flags & EScctLobbyScoreRowFlags::Bot) != 0) { Name += TEXT("  (bot)"); }
				if (bLeft) { Name += TEXT("  · left early"); }
				const TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
				Line->AddSlot().AutoWidth()
				[
					SNew(SBox).WidthOverride(RankWidth).ToolTipText(FText::FromString(ScctLobbyExtras::RankWords(Row.Rank)))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, Badge.IsEmpty() ? 0.0f : 5.0f, 0.0f)
						[
							ScctRankIcons::Icon(ScctRankIcons::ForPacked(Row.Rank, 18.0f), 18.0f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(Badge.IsEmpty() ? ScctHudStyle::TextDim() : ScctLobbyExtras::RankColour(Row.Rank))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text(FText::FromString(Badge.IsEmpty() ? FString(TEXT("—")) : Badge))
						]
					]
				];
				Line->AddSlot().FillWidth(1.0f)
				[
					SNew(STextBlock).Font(ScctUi::Body(bLocal)).ColorAndOpacity(TextColour).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text(FText::FromString(Name))
				];
				const TArray<FString>& Cells = Board.Cells.IsValidIndex(Index) ? Board.Cells[Index] : TArray<FString>();
				for (int32 Column = 0; Column < Columns.Num(); ++Column)
				{
					Line->AddSlot().AutoWidth()[ Cell(Cells.IsValidIndex(Column) ? Cells[Column] : FString(), ColumnWidth, TextColour, true, ScctUi::Small()) ];
				}
				Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(bLocal ? SideColour.CopyWithNewOpacity(0.16f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.04f))
					.Padding(FMargin(10.0f, 6.0f))
					[
						Line
					]
				];
			}
			if (Count == 0)
			{
				Box->AddSlot().AutoHeight().Padding(10.0f, 4.0f)[ LobbyExtrasWords(TEXT("Nobody played on this side."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
			}
			return Box;
		}

		/** The picked player's round totals, by group (RoundScoreboardModel.Details). */
		void FillDetails()
		{
			if (!DetailBox.IsValid())
			{
				return;
			}
			DetailBox->ClearChildren();
			FString Group;
			for (const FScctScoreDetail& Detail : ScctLobbyExtras::ReadDetails(Picked))
			{
				if (Detail.Group != Group)
				{
					Group = Detail.Group;
					DetailBox->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)[ ScctUi::Label(Group.ToUpper(), ScctUi::Accent(), 11.0f) ];
				}
				DetailBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).Text(FText::FromString(Detail.Label)) ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Detail.Value)) ]
				];
			}
		}

		FScctLastScoreboard Board;
		TSharedPtr<SVerticalBox> DetailBox;
		int32 Picked = 0;
	};

	// ---- the briefing --------------------------------------------------------------------------------------------

	class SScctBriefingPanel : public SScctLobbyExtrasDialog
	{
	public:
		SLATE_BEGIN_ARGS(SScctBriefingPanel) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<SOverlay>& InLayer, FScctLobbyBriefing&& InBriefing)
		{
			Layer = InLayer;
			What = TEXT("briefing");
			Briefing = MoveTemp(InBriefing);
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const FSlateBrush* Picture = Menu != nullptr ? Menu->GetPicture(Briefing.Picture) : nullptr;
			const FSlateBrush* Plan = Menu != nullptr ? Menu->GetPicture(Briefing.Plan) : nullptr;
			const FString NoBriefing = TEXT("This map has no briefing for this mode.");

			TSharedPtr<SScctButton> FlipButton;
			ChildSlot
			[
				LobbyExtrasBackdrop(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Label(TEXT("BRIEFING"), ScctUi::Accent(), 12.0f) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 14.0f)[ LobbyExtrasWords(Briefing.Title, ScctUi::H2(), ScctHudStyle::Text()) ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(Pages, SWidgetSwitcher)
						// Page 1: the level picture beside the main objective, the mode's objective name and the map's briefing.
						+ SWidgetSwitcher::Slot()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.7f).Padding(0.0f, 0.0f, 18.0f, 0.0f)
							[
								SNew(SScctBriefingPicture).Brush(Picture)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)[ ScctUi::Label(Briefing.PageTitle, ScctUi::Accent(), 14.0f) ]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
									[
										LobbyExtrasWords(Briefing.Objective.IsEmpty() ? NoBriefing : Briefing.Objective, ScctUi::Body(), ScctHudStyle::Text())
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ LobbyExtrasWords(Briefing.ObjectiveName, ScctUi::Body(true), ScctUi::Accent()) ]
									+ SVerticalBox::Slot().AutoHeight()[ LobbyExtrasWords(Briefing.MapBrief, ScctUi::Body(), ScctHudStyle::Text()) ]
								]
							]
						]
						// Page 2: the plan with its place numbers, the mode's briefing and the legend.
						+ SWidgetSwitcher::Slot()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.7f).Padding(0.0f, 0.0f, 18.0f, 0.0f)
							[
								SNew(SScctBriefingPicture).Brush(Plan).Marks(Briefing.Marks)
							]
							+ SHorizontalBox::Slot().FillWidth(0.6f).Padding(0.0f, 0.0f, 18.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()[ LobbyExtrasWords(Briefing.ModeBrief.IsEmpty() ? NoBriefing : Briefing.ModeBrief, ScctUi::Body(), ScctHudStyle::Text()) ]
							]
							+ SHorizontalBox::Slot().FillWidth(0.4f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()[ LobbyExtrasWords(Briefing.Legend, ScctUi::Small(), ScctHudStyle::TextDim()) ]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SAssignNew(FlipButton, SScctButton)
							.Look(EScctButtonLook::Primary)
							.MinWidth(160.0f)
							.Text_Lambda([this] { return FText::FromString(Page == 0 ? TEXT("NEXT") : TEXT("PREVIOUS")); })
							.Description(FText::FromString(TEXT("The other page of the briefing: the main objective, or the plan of the map with the mode's briefing.")))
							.OnPressed(FSimpleDelegate::CreateLambda([this] { Flip(); }))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SScctButton)
							.MinWidth(160.0f)
							.Text(FText::FromString(TEXT("CLOSE")))
							.Description(FText::FromString(TEXT("Back to the lobby.")))
							.OnPressed(FSimpleDelegate::CreateLambda([this] { Close(); }))
						]
					], 1260.0f, 760.0f)
			];
			FirstFocus = FlipButton;
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby briefing '%s': objective %d chars, map brief %d, mode brief %d, legend %d, picture '%s', plan '%s', %d place number(s)"),
				*Briefing.Title, Briefing.Objective.Len(), Briefing.MapBrief.Len(), Briefing.ModeBrief.Len(), Briefing.Legend.Len(),
				*FPaths::GetCleanFilename(Briefing.Picture), *FPaths::GetCleanFilename(Briefing.Plan), Briefing.Marks.Num());
		}

		/** NEXT / PREVIOUS. */
		void Flip()
		{
			Page = 1 - Page;
			Pages->SetActiveWidgetIndex(Page);
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby briefing shows page %d"), Page + 1);
		}

	private:
		FScctLobbyBriefing Briefing;
		TSharedPtr<SWidgetSwitcher> Pages;
		int32 Page = 0;
	};

	TSharedPtr<SScctLobbyExtrasDialog> LobbyExtrasOpenDialog(const TSharedPtr<SOverlay>& Layer)
	{
		FChildren* Children = Layer.IsValid() ? Layer->GetChildren() : nullptr;
		if (Children == nullptr)
		{
			return nullptr;
		}
		for (int32 Index = Children->Num() - 1; Index >= 0; --Index)
		{
			const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
			if (Child->GetType() == TEXT("SScctRoundScoreboardPanel") || Child->GetType() == TEXT("SScctBriefingPanel"))
			{
				return StaticCastSharedRef<SScctLobbyExtrasDialog>(Child);
			}
		}
		return nullptr;
	}
}

bool ScctLobbyPanels::OpenScoreboard(const TSharedPtr<SOverlay>& Layer)
{
	FScctLastScoreboard Board;
	if (!Layer.IsValid() || !ScctLobbyExtras::ReadLastScoreboard(Board))
	{
		return false;
	}
	Back(Layer);
	FString Lines;
	for (int32 Index = 0; Index < Board.Rows.Num(); ++Index)
	{
		const FScctLobbyScoreRow& Row = Board.Rows[Index];
		Lines += FString::Printf(TEXT("\n   %s '%s'%s rank '%s': %s"), Row.Team == 0 ? TEXT("spy") : TEXT("merc"), Row.Name,
			(Row.Flags & EScctLobbyScoreRowFlags::Local) != 0 ? TEXT(" (you)") : TEXT(""), *ScctLobbyExtras::RankBadge(Row.Rank),
			Board.Cells.IsValidIndex(Index) ? *FString::Join(Board.Cells[Index], TEXT(" | ")) : TEXT(""));
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby opens the match scoreboard: %s; %s; %s; %d row(s)%s"), *Board.Title, *Board.Metadata, *Board.Timing, Board.Rows.Num(), *Lines);
	const TSharedRef<SScctRoundScoreboardPanel> Panel = SNew(SScctRoundScoreboardPanel, Layer.ToSharedRef(), MoveTemp(Board));
	Layer->AddSlot()[ Panel ];
	Panel->FocusFirst();
	return true;
}

bool ScctLobbyPanels::OpenBriefing(const TSharedPtr<SOverlay>& Layer)
{
	FScctLobbyBriefing Briefing;
	if (!Layer.IsValid() || !ScctLobbyExtras::ReadBriefing(Briefing))
	{
		return false;
	}
	Back(Layer);
	const TSharedRef<SScctBriefingPanel> Panel = SNew(SScctBriefingPanel, Layer.ToSharedRef(), MoveTemp(Briefing));
	Layer->AddSlot()[ Panel ];
	Panel->FocusFirst();
	return true;
}

bool ScctLobbyPanels::Back(const TSharedPtr<SOverlay>& Layer)
{
	if (const TSharedPtr<SScctLobbyExtrasDialog> Dialog = LobbyExtrasOpenDialog(Layer))
	{
		Dialog->Close();
		return true;
	}
	return false;
}

bool ScctLobbyPanels::IsOpen(const TSharedPtr<SOverlay>& Layer)
{
	return LobbyExtrasOpenDialog(Layer).IsValid();
}
