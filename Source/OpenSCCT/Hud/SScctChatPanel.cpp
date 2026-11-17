#include "Hud/SScctChatPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudPainter.h"
#include "Hud/ScctHudStyle.h"
#include "InputCoreTypes.h"
#include "Settings/ScctSettings.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

using namespace ScctHudStyle;
using EAlign = FScctHudPainter::EAlign;
using ScctHudElements::HudBox;

namespace
{
	/** How long the input stays with the panel at most after it closed, waiting for the closing key to come up. */
	constexpr double ReleaseSeconds = 0.4;

	constexpr float LinePad = 2.0f;

	FSlateFontInfo ChatFont() { return Font(EWeight::Regular, 11.0f); }

	const TCHAR* ChannelTag(EScctChatChannel Channel)
	{
		switch (Channel)
		{
		case EScctChatChannel::Team: return TEXT("[TEAM]");
		case EScctChatChannel::System: return TEXT("[SYSTEM]");
		default: return TEXT("[ALL]");
		}
	}

	FLinearColor ChannelColour(EScctChatChannel Channel, int32 LocalTeam)
	{
		switch (Channel)
		{
		case EScctChatChannel::Team: return Accent(LocalTeam);
		case EScctChatChannel::System: return TextDim();
		default: return Srgb(176, 199, 184);
		}
	}

	FLinearColor SenderColour(int32 Team)
	{
		return Team == 0 || Team == 1 ? Accent(Team) : Text();
	}

	/** "Name: " in front of a player's line; nothing for the server's. */
	FString SenderPart(const FScctChatEntry& Entry)
	{
		return Entry.Sender.IsEmpty() || Entry.Channel == EScctChatChannel::System ? FString() : Entry.Sender + TEXT(": ");
	}

	/** One wrapped line of an entry as drawn. */
	struct FDisplayLine
	{
		const FScctChatEntry* Entry = nullptr;
		FString Text;
		bool bFirst = false;
	};

	/** The entry broken into the lines it takes at Width (after the channel tag's column). */
	void WrapEntry(const FScctHudPainter& Painter, const FScctChatEntry& Entry, float Width, TArray<FDisplayLine>& Out)
	{
		TArray<FString> Wrapped;
		Painter.Wrap(SenderPart(Entry) + Entry.Text, ChatFont(), Width, 8, Wrapped);

		// The wrap breaks at spaces only: a word wider than the panel (a pasted link, a long name) is broken where it
		// meets the edge.
		TArray<FString> Lines;
		for (FString Line : Wrapped)
		{
			while (Line.Len() > 1 && Painter.Measure(Line, ChatFont()).X > Width)
			{
				int32 Fit = 1;
				while (Fit < Line.Len() && Painter.Measure(Line.Left(Fit + 1), ChatFont()).X <= Width)
				{
					++Fit;
				}
				Lines.Add(Line.Left(Fit));
				Line.RightChopInline(Fit);
			}
			Lines.Add(Line);
		}
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			FDisplayLine& Line = Out.AddDefaulted_GetRef();
			Line.Entry = &Entry;
			Line.Text = Lines[Index];
			Line.bFirst = Index == 0;
		}
	}

	/** Draws one wrapped line: the channel tag and the sender's name in their colours on the first line, then the text. */
	void DrawLine(FScctHudPainter& Painter, const FDisplayLine& Line, const FVector2f& At, float TagWidth, int32 LocalTeam)
	{
		const FScctChatEntry& Entry = *Line.Entry;
		const FSlateFontInfo LineFont = ChatFont();
		const FLinearColor Body = Entry.Channel == EScctChatChannel::System ? TextDim() : Text();
		float X = At.X + TagWidth;
		FString Rest = Line.Text;
		if (Line.bFirst)
		{
			Painter.Text(At, ChannelTag(Entry.Channel), LineFont, ChannelColour(Entry.Channel, LocalTeam));
			const FString Sender = SenderPart(Entry);
			if (!Sender.IsEmpty() && Rest.StartsWith(Sender, ESearchCase::CaseSensitive))
			{
				X += Painter.Text(FVector2f(X, At.Y), Sender, LineFont, SenderColour(Entry.SenderTeam)).X;
				Rest.RightChopInline(Sender.Len());
			}
		}
		Painter.Text(FVector2f(X, At.Y), Rest, LineFont, Body);
	}

	FString CleanLine(const FString& Text)
	{
		FString Result;
		Result.Reserve(FMath::Min(Text.Len(), SScctChatPanel::MaxChars));
		for (const TCHAR Char : Text)
		{
			if (Result.Len() >= SScctChatPanel::MaxChars)
			{
				break;
			}
			if (!FChar::IsControl(Char))
			{
				Result.AppendChar(Char);
			}
		}
		return Result.TrimStartAndEnd();
	}

	bool IsFunctionKey(const FKey& Key)
	{
		static const FKey Keys[] = { EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8,
			EKeys::F9, EKeys::F10, EKeys::F11, EKeys::F12 };
		for (const FKey& Each : Keys)
		{
			if (Each == Key)
			{
				return true;
			}
		}
		return false;
	}
}

