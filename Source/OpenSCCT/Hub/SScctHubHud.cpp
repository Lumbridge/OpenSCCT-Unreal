#include "Hub/SScctHubHud.h"

#include "Menu/ScctRankIcons.h"
#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Hub/ScctHubHud.h"
#include "Hud/ScctHudPainter.h"
#include "Hud/ScctHudStyle.h"
#include "Menu/ScctMenuStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	namespace Ui = ScctMenuStyle;
	using ScctHudStyle::EWeight;

	/** Chat lines stay this long on the panel while the composer is closed (seconds), then fade over a second. */
	constexpr double ChatShowSeconds = 20.0;

	/** Lines the chat panel shows. */
	constexpr int32 ChatLines = 8;

	/** The name tags' size at scale 1 (HubHud's label at 1.0, about 16 px at 1080p). */
	constexpr float TagSize = 12.0f;

	/** Name tags shrink and grow with distance (HubHud.DrawTags: 1.4 - d / 4500 within 0.6 to 1.2). */
	float TagScale(float Distance)
	{
		return FMath::Clamp(1.4f - Distance / 4500.0f, 0.6f, 1.2f);
	}

	const TCHAR* ChannelLabel(EScctHubChatChannel Channel)
	{
		switch (Channel)
		{
		case EScctHubChatChannel::Side: return TEXT("SIDE");
		case EScctHubChatChannel::Nearby: return TEXT("NEARBY");
		case EScctHubChatChannel::System: return TEXT("HUB");
		default: return TEXT("ALL");
		}
	}

	FString Time(int32 Ms)
	{
		return FString::Printf(TEXT("%d:%02d.%02d"), Ms / 60000, (Ms / 1000) % 60, (Ms / 10) % 100);
	}
}

void SScctHubHud::Construct(const FArguments& InArgs)
{
	Hud = InArgs._Hud;
	Model = InArgs._Model;
	check(Model.IsValid());
	SetVisibility(EVisibility::SelfHitTestInvisible);

	const TSharedPtr<FScctHubHudModel> M = Model;
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(ScctHudStyle::Margin, 0.0f, 0.0f, 250.0f)
		[
			SNew(SBox)
			.WidthOverride(560.0f)
			.Visibility_Lambda([M] { return M->bComposing ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				BuildComposer()
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.Visibility_Lambda([M] { return M->bPlayerList ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				BuildPlayerList()
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.Visibility_Lambda([M] { return M->bTravel ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				BuildTravel()
			]
		]
	];
}

// ---- the panels -------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctHubHud::BuildComposer()
{
	const TSharedPtr<FScctHubHudModel> M = Model;
	const TWeakPtr<FScctHubHud> WeakHud = Hud;
	return Ui::Glass(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([M] { return Ui::ToText(FString::Printf(TEXT("%s  >"), ChannelLabel(M->Channel))); })
			.Font(ScctHudStyle::Label(10.0f))
			.ColorAndOpacity_Lambda([M]
			{
				return FSlateColor(M->Channel == EScctHubChatChannel::Side ? ScctHudStyle::Accent(M->Team)
					: M->Channel == EScctHubChatChannel::Nearby ? ScctHudStyle::Good() : ScctHudStyle::Text());
			})
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SAssignNew(Composer, SEditableTextBox)
			.HintText(Ui::ToText(TEXT("Say something   (Tab: everyone / side / nearby, Enter: send, Escape: cancel)")))
			.Font(ScctHudStyle::Font(EWeight::Regular, 12.0f))
			.ClearKeyboardFocusOnCommit(false)
			.OnKeyDownHandler(this, &SScctHubHud::OnComposerKey)
			.OnTextCommitted_Lambda([this, WeakHud](const FText& Text, ETextCommit::Type Type)
			{
				TSharedPtr<FScctHubHud> Owner = WeakHud.Pin();
				if (!Owner.IsValid() || !Model->bComposing)
				{
					return;
				}
				if (Type == ETextCommit::OnEnter)
				{
					Owner->SendChat(Text.ToString());
					Composer->SetText(FText::GetEmpty());
					Owner->CloseComposer();
				}
			})
		], FMargin(14.0f, 8.0f));
}

FReply SScctHubHud::OnComposerKey(const FGeometry& Geometry, const FKeyEvent& Key)
{
	TSharedPtr<FScctHubHud> Owner = Hud.Pin();
	if (!Owner.IsValid())
	{
		return FReply::Unhandled();
	}
	if (Key.GetKey() == EKeys::Tab)
	{
		Owner->CycleChannel();
		return FReply::Handled();
	}
	if (Key.GetKey() == EKeys::Escape)
	{
		Composer->SetText(FText::GetEmpty());
		Owner->CloseComposer();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SScctHubHud::FocusComposer()
{
	if (Composer.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Composer, EFocusCause::SetDirectly);
	}
}

TSharedRef<SWidget> SScctHubHud::BuildPlayerList()
{
	static const FSlateColorBrush Shade(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
	const TWeakPtr<FScctHubHud> WeakHud = Hud;
	const TSharedPtr<FScctHubHudModel> M = Model;
	auto Column = [](const TCHAR* Title, int32 Side, TSharedPtr<SVerticalBox>& Rows)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				Ui::Label(Title, ScctHudStyle::Accent(Side))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(Rows, SVerticalBox)
			];
	};

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(&Shade)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(1380.0f)
			.MaxDesiredHeight(780.0f)
			[
				Ui::Glass(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							Ui::Title(TAttribute<FText>::CreateLambda([M]
							{
								return Ui::ToText(FString::Printf(TEXT("PLAYERS IN THE HUB  %d"), M->Players.Num()));
							}), 22.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							Ui::Button(Ui::ToText(TEXT("CLOSE")), FOnClicked::CreateLambda([WeakHud]
							{
								if (TSharedPtr<FScctHubHud> Owner = WeakHud.Pin())
								{
									Owner->TogglePlayerList();
								}
								return FReply::Handled();
							}))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						Ui::Text(Ui::ToText(TEXT("Voice volume and mute are kept on this PC for the hub's voice chat; chat mute hides a player's lines for this visit.")),
							11.0f, FSlateColor(ScctHudStyle::TextDim()))
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 24.0f, 0.0f)
							[
								Column(TEXT("SPIES"), 0, SpyRows)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								Column(TEXT("MERCS"), 1, MercRows)
							]
						]
					])
			]
		];
}

