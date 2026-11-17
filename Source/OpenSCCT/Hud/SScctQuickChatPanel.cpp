#include "Hud/SScctQuickChatPanel.h"

#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudPainter.h"
#include "Hud/ScctHudStyle.h"
#include "Hud/ScctQuickChat.h"

using namespace ScctHudStyle;

namespace
{
	// Godot's panel: 386 wide, 24 from the right edge, centred on the screen's height (as high as its rows need).
	constexpr float QuickChatPanelWidth = 386.0f;
	constexpr float QuickChatPanelHeight = 252.0f;
	constexpr float QuickChatRightGap = 24.0f;
	constexpr float QuickChatPad = 16.0f;
	constexpr float QuickChatRowHeight = 40.0f;

	/** The D-pad direction of each choice, as Godot labels them. */
	const TCHAR* QuickChatDirection(int32 Index)
	{
		static const TCHAR* const Directions[] = { TEXT("↑"), TEXT("→"), TEXT("↓"), TEXT("←") };
		return Directions[FMath::Clamp(Index, 0, 3)];
	}
}

void SScctQuickChatPanel::Construct(const FArguments& InArgs)
{
	State = InArgs._State;
	LocalTeam = InArgs._LocalTeam;
	Clock = InArgs._Clock;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SScctQuickChatPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (State == nullptr || !State->IsOpen())
	{
		return LayerId;
	}

	FScctHudPainter Painter(OutDrawElements, AllottedGeometry, LayerId);
	const FVector2f Size = Painter.Size();
	const int32 Team = LocalTeam.Get(0);
	const FLinearColor Tint = Accent(Team);
	const int32 Category = State->GetCategory();

	const float Left = Size.X - QuickChatRightGap - QuickChatPanelWidth;
	const float Top = FMath::Max((Size.Y - QuickChatPanelHeight) * 0.5f, 0.0f);
	const FBox2f Back = ScctHudElements::HudBox(Left, Top, QuickChatPanelWidth, QuickChatPanelHeight);
	Painter.Glass(Back, Corner);
	Painter.Fill(Back, FLinearColor(0.004f, 0.009f, 0.007f, 0.45f), Corner, Fade(Tint, 0.55f), 1.0f);
	Painter.Fill(ScctHudElements::HudBox(Left, Top + Corner, 2.0f, QuickChatPanelHeight - 2.0f * Corner), Fade(Tint, 0.8f));

	// The title: the menu's, or the category picked.
	float Y = Top + QuickChatPad;
	const FString Title = Category >= 0
		? FString::Printf(TEXT("TEAM — %s"), *FString(FScctQuickChatState::Category(Category).Name).ToUpper())
		: FString(TEXT("TEAM QUICK CHAT"));
	Painter.Text(FVector2f(Left + QuickChatPad, Y), Title, Font(EWeight::Bold, 12.0f, 120), Tint);
	Y += 30.0f;

	// The four choices, each with its key and D-pad direction.
	const FSlateFontInfo KeyFont = Font(EWeight::Regular, 9.0f);
	const FSlateFontInfo ChoiceFont = Font(EWeight::Regular, 12.0f);
	for (int32 Index = 0; Index < FScctQuickChatState::NumChoices; ++Index)
	{
		const FBox2f Row = ScctHudElements::HudBox(Left + QuickChatPad, Y, QuickChatPanelWidth - 2.0f * QuickChatPad, QuickChatRowHeight - 6.0f);
		Painter.Fill(Row, FLinearColor(1.0f, 1.0f, 1.0f, 0.05f), 3.0f, Fade(Tint, 0.25f), 1.0f);
		const FBox2f KeyBox = ScctHudElements::HudBox(Row.Min.X + 6.0f, Row.Min.Y + 5.0f, 24.0f, Row.GetSize().Y - 10.0f);
		Painter.Fill(KeyBox, Fade(Tint, 0.85f), 3.0f);
		const float TextY = Row.Min.Y + (Row.GetSize().Y - Painter.Measure(TEXT("Ag"), ChoiceFont).Y) * 0.5f;
		Painter.Text(FVector2f(KeyBox.GetCenter().X, TextY), FString::FromInt(Index + 1), Font(EWeight::Bold, 12.0f), FLinearColor(0.02f, 0.02f, 0.02f), FScctHudPainter::EAlign::Centre, false);
		const FString Choice = Category >= 0 ? FScctQuickChatState::Category(Category).Messages[Index] : FScctQuickChatState::Category(Index).Name;
		Painter.Text(FVector2f(KeyBox.Max.X + 12.0f, TextY), Choice, ChoiceFont, Text());
		const FString Pad = FString::Printf(TEXT("D-pad %s"), QuickChatDirection(Index));
		Painter.Text(FVector2f(Row.Max.X - 8.0f, Row.Min.Y + (Row.GetSize().Y - Painter.Measure(TEXT("Ag"), KeyFont).Y) * 0.5f), Pad, KeyFont, TextFaint(), FScctHudPainter::EAlign::Right);
		Y += QuickChatRowHeight;
	}

	// The keys' hint and the time left before the menu closes by itself.
	Y += 4.0f;
	const FString Hint = Category >= 0
		? FString(TEXT("1–4 / D-pad: send to team    Esc / B: back"))
		: FString(TEXT("1–4 / D-pad: choose    Esc / B: close"));
	Painter.Text(FVector2f(Left + QuickChatPad, Y), Hint, Font(EWeight::Regular, 9.5f), TextDim());
	const float BarWidth = QuickChatPanelWidth - 2.0f * QuickChatPad;
	const float Share = static_cast<float>(State->TimeLeft(Clock.Get(0.0)) / FScctQuickChatState::TimeoutSeconds);
	const float BarY = Top + QuickChatPanelHeight - QuickChatPad + 4.0f;
	Painter.Fill(ScctHudElements::HudBox(Left + QuickChatPad, BarY, BarWidth, 2.0f), FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
	Painter.Fill(ScctHudElements::HudBox(Left + QuickChatPad, BarY, BarWidth * FMath::Clamp(Share, 0.0f, 1.0f), 2.0f), Fade(Tint, 0.7f));

	return Painter.GetLayer();
}