void SScctChatPanel::Construct(const FArguments& InArgs)
{
	Transcript = InArgs._Transcript;
	LocalTeam = InArgs._LocalTeam;
	Clock = InArgs._Clock;
	OnSubmit = InArgs._OnSubmit;
	OnClosed = InArgs._OnClosed;

	// The panel takes clicks and the wheel only while it is open; closed it is part of the picture.
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return bOpen ? EVisibility::Visible : EVisibility::HitTestInvisible;
	}));

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateSP(this, &SScctChatPanel::EntryPadding))
		[
			SNew(SBox)
			.WidthOverride_Lambda([this]() { return static_cast<float>(Layout(FVector2f(GetCachedGeometry().GetLocalSize())).Entry.GetSize().X); })
			.HeightOverride_Lambda([this]() { return static_cast<float>(Layout(FVector2f(GetCachedGeometry().GetLocalSize())).Entry.GetSize().Y); })
			.VAlign(VAlign_Center)
			.Visibility_Lambda([this]() { return bOpen ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SAssignNew(Entry, SEditableText)
				.Font(ChatFont())
				.ColorAndOpacity(Text())
				.HintText_Lambda([this]() { return FText::FromString(bTeam ? TEXT("Message your team...") : TEXT("Message everyone...")); })
				.ClearKeyboardFocusOnCommit(false)
				.SelectAllTextWhenFocused(false)
				.RevertTextOnEscape(false)
				.OnTextChanged(this, &SScctChatPanel::HandleTextChanged)
				.OnTextCommitted(this, &SScctChatPanel::HandleTextCommitted)
			]
		]
	];
}

SScctChatPanel::FLayout SScctChatPanel::Layout(const FVector2f& Size)
{
	FLayout L;
	L.Left = Margin;
	L.Width = FMath::Clamp(Size.X * 0.36f, 260.0f, 460.0f);

	// The HUD's status block (72 high, 16 above the margin) and the five lines of its log above it stay clear.
	L.PassiveBottom = FMath::Max(Size.Y - Margin - 72.0f - 16.0f - 5.0f * 20.0f - 14.0f, 120.0f);
	const float Height = FMath::Clamp(L.PassiveBottom - Margin - 40.0f, 150.0f, 380.0f);
	const float Top = L.PassiveBottom - Height;
	constexpr float Pad = 12.0f, HeaderHeight = 22.0f, ComposerHeight = 30.0f, HintHeight = 16.0f, Gap = 6.0f;

	L.Panel = HudBox(L.Left, Top, L.Width, Height);
	L.Header = HudBox(L.Left + Pad, Top + Pad - 2.0f, L.Width - Pad * 2.0f, HeaderHeight);
	L.Hint = HudBox(L.Left + Pad, Top + Height - Pad - HintHeight + 2.0f, L.Width - Pad * 2.0f, HintHeight);
	L.Composer = HudBox(L.Left + Pad, L.Hint.Min.Y - Gap - ComposerHeight, L.Width - Pad * 2.0f, ComposerHeight);
	L.History = FBox2f(FVector2f(L.Left + Pad, L.Header.Max.Y + Gap), FVector2f(L.Left + L.Width - Pad, L.Composer.Min.Y - Gap));

	// In the composer: the channel chip (64), the text box, the count of characters (56).
	constexpr float Chip = 64.0f, Count = 56.0f;
	L.Entry = FBox2f(FVector2f(L.Composer.Min.X + Chip + 10.0f, L.Composer.Min.Y + 2.0f), FVector2f(L.Composer.Max.X - Count, L.Composer.Max.Y - 2.0f));
	return L;
}