void SScctHubHud::RebuildPlayerRows()
{
	BuiltRosterRevision = Model->RosterRevision;
	SpyRows->ClearChildren();
	MercRows->ClearChildren();
	const TWeakPtr<FScctHubHud> WeakHud = Hud;
	const TSharedPtr<FScctHubHudModel> M = Model;
	for (const FScctHubHudPlayer& Player : Model->Players)
	{
		const int32 Id = Player.PlayerId;
		const FString Name = Player.Name;
		const bool bMe = (Player.Flags & EScctHubSocialFlags::Me) != 0;
		auto Find = [M, Id]() -> const FScctHubHudPlayer*
		{
			return M->Players.FindByPredicate([Id](const FScctHubHudPlayer& P) { return P.PlayerId == Id; });
		};

		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([Find]
				{
					const FScctHubHudPlayer* P = Find();
					if (P == nullptr)
					{
						return FText::GetEmpty();
					}
					return Ui::ToText(P->Name + ((P->Flags & EScctHubSocialFlags::SignedIn) != 0 ? TEXT("  ✓") : TEXT("  (guest)"))
						+ ((P->Flags & EScctHubSocialFlags::Me) != 0 ? TEXT("  (you)") : TEXT("")));
				})
				.Font(ScctHudStyle::Font(EWeight::Regular, 12.5f))
				.ColorAndOpacity(ScctHudStyle::Text())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(64.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([Find]
					{
						const FScctHubHudPlayer* P = Find();
						return P != nullptr ? Ui::ToText(FString::Printf(TEXT("%d ms"), P->PingMs)) : FText::GetEmpty();
					})
					.Font(ScctHudStyle::Font(EWeight::Regular, 11.5f))
					.ColorAndOpacity_Lambda([Find]
					{
						const FScctHubHudPlayer* P = Find();
						return FSlateColor(P != nullptr && P->PingMs > 150 ? ScctHudStyle::Danger() : ScctHudStyle::TextDim());
					})
				]
			];

		if (!bMe)
		{
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(120.0f)
				[
					SNew(SSlider)
					.MinValue(0.0f)
					.MaxValue(2.0f)
					.StepSize(0.05f)
					.ToolTipText(Ui::ToText(TEXT("This player's voice volume")))
					.Value_Lambda([WeakHud, Name] { TSharedPtr<FScctHubHud> Owner = WeakHud.Pin(); return Owner.IsValid() ? Owner->GetVoiceVolume(Name) : 1.0f; })
					.OnValueChanged_Lambda([WeakHud, Name](float Value)
					{
						if (TSharedPtr<FScctHubHud> Owner = WeakHud.Pin())
						{
							Owner->SetVoiceVolume(Name, Value);
						}
					})
				]
			];
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(48.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([WeakHud, Name]
					{
						TSharedPtr<FScctHubHud> Owner = WeakHud.Pin();
						return Ui::ToText(FString::Printf(TEXT("%.0f%%"), (Owner.IsValid() ? Owner->GetVoiceVolume(Name) : 1.0f) * 100.0f));
					})
					.Font(ScctHudStyle::Font(EWeight::Regular, 11.0f))
					.ColorAndOpacity(ScctHudStyle::TextDim())
				]
			];
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([WeakHud, Name]
				{
					TSharedPtr<FScctHubHud> Owner = WeakHud.Pin();
					return Owner.IsValid() && Owner->IsVoiceMuted(Name) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakHud, Name](ECheckBoxState NewState)
				{
					if (TSharedPtr<FScctHubHud> Owner = WeakHud.Pin())
					{
						Owner->SetVoiceMuted(Name, NewState == ECheckBoxState::Checked);
					}
				})
				[
					Ui::Text(Ui::ToText(TEXT("Mute voice")), 11.0f)
				]
			];
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Find]
				{
					const FScctHubHudPlayer* P = Find();
					return P != nullptr && (P->Flags & EScctHubSocialFlags::ChatMuted) != 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakHud, Name](ECheckBoxState NewState)
				{
					if (TSharedPtr<FScctHubHud> Owner = WeakHud.Pin())
					{
						Owner->SetChatMuted(Name, NewState == ECheckBoxState::Checked);
					}
				})
				[
					Ui::Text(Ui::ToText(TEXT("Mute chat")), 11.0f)
				]
			];
		}

		(Player.Team == 1 ? MercRows : SpyRows)->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
		[
			Line
		];
	}
}

