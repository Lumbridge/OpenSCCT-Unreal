#include "Voice/SScctVoiceOverlay.h"

#include "Brushes/SlateColorBrush.h"
#include "Hud/ScctHudStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ScctVoiceOverlay"

namespace
{
	/** Below the HUD's top right block (the round's clock and score), right aligned to the HUD's margin. */
	constexpr float TopOffset = 150.0f;
	constexpr float RowWidth = 330.0f;

	/**
	 * One line of the overlay. A player's line takes a left click (mute toggle) and the wheel (volume) while the overlay
	 * is interactive, and lights up under the cursor; the local player's lines never take the mouse.
	 */
	class SScctVoiceOverlayRow : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctVoiceOverlayRow) {}
			SLATE_ARGUMENT(FScctVoiceOverlayLine, Line)
			SLATE_ATTRIBUTE(bool, Interactive)
			SLATE_EVENT(FScctVoiceOverlayMute, OnToggleMute)
			SLATE_EVENT(FScctVoiceOverlayVolume, OnAdjustVolume)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Line = InArgs._Line;
			Interactive = InArgs._Interactive;
			OnToggleMute = InArgs._OnToggleMute;
			OnAdjustVolume = InArgs._OnAdjustVolume;
			static const FSlateColorBrush Brush(FLinearColor::White);
			const bool bPlayer = !Line.Player.IsEmpty();

			SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SScctVoiceOverlayRow::GetRowVisibility));
			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(&Brush)
				.Padding(FMargin(8.0f, 2.0f))
				.HAlign(HAlign_Right)
				.BorderBackgroundColor(TAttribute<FSlateColor>::CreateSP(this, &SScctVoiceOverlayRow::GetBackground))
				.ToolTipText(bPlayer ? LOCTEXT("RowTip", "Click to mute or unmute; scroll to adjust volume") : FText::GetEmpty())
				[
					SNew(STextBlock)
					.Text(FText::FromString(Line.Text))
					.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 13.0f, 20))
					.ColorAndOpacity(FSlateColor(Line.Colour))
					.ShadowOffset(FVector2D(1.0f, 1.0f))
					.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
				]
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!CanTakeMouse() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			OnToggleMute.ExecuteIfBound(Line.Player);
			return FReply::Handled();
		}

		virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!CanTakeMouse() || FMath::IsNearlyZero(Event.GetWheelDelta()))
			{
				return FReply::Unhandled();
			}
			OnAdjustVolume.ExecuteIfBound(Line.Player, Event.GetWheelDelta() > 0.0f ? 1 : -1);
			return FReply::Handled();
		}

		virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override
		{
			return CanTakeMouse() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
		}

	private:
		bool CanTakeMouse() const { return !Line.Player.IsEmpty() && Interactive.Get(); }

		EVisibility GetRowVisibility() const
		{
			return CanTakeMouse() ? EVisibility::Visible : EVisibility::HitTestInvisible;
		}

		FSlateColor GetBackground() const
		{
			// A dark band behind every line so it reads over a bright scene; lighter under the cursor when it takes clicks.
			const float Alpha = CanTakeMouse() && IsHovered() ? 0.55f : 0.32f;
			return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, Alpha));
		}

		FScctVoiceOverlayLine Line;
		TAttribute<bool> Interactive;
		FScctVoiceOverlayMute OnToggleMute;
		FScctVoiceOverlayVolume OnAdjustVolume;
	};
}

void SScctVoiceOverlay::Construct(const FArguments& InArgs)
{
	OnToggleMute = InArgs._OnToggleMute;
	OnAdjustVolume = InArgs._OnAdjustVolume;
	// The overlay itself never takes the mouse; only its players' lines do, while the cursor is free.
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	.Padding(FMargin(0.0f, TopOffset, ScctHudStyle::Margin, 0.0f))
	[
		SNew(SBox)
		.WidthOverride(RowWidth)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SAssignNew(Rows, SVerticalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
		]
	];
}

void SScctVoiceOverlay::SetLines(const TArray<FScctVoiceOverlayLine>& InLines)
{
	if (InLines == Lines)
	{
		return;
	}
	Lines = InLines;
	Rebuild();
}

bool SScctVoiceOverlay::GetLineScreenCentre(int32 Index, FVector2D& OutCentre) const
{
	FChildren* Children = Rows.IsValid() ? Rows->GetChildren() : nullptr;
	if (Children == nullptr || Index < 0 || Index >= Children->Num())
	{
		return false;
	}
	const FGeometry& Geometry = Children->GetChildAt(Index)->GetTickSpaceGeometry();
	if (Geometry.GetLocalSize().IsNearlyZero())
	{
		return false;
	}
	OutCentre = FVector2D(Geometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f)));
	return true;
}

void SScctVoiceOverlay::Rebuild()
{
	Rows->ClearChildren();
	const TWeakPtr<SScctVoiceOverlay> Weak = SharedThis(this);
	const TAttribute<bool> Interactive = TAttribute<bool>::CreateLambda([Weak]
	{
		const TSharedPtr<SScctVoiceOverlay> Pinned = Weak.Pin();
		return Pinned.IsValid() && Pinned->bInteractive;
	});
	for (const FScctVoiceOverlayLine& Line : Lines)
	{
		Rows->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(SScctVoiceOverlayRow)
			.Line(Line)
			.Interactive(Interactive)
			.OnToggleMute(OnToggleMute)
			.OnAdjustVolume(OnAdjustVolume)
		];
	}
}

#undef LOCTEXT_NAMESPACE