FMargin SScctChatPanel::EntryPadding() const
{
	const FLayout L = Layout(FVector2f(GetCachedGeometry().GetLocalSize()));
	return FMargin(L.Entry.Min.X, L.Entry.Min.Y, 0.0f, 0.0f);
}

void SScctChatPanel::Open(bool bInTeam)
{
	bTeam = bInTeam;
	bOpen = true;
	bReleasing = false;
	Scroll = 0;
	PaintedLastId = -1;
	bNewBelow = false;
	Entry->SetText(FText::GetEmpty());
	FSlateApplication::Get().SetKeyboardFocus(Entry, EFocusCause::SetDirectly);
}

void SScctChatPanel::Close()
{
	FinishClose(FKey());
}

void SScctChatPanel::FinishClose(const FKey& ClosingKey)
{
	const bool bWasOpen = bOpen;
	bOpen = false;
	Entry->SetText(FText::GetEmpty());
	if (ClosingKey.IsValid())
	{
		// The key that closed the panel is still down: the game gets the keys back when it comes up, so the bridge
		// does not read it as a new press (Enter is Talk, Esc the match menu).
		bReleasing = true;
		ReleaseKey = ClosingKey;
		ReleaseUntil = FPlatformTime::Seconds() + ReleaseSeconds;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
		return;
	}
	const bool bWasReleasing = bReleasing;
	bReleasing = false;
	if (bWasOpen || bWasReleasing)
	{
		OnClosed.ExecuteIfBound();
	}
}

void SScctChatPanel::SetChannelKeys(const TArray<FKey>& InTalkKeys, const TArray<FKey>& InTeamTalkKeys)
{
	TalkKeys = InTalkKeys;
	TeamTalkKeys = InTeamTalkKeys;
}

FString SScctChatPanel::GetDraft() const
{
	return Entry->GetText().ToString();
}

void SScctChatPanel::SetDraft(const FString& Text)
{
	Entry->SetText(FText::FromString(Text.Left(MaxChars)));
}

void SScctChatPanel::Submit()
{
	if (!bOpen)
	{
		return;
	}
	const FString Line = CleanLine(GetDraft());
	const bool bWasTeam = bTeam;
	FinishClose(FKey());
	if (!Line.IsEmpty())
	{
		OnSubmit.ExecuteIfBound(Line, bWasTeam);
	}
}

void SScctChatPanel::HandleTextChanged(const FText& Text)
{
	// The relay takes 100 UTF-16 characters (ChatK.MaxChars); a pasted longer text is cut, never a character in two.
	const FString String = Text.ToString();
	if (String.Len() > MaxChars)
	{
		int32 Length = MaxChars;
		if (StringConv::IsHighSurrogate(String[Length - 1]))
		{
			--Length;
		}
		Entry->SetText(FText::FromString(String.Left(Length)));
	}
}

void SScctChatPanel::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter || !bOpen)
	{
		return;
	}
	const FString Line = CleanLine(Text.ToString());
	const bool bWasTeam = bTeam;
	FinishClose(EKeys::Enter);
	if (!Line.IsEmpty())
	{
		OnSubmit.ExecuteIfBound(Line, bWasTeam);
	}
}

FReply SScctChatPanel::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bOpen)
	{
		// Waiting for the closing key: nothing typed meanwhile reaches anything.
		return bReleasing ? FReply::Handled() : FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		FinishClose(Key);
		return FReply::Handled();
	}
	if (Key == EKeys::Tab)
	{
		if (!InKeyEvent.IsRepeat())
		{
			if (InKeyEvent.IsControlDown())
			{
				SetFilter(static_cast<EFilter>((static_cast<int32>(Filter) + 1) % 3));
			}
			else
			{
				bTeam = !bTeam;
			}
		}
		return FReply::Handled();
	}
	if (Key == EKeys::PageUp || Key == EKeys::PageDown)
	{
		const int32 Page = FMath::Max(VisibleLines - 1, 1);
		ScrollBy(Key == EKeys::PageUp ? Page : -Page);
		return FReply::Handled();
	}
	if (TeamTalkKeys.Contains(Key) || TalkKeys.Contains(Key) || IsFunctionKey(Key))
	{
		// Talk and TeamTalk pick the channel; no function key opens anything behind the box.
		if (!InKeyEvent.IsRepeat() && (TeamTalkKeys.Contains(Key) || (TalkKeys.Contains(Key) && Key != EKeys::Enter)))
		{
			bTeam = TeamTalkKeys.Contains(Key);
		}
		if (Key != EKeys::Enter)
		{
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SScctChatPanel::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bReleasing && InKeyEvent.GetKey() == ReleaseKey)
	{
		FinishClose(FKey());
		return FReply::Handled();
	}
	return bReleasing ? FReply::Handled() : FReply::Unhandled();
}