TSharedRef<SWidget> SScctHubHud::BuildTravel()
{
	static const FSlateColorBrush Shade(FLinearColor(0.01f, 0.025f, 0.045f, 0.93f));
	const TWeakPtr<FScctHubHud> WeakHud = Hud;
	const TSharedPtr<FScctHubHudModel> M = Model;
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(&Shade)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(640.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					Ui::Title(Ui::ToText(TEXT("TRAVEL THE HUB")), 26.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 14.0f)
				[
					Ui::Text(TAttribute<FText>::CreateLambda([M]
					{
						return Ui::ToText(M->bBusy ? TEXT("Travelling cancels your current activity and leaves any match queue.")
							: M->Destinations.Num() == 0 ? TEXT("Travel destinations are not available on this hub.") : TEXT("Choose a district. All destinations are available."));
					}), 12.5f, FSlateColor(ScctHudStyle::TextDim()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(DestinationRows, SVerticalBox)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateLambda([WeakHud]
					{
						if (TSharedPtr<FScctHubHud> Owner = WeakHud.Pin())
						{
							Owner->CloseTravel();
						}
						return FReply::Handled();
					}), false, Ui::Accent(), 640.0f)
				]
			]
		];
}

void SScctHubHud::RebuildDestinations()
{
	BuiltDestinationRevision = Model->DestinationRevision;
	DestinationRows->ClearChildren();
	const TWeakPtr<FScctHubHud> WeakHud = Hud;
	for (const FScctHubHudDestination& Destination : Model->Destinations)
	{
		const FString Id = Destination.Id;
		const FString Caption = Destination.BestMs >= 0 ? Destination.Title + TEXT("   ·   Best ") + Time(Destination.BestMs) : Destination.Title;
		DestinationRows->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			Ui::Button(Ui::ToText(Caption), FOnClicked::CreateLambda([WeakHud, Id]
			{
				if (TSharedPtr<FScctHubHud> Owner = WeakHud.Pin())
				{
					Owner->Travel(Id);
				}
				return FReply::Handled();
			}), false, Ui::Accent(), 640.0f)
		];
	}
}

void SScctHubHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Model->bPlayerList && BuiltRosterRevision != Model->RosterRevision)
	{
		RebuildPlayerRows();
	}
	if (Model->bTravel && BuiltDestinationRevision != Model->DestinationRevision)
	{
		RebuildDestinations();
	}
	if (Model->bComposing && !bWasComposing)
	{
		FocusComposer();
	}
	bWasComposing = Model->bComposing;
}

// ---- painting ---------------------------------------------------------------------------------------------------

int32 SScctHubHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = PaintSigns(AllottedGeometry, OutDrawElements, LayerId);
	Layer = PaintTags(AllottedGeometry, OutDrawElements, Layer + 1);
	Layer = PaintChat(AllottedGeometry, OutDrawElements, Layer + 1);
	Layer = PaintTexts(AllottedGeometry, OutDrawElements, Layer + 1);
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer + 1, InWidgetStyle, bParentEnabled);
}

int32 SScctHubHud::PaintSigns(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	FScctHudPainter Painter(Out, Geometry, LayerId);
	const TSharedPtr<FScctHubHud> Owner = Hud.Pin();
	Model->SignsDrawn = 0;
	if (!Owner.IsValid() || Model->bTravel)
	{
		return Painter.GetLayer();
	}

	const float Scale = Geometry.Scale;
	// Farthest first, so a near sign is drawn over a far one.
	for (int32 Index = Model->Signs.Num() - 1; Index >= 0; --Index)
	{
		const FScctHubHudSign& Sign = Model->Signs[Index];
		FVector2D At, Above;
		if (!Owner->Project(Sign.World, Scale, At) || !Owner->Project(Sign.World + FVector(0.0, 0.0, Sign.LineHeight), Scale, Above))
		{
			continue;
		}
		// The text is as tall on the screen as a line of the sign is in the world.
		const float LineHeight = static_cast<float>(FVector2D::Distance(At, Above));
		const float Size = FMath::Min(LineHeight / 1.3f, 40.0f);
		if (Size < 5.0f)
		{
			continue;
		}
		TArray<FString> Lines;
		Sign.Text.ParseIntoArray(Lines, TEXT("\n"), false);
		const FSlateFontInfo Font = ScctHudStyle::Font(EWeight::Regular, Size);
		const float Height = Lines.Num() * Size * 1.3f;
		float Y = static_cast<float>(At.Y) - Height * 0.5f;
		Painter.SetOpacity(Sign.Alpha);
		for (const FString& Line : Lines)
		{
			Painter.Text(FVector2f(static_cast<float>(At.X), Y), Line, Font, Sign.Colour, FScctHudPainter::EAlign::Centre);
			Y += Size * 1.3f;
		}
		Painter.SetOpacity(1.0f);
		++Model->SignsDrawn;
	}
	return Painter.GetLayer();
}