FReply SScctChatPanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bOpen)
	{
		return FReply::Unhandled();
	}
	ScrollBy(FMath::RoundToInt32(MouseEvent.GetWheelDelta() * 3.0f));
	return FReply::Handled();
}

FReply SScctChatPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bOpen)
	{
		return FReply::Unhandled();
	}
	const FVector2f Local(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	for (const TPair<FBox2f, EFilter>& Tab : TabBoxes)
	{
		if (Tab.Key.IsInsideOrOn(Local))
		{
			SetFilter(Tab.Value);
			break;
		}
	}
	const FLayout L = Layout(FVector2f(MyGeometry.GetLocalSize()));
	if (Local.X >= L.Composer.Min.X && Local.X < L.Entry.Min.X && Local.Y >= L.Composer.Min.Y && Local.Y <= L.Composer.Max.Y)
	{
		bTeam = !bTeam;
	}
	// The text box keeps the keys whatever was clicked.
	return FReply::Handled().SetUserFocus(Entry.ToSharedRef(), EFocusCause::SetDirectly);
}

void SScctChatPanel::ScrollBy(int32 Lines)
{
	// Past the oldest line is taken back by the next paint, which knows how many lines the history has.
	Scroll = FMath::Max(Scroll + Lines, 0);
	if (PaintedLastId >= 0)
	{
		Scroll = FMath::Min(Scroll, FMath::Max(PaintedLines - VisibleLines, 0));
	}
}

void SScctChatPanel::SetFilter(EFilter InFilter)
{
	Filter = InFilter;
	Scroll = 0;
	PaintedLastId = -1;
}

TSharedPtr<SWidget> SScctChatPanel::GetFocusTarget() const
{
	return Entry;
}

bool SScctChatPanel::Shows(const FScctChatEntry& Line) const
{
	switch (Filter)
	{
	case EFilter::Team: return Line.Channel == EScctChatChannel::Team;
	case EFilter::System: return Line.Channel == EScctChatChannel::System;
	default: return true;
	}
}

int32 SScctChatPanel::CountListed() const
{
	int32 Count = 0;
	if (Transcript.IsValid())
	{
		for (const FScctChatEntry& Line : Transcript->GetEntries())
		{
			Count += Shows(Line) ? 1 : 0;
		}
	}
	return Count;
}

void SScctChatPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bReleasing && FPlatformTime::Seconds() >= ReleaseUntil)
	{
		FinishClose(FKey());
	}
	// A click on the game behind (or a window switch) must not leave the open box without the keys.
	if (bOpen && FSlateApplication::Get().GetKeyboardFocusedWidget() != Entry)
	{
		FSlateApplication::Get().SetKeyboardFocus(Entry, EFocusCause::SetDirectly);
	}
}

int32 SScctChatPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!Transcript.IsValid())
	{
		return LayerId;
	}

	FScctHudPainter Painter(OutDrawElements, AllottedGeometry, LayerId);
	const FLayout L = Layout(Painter.Size());
	const TArray<FScctChatEntry>& Entries = Transcript->GetEntries();
	const int32 Team = LocalTeam.Get(0);
	const FSlateFontInfo LineFont = ChatFont();
	const float LineHeight = Painter.Measure(TEXT("Ag"), LineFont).Y + LinePad;
	const float TagWidth = Painter.Measure(TEXT("[SYSTEM] "), LineFont).X;
	const double Now = Clock.Get(0.0);

	if (!bOpen)
	{
		// Passive: the newest lines that are still in their time, newest lowest, at most PassiveLines wrapped lines.
		const int32 PassiveLines = FMath::Clamp(FScctSettings::GetInt(TEXT("Hud.ChatPassiveLines")), 0, 8);
		const double Visible = FMath::Max(FScctSettings::GetFloat(TEXT("Hud.ChatFadeSeconds")), 0.5f);
		const float TextWidth = L.Width - 20.0f - TagWidth;
		TArray<FDisplayLine> Shown;
		TArray<float> Alphas;
		float Strongest = 0.0f;
		for (int32 Index = Entries.Num() - 1; Index >= 0 && Shown.Num() < PassiveLines; --Index)
		{
			const float Alpha = FScctChatTranscript::Opacity(Entries[Index], Now, Visible);
			if (Alpha <= 0.0f)
			{
				break;
			}
			TArray<FDisplayLine> Lines;
			WrapEntry(Painter, Entries[Index], TextWidth, Lines);
			// The oldest line's first rows are what gives way when there is no room for all of it.
			for (int32 Row = Lines.Num() - 1; Row >= 0 && Shown.Num() < PassiveLines; --Row)
			{
				Shown.Add(Lines[Row]);
				Alphas.Add(Alpha);
			}
			Strongest = FMath::Max(Strongest, Alpha);
		}
		if (Shown.Num() == 0)
		{
			return LayerId;
		}

		const float Height = Shown.Num() * LineHeight + 10.0f;
		const FBox2f Back = HudBox(L.Left, L.PassiveBottom - Height, L.Width, Height);
		Painter.SetOpacity(Strongest);
		Painter.Fill(Back, FLinearColor(0.004f, 0.009f, 0.007f, 0.55f), 3.0f);
		Painter.Fill(HudBox(L.Left, Back.Min.Y, 2.0f, Height), Fade(Accent(Team), 0.5f));
		float Y = L.PassiveBottom - 5.0f - LineHeight;
		for (int32 Index = 0; Index < Shown.Num(); ++Index)
		{
			Painter.SetOpacity(Alphas[Index]);
			// A line continued from the one above has no tag of its own: it is indented under the text.
			DrawLine(Painter, Shown[Index], FVector2f(L.Left + 10.0f, Y), TagWidth, Team);
			Y -= LineHeight;
		}
		Painter.SetOpacity(1.0f);
		return Painter.GetLayer();
	}

	// Open: the panel, the header with the tabs and the count, the history, the composer and the keys.
	Painter.Glass(L.Panel, Corner, 0.72f);
	Painter.GlowLine(FVector2f(L.Panel.Min.X + 1.0f, L.Panel.Min.Y + 10.0f), FVector2f(L.Panel.Min.X + 1.0f, L.Panel.Max.Y - 10.0f), Accent(Team), 2.0f);

	const FSlateFontInfo LabelFont = Label(8.5f);
	const FSlateFontInfo SmallFont = Font(EWeight::Regular, 9.0f);
	float X = L.Header.Min.X;
	X += Painter.Text(FVector2f(X, L.Header.Min.Y + 4.0f), TEXT("CHAT"), LabelFont, TextDim()).X + 16.0f;
	TabBoxes.Reset();
	static const TPair<const TCHAR*, EFilter> Tabs[] = { { TEXT("ALL"), EFilter::All }, { TEXT("TEAM"), EFilter::Team }, { TEXT("SYSTEM"), EFilter::System } };
	for (const TPair<const TCHAR*, EFilter>& Tab : Tabs)
	{
		const bool bOn = Filter == Tab.Value;
		const float Width = Painter.Measure(Tab.Key, LabelFont).X + 16.0f;
		const FBox2f Box = HudBox(X, L.Header.Min.Y, Width, L.Header.GetSize().Y - 2.0f);
		const FLinearColor Colour = Tab.Value == EFilter::Team ? Accent(Team) : (Tab.Value == EFilter::System ? TextDim() : Srgb(176, 199, 184));
		Painter.Fill(Box, bOn ? Fade(Colour, 0.16f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.03f), 3.0f, bOn ? Fade(Colour, 0.7f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);
		Painter.Text(FVector2f(X + 8.0f, L.Header.Min.Y + 4.0f), Tab.Key, LabelFont, bOn ? Colour : TextFaint());
		TabBoxes.Emplace(Box, Tab.Value);
		X += Width + 6.0f;
	}
	const int32 Listed = CountListed();
	Painter.Text(FVector2f(L.Header.Max.X, L.Header.Min.Y + 4.0f), FString::Printf(TEXT("%d %s"), Listed, Listed == 1 ? TEXT("LINE") : TEXT("LINES")), LabelFont, TextDim(), EAlign::Right);

	// The history: every listed line wrapped to the width, the newest at the bottom unless scrolled back.
	constexpr float BarWidth = 3.0f;
	const float TextWidth = L.History.GetSize().X - TagWidth - BarWidth - 8.0f;
	TArray<FDisplayLine> Lines;
	int64 LastId = 0;
	for (const FScctChatEntry& Line : Entries)
	{
		if (Shows(Line))
		{
			WrapEntry(Painter, Line, TextWidth, Lines);
			LastId = Line.Id;
		}
	}
	VisibleLines = FMath::Max(FMath::FloorToInt32(L.History.GetSize().Y / LineHeight), 1);
	if (PaintedLastId >= 0 && LastId != PaintedLastId && Scroll > 0)
	{
		// New lines while scrolled back: the view stays on what it showed, and the keys row says there is more.
		Scroll += FMath::Max(Lines.Num() - PaintedLines, 0);
		bNewBelow = true;
	}
	PaintedLastId = LastId;
	PaintedLines = Lines.Num();
	Scroll = FMath::Clamp(Scroll, 0, FMath::Max(PaintedLines - VisibleLines, 0));
	if (Scroll == 0)
	{
		bNewBelow = false;
	}

	if (Lines.Num() == 0)
	{
		Painter.Text(FVector2f(L.History.Min.X, L.History.Max.Y - LineHeight), Filter == EFilter::All ? TEXT("No messages yet.") : TEXT("No messages on this channel."), LineFont, TextFaint());
	}
	float Y = L.History.Max.Y - LineHeight;
	for (int32 Index = Lines.Num() - 1 - Scroll, Drawn = 0; Index >= 0 && Drawn < VisibleLines; --Index, ++Drawn)
	{
		DrawLine(Painter, Lines[Index], FVector2f(L.History.Min.X, Y), TagWidth, Team);
		Y -= LineHeight;
	}
	if (PaintedLines > VisibleLines)
	{
		// The scroll bar: where the view is in the whole history.
		const FBox2f Track(FVector2f(L.History.Max.X - BarWidth, L.History.Min.Y), L.History.Max);
		const float TrackHeight = Track.GetSize().Y;
		const float Thumb = FMath::Max(TrackHeight * VisibleLines / PaintedLines, 18.0f);
		const float Bottom = Track.Max.Y - (TrackHeight - Thumb) * Scroll / FMath::Max(PaintedLines - VisibleLines, 1);
		Painter.Fill(Track, FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), 1.5f);
		Painter.Fill(FBox2f(FVector2f(Track.Min.X, Bottom - Thumb), FVector2f(Track.Max.X, Bottom)), Fade(Text(), 0.45f), 1.5f);
	}

	// The composer: the channel the line goes to, the box (the text box itself is the child drawn over it), the count.
	const FLinearColor ChannelCol = bTeam ? Accent(Team) : Srgb(176, 199, 184);
	const FBox2f Chip(L.Composer.Min, FVector2f(L.Composer.Min.X + 64.0f, L.Composer.Max.Y));
	Painter.Fill(Chip, Fade(ChannelCol, 0.14f), 4.0f, Fade(ChannelCol, 0.65f), 1.0f);
	Painter.Text(FVector2f(Chip.GetCenter().X, Chip.Min.Y + 8.0f), bTeam ? TEXT("TEAM") : TEXT("ALL"), Label(9.0f), ChannelCol, EAlign::Centre);
	const FBox2f Field(FVector2f(L.Entry.Min.X - 6.0f, L.Composer.Min.Y), FVector2f(L.Composer.Max.X, L.Composer.Max.Y));
	Painter.Fill(Field, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 4.0f, Fade(ChannelCol, 0.45f), 1.0f);
	const int32 Typed = GetDraft().Len();
	Painter.Text(FVector2f(L.Composer.Max.X - 8.0f, L.Composer.Min.Y + 8.0f), FString::Printf(TEXT("%d/%d"), Typed, MaxChars), SmallFont, Typed >= MaxChars ? Danger() : TextFaint(), EAlign::Right);

	const FString Hint = bNewBelow
		? FString(TEXT("New lines below  -  PgDn to follow"))
		: FString(TEXT("Enter send   Tab channel   Ctrl+Tab filter   PgUp/PgDn scroll   Esc close"));
	Painter.Text(FVector2f(L.Hint.Min.X, L.Hint.Min.Y), Hint, SmallFont, bNewBelow ? Accent(Team) : TextFaint());

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Painter.GetLayer() + 1, InWidgetStyle, bParentEnabled);
}