int32 SScctHubHud::PaintTags(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	FScctHudPainter Painter(Out, Geometry, LayerId);
	const TSharedPtr<FScctHubHud> Owner = Hud.Pin();
	Model->TagsDrawn = 0;
	if (!Owner.IsValid() || Model->bTravel)
	{
		return Painter.GetLayer();
	}

	const float Scale = Geometry.Scale;
	for (const FScctHubHudTag& NameTag : Model->Tags)
	{
		FVector2D At;
		if (!Owner->Project(NameTag.World, Scale, At))
		{
			continue;
		}
		const float TagScaleNow = TagScale(NameTag.Distance);
		const FSlateFontInfo NameFont = ScctHudStyle::Font(EWeight::Bold, TagSize * TagScaleNow);
		const FSlateFontInfo SmallFont = ScctHudStyle::Font(EWeight::Regular, TagSize * 0.85f * TagScaleNow);
		const FLinearColor Colour = ScctHudStyle::Accent(NameTag.Team == 1 ? 1 : 0);

		// The lines stack upwards from the head: the bubble lowest, nearest the head... the name on top (as HubHud: name,
		// then the emote, then the bubble, the whole label's bottom at the head).
		TArray<TPair<FString, bool>> Lines;
		Lines.Emplace(NameTag.Name, true);
		if (!NameTag.Caption.IsEmpty())
		{
			Lines.Emplace(NameTag.Caption, false);
		}
		if (!NameTag.Bubble.IsEmpty())
		{
			TArray<FString> Bubble;
			NameTag.Bubble.ParseIntoArray(Bubble, TEXT("\n"));
			for (const FString& Part : Bubble)
			{
				Lines.Emplace(Part, false);
			}
		}
		const float LineGap = TagSize * 1.3f * TagScaleNow;
		float Y = static_cast<float>(At.Y) - LineGap * Lines.Num();
		for (const TPair<FString, bool>& Line : Lines)
		{
			Painter.Text(FVector2f(static_cast<float>(At.X), Y), Line.Key, Line.Value ? NameFont : SmallFont,
				Line.Value ? Colour : ScctHudStyle::Text(), FScctHudPainter::EAlign::Centre);
			if (Line.Value)
			{
				// The rank's emblem left of the name (HubHud's tag icon, Menu/ScctRankIcons); none for a guest.
				if (const FSlateBrush* RankBrush = ScctRankIcons::ForPacked(NameTag.Rank, 24.0f))
				{
					const float Edge = TagSize * 1.25f * TagScaleNow;
					const float NameWidth = Painter.Measure(Line.Key, NameFont).X;
					Painter.Image(FBox2f(FVector2f(static_cast<float>(At.X) - NameWidth * 0.5f - Edge - 4.0f, Y), FVector2f(static_cast<float>(At.X) - NameWidth * 0.5f - 4.0f, Y + Edge)),
						*RankBrush, FBox2f(FVector2f::ZeroVector, FVector2f::UnitVector), FLinearColor::White);
				}
			}
			Y += LineGap;
		}
		++Model->TagsDrawn;
	}
	return Painter.GetLayer();
}

int32 SScctHubHud::PaintChat(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	FScctHudPainter Painter(Out, Geometry, LayerId);
	if (Model->bPlayerList || Model->bTravel)
	{
		return Painter.GetLayer();
	}

	// The newest lines, bottom up, above the composer's place; older lines fade unless the composer is open.
	const FVector2f Size = Painter.Size();
	const FSlateFontInfo Font = ScctHudStyle::Font(EWeight::Regular, 12.0f);
	const FSlateFontInfo LabelFont = ScctHudStyle::Label(8.5f);
	const float Width = 560.0f;
	const float Left = ScctHudStyle::Margin;
	float Bottom = Size.Y - 300.0f;
	int32 Shown = 0;
	for (int32 Index = Model->Chat.Num() - 1; Index >= 0 && Shown < ChatLines; --Index)
	{
		const FScctHubHudChat& Line = Model->Chat[Index];
		const double Age = Model->Clock - Line.At;
		const float Alpha = Model->bComposing ? 1.0f : static_cast<float>(FMath::Clamp(ChatShowSeconds + 1.0 - Age, 0.0, 1.0));
		if (Alpha <= 0.0f)
		{
			break;
		}
		const bool bSystem = Line.Channel == EScctHubChatChannel::System;
		const FString Text = bSystem ? Line.Text : Line.Name + TEXT(": ") + Line.Text;
		TArray<FString> Wrapped;
		Painter.Wrap(Text, Font, Width - 64.0f, 3, Wrapped);
		const float Height = Wrapped.Num() * 17.0f;
		const float Top = Bottom - Height;
		Painter.SetOpacity(Alpha);
		Painter.Text(FVector2f(Left, Top + 3.0f), ChannelLabel(Line.Channel), LabelFont,
			Line.Channel == EScctHubChatChannel::Side ? ScctHudStyle::Accent(Line.Team) : Line.Channel == EScctHubChatChannel::Nearby ? ScctHudStyle::Good() : ScctHudStyle::TextDim());
		float Y = Top;
		for (const FString& Part : Wrapped)
		{
			Painter.Text(FVector2f(Left + 64.0f, Y), Part, Font, bSystem ? ScctHudStyle::TextDim() : Line.Team >= 0 ? FMath::Lerp(ScctHudStyle::Text(), ScctHudStyle::Accent(Line.Team), 0.45f) : ScctHudStyle::Text());
			Y += 17.0f;
		}
		Painter.SetOpacity(1.0f);
		Bottom = Top - 4.0f;
		++Shown;
	}
	return Painter.GetLayer();
}

int32 SScctHubHud::PaintTexts(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	FScctHudPainter Painter(Out, Geometry, LayerId);
	if (Model->bTravel)
	{
		return Painter.GetLayer();
	}
	const FVector2f Size = Painter.Size();

	// The hub's keys, top left, wrapped within 42 % of the width (HubHud's help line).
	const FSlateFontInfo Caption = ScctHudStyle::Font(EWeight::Regular, 10.5f);
	float HelpHeight = 0.0f;
	if (!Model->Help.IsEmpty())
	{
		const FString Title = Model->HubName.IsEmpty() ? FString() : Model->HubName.ToUpper();
		float Top = 16.0f;
		if (!Title.IsEmpty())
		{
			Painter.Text(FVector2f(ScctHudStyle::Margin, Top), Title, ScctHudStyle::Label(9.0f), ScctHudStyle::Accent(Model->Team));
			Top += 16.0f;
		}
		Painter.SetOpacity(0.8f);
		HelpHeight = Top + Painter.Paragraph(FVector2f(ScctHudStyle::Margin, Top), FMath::Max(320.0f, Size.X * 0.42f), Model->Help, Caption, ScctHudStyle::Text(),
			FScctHudPainter::EAlign::Left, 3);
		Painter.SetOpacity(1.0f);
	}

	// The pad panel, top middle, under the keys: the queue line on glass in its pad's state colour (white idle, yellow
	// waiting, green counting down, cyan starting), with a bar of that colour along its top (HubHud's pad panel).
	float PanelBottom = FMath::Max(84.0f, HelpHeight + 8.0f);
	if (!Model->Queue.IsEmpty())
	{
		const FSlateFontInfo Font = ScctHudStyle::Font(EWeight::Regular, 18.0f);
		const float Top = PanelBottom;
		const float HalfWidth = FMath::Min(Size.X * 0.4f, FMath::Max(260.0f, Model->Queue.Len() * 5.2f + 28.0f));
		const FBox2f Panel(FVector2f(Size.X * 0.5f - HalfWidth, Top - 8.0f), FVector2f(Size.X * 0.5f + HalfWidth, Top + 34.0f));
		Painter.Glass(Panel, ScctHudStyle::Corner);
		Painter.Fill(FBox2f(Panel.Min + FVector2f(ScctHudStyle::Corner, 0.0f), FVector2f(Panel.Max.X - ScctHudStyle::Corner, Panel.Min.Y + 3.0f)), Model->QueueColour);
		Painter.Text(FVector2f(Size.X * 0.5f, Top), Model->Queue, Font, Model->QueueColour, FScctHudPainter::EAlign::Centre);
		PanelBottom = Panel.Max.Y + 10.0f;
	}
	if (!Model->Note.IsEmpty())
	{
		Painter.SetOpacity(0.9f);
		Painter.Text(FVector2f(Size.X * 0.5f, PanelBottom), Model->Note, ScctHudStyle::Font(EWeight::Regular, 13.5f), ScctHudStyle::Text(), FScctHudPainter::EAlign::Centre);
		Painter.SetOpacity(1.0f);
		PanelBottom += 24.0f;
	}

	// The activity's clock, top middle on glass (a course run's time and gates, a range round's score and time left),
	// with the activities' notices under it (HubHud.Activities).
	if (!Model->Activity.IsEmpty())
	{
		TArray<FString> Lines;
		Model->Activity.ParseIntoArray(Lines, TEXT("\n"));
		const FSlateFontInfo Big = ScctHudStyle::Font(EWeight::Bold, 20.0f);
		const FSlateFontInfo Small = ScctHudStyle::Font(EWeight::Regular, 12.5f);
		const float Top = PanelBottom + 4.0f;
		const float Height = 30.0f + (Lines.Num() - 1) * 18.0f;
		const float HalfWidth = 190.0f;
		Painter.Glass(FBox2f(FVector2f(Size.X * 0.5f - HalfWidth, Top - 8.0f), FVector2f(Size.X * 0.5f + HalfWidth, Top + Height + 6.0f)), ScctHudStyle::Corner);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			Painter.Text(FVector2f(Size.X * 0.5f, Top + (Index == 0 ? 0.0f : 12.0f + Index * 18.0f)), Lines[Index], Index == 0 ? Big : Small,
				Index == 0 ? ScctHudStyle::Accent(Model->Team) : ScctHudStyle::Text(), FScctHudPainter::EAlign::Centre);
		}
		PanelBottom = Top + Height + 16.0f;
	}
	if (!Model->Notices.IsEmpty())
	{
		TArray<FString> Lines;
		Model->Notices.ParseIntoArray(Lines, TEXT("\n"));
		const FSlateFontInfo Font = ScctHudStyle::Font(EWeight::Regular, 14.0f);
		Painter.SetOpacity(0.95f);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			Painter.Text(FVector2f(Size.X * 0.5f, PanelBottom + Index * 20.0f), Lines[Index], Font, ScctHudStyle::Text(), FScctHudPainter::EAlign::Centre);
		}
		Painter.SetOpacity(1.0f);
	}

	// The nearest score board (the course's best times, the range's best scores, the ball's goals), a panel on the right.
	if (!Model->BoardTitle.IsEmpty() && !Model->bPlayerList)
	{
		TArray<FString> Lines;
		Model->BoardLines.ParseIntoArray(Lines, TEXT("\n"));
		const FSlateFontInfo Font = ScctHudStyle::Font(EWeight::Regular, 12.5f);
		constexpr float Width = 360.0f;
		const FVector2f At(Size.X - ScctHudStyle::Margin - Width + 16.0f, 200.0f);
		Painter.Glass(FBox2f(At - FVector2f(16.0f, 12.0f), At + FVector2f(Width - 16.0f, 30.0f + Lines.Num() * 19.0f)), ScctHudStyle::Corner);
		Painter.Text(At, Model->BoardTitle.ToUpper(), ScctHudStyle::Label(10.0f), ScctHudStyle::Accent(Model->Team));
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			Painter.Text(At + FVector2f(0.0f, 26.0f + Index * 19.0f), Lines[Index], Font, ScctHudStyle::Text());
		}
	}

	// What U does here, bottom middle.
	if (!Model->Prompt.IsEmpty() && !Model->bPlayerList)
	{
		Painter.Text(FVector2f(Size.X * 0.5f, Size.Y - 170.0f), Model->Prompt, ScctHudStyle::Font(EWeight::Regular, 13.5f), ScctHudStyle::Text(), FScctHudPainter::EAlign::Centre);
	}

	// The emote menu, left of centre.
	if (Model->bEmoteMenu && !Model->EmoteMenu.IsEmpty())
	{
		TArray<FString> Lines;
		Model->EmoteMenu.ParseIntoArray(Lines, TEXT("\n"));
		const FSlateFontInfo Font = ScctHudStyle::Font(EWeight::Regular, 12.5f);
		const FVector2f At(Size.X * 0.5f - 300.0f, Size.Y * 0.5f - 130.0f);
		Painter.Glass(FBox2f(At - FVector2f(16.0f, 12.0f), At + FVector2f(200.0f, Lines.Num() * 20.0f + 8.0f)), ScctHudStyle::Corner);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			Painter.Text(At + FVector2f(0.0f, Index * 20.0f), Lines[Index], Index == 0 ? ScctHudStyle::Label(9.5f) : Font,
				Index == 0 ? ScctHudStyle::Accent(Model->Team) : ScctHudStyle::Text());
		}
	}
	return Painter.GetLayer();
}
