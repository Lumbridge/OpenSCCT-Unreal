#include "Hud/ScctHudWidget.h"

#include "Framework/Application/SlateApplication.h"
#include "Hud/SScctHudCanvas.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudStyle.h"
#include "Menu/ScctMenuStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The longest line the original's chat takes (Engine.Console s_ChatTyping: Len(TypedStr) < 100). */
	constexpr int32 MaxChatChars = 100;

	/** Above the status block and the vision chip, where the log's newest line otherwise is (ScctHudElements::PaintLog). */
	constexpr float ChatBottom = ScctHudStyle::Margin + 72.0f + 16.0f;
	constexpr float ChatWidth = 520.0f;
}

TSharedRef<SWidget> UScctHudWidget::RebuildWidget()
{
	const TWeakObjectPtr<UScctHudWidget> Weak(this);
	const auto ChannelColour = [Weak]
	{
		const UScctHudWidget* Widget = Weak.Get();
		const int32 Team = Widget != nullptr && Widget->Model.IsValid() ? Widget->Model->LocalTeam : 0;
		return FSlateColor(Widget != nullptr && Widget->bChatTeam ? ScctHudStyle::Accent(Team) : ScctHudStyle::Text());
	};

	static const FEditableTextStyle ChatStyle = []
	{
		FEditableTextStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>(TEXT("NormalEditableText"));
		Style.SetFont(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 11.5f));
		Style.SetColorAndOpacity(ScctHudStyle::Text());
		return Style;
	}();

	return SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		[
			SNew(SScctHudCanvas)
			.Model(Model)
			.Layer(SScctHudCanvas::ELayer::FullScreen)
		]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone)
			.IsTitleSafe(true)
			.Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SScctHudCanvas)
				.Model(Model)
				.Layer(SScctHudCanvas::ELayer::SafeArea)
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone)
			.IsTitleSafe(true)
			.Visibility(EVisibility::SelfHitTestInvisible)
			[
				SNew(SBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(ScctHudStyle::Margin, 0.0f, 0.0f, ChatBottom))
				.Visibility(TAttribute<EVisibility>::CreateLambda([Weak] { return Weak.IsValid() && Weak->bChatOpen ? EVisibility::Visible : EVisibility::Collapsed; }))
				[
					SNew(SBox)
					.WidthOverride(ChatWidth)
					[
						ScctMenuStyle::Glass(
							SNew(SHorizontalBox)
							// Who reads the line: everyone, or the team (in the team's colour).
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(TAttribute<FText>::CreateLambda([Weak] { return FText::FromString(Weak.IsValid() && Weak->bChatTeam ? TEXT("TEAM") : TEXT("ALL")); }))
								.Font(ScctHudStyle::Label(8.5f))
								.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda(ChannelColour))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SAssignNew(ChatText, SEditableText)
								.Style(&ChatStyle)
								.ClearKeyboardFocusOnCommit(false)
								.SelectAllTextWhenFocused(false)
								.RevertTextOnEscape(false)
								.OnTextChanged_Lambda([Weak](const FText& Text)
								{
									// The original's line stops taking characters at its limit.
									UScctHudWidget* Widget = Weak.Get();
									if (Widget != nullptr && Widget->ChatText.IsValid() && Text.ToString().Len() > MaxChatChars)
									{
										Widget->ChatText->SetText(FText::FromString(Text.ToString().Left(MaxChatChars)));
									}
								})
								.OnTextCommitted_Lambda([Weak](const FText&, ETextCommit::Type Commit)
								{
									if (Commit == ETextCommit::OnEnter && Weak.IsValid())
									{
										Weak->SubmitChat();
									}
								})
								.OnKeyDownHandler_Lambda([Weak](const FGeometry&, const FKeyEvent& Key)
								{
									UScctHudWidget* Widget = Weak.Get();
									if (Widget == nullptr || !Widget->bChatOpen)
									{
										return FReply::Unhandled();
									}
									if (Key.GetKey() == EKeys::Escape)
									{
										Widget->CloseChat();
										return FReply::Handled();
									}
									if (Key.GetKey() == EKeys::Tab)
									{
										Widget->bChatTeam = !Widget->bChatTeam;
										return FReply::Handled();
									}
									return FReply::Unhandled();
								})
							],
							FMargin(14.0f, 8.0f), 0.72f)
					]
				]
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SScctHudCanvas)
			.Model(Model)
			.Layer(SScctHudCanvas::ELayer::Blinding)
		];
}

void UScctHudWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ChatText.Reset();
}

void UScctHudWidget::OpenChat(bool bTeamOnly)
{
	if (!ChatText.IsValid())
	{
		return;
	}
	bChatTeam = bTeamOnly;
	if (!bChatOpen)
	{
		bChatOpen = true;
		ChatText->SetText(FText::GetEmpty());
	}
	// The line is laid out from the next frame on; the focus can be given now, by the widget's path in the window.
	FSlateApplication::Get().SetKeyboardFocus(ChatText, EFocusCause::SetDirectly);
}

void UScctHudWidget::CloseChat()
{
	if (!bChatOpen)
	{
		return;
	}
	bChatOpen = false;
	if (ChatText.IsValid())
	{
		ChatText->SetText(FText::GetEmpty());
	}
	OnChatClosed.ExecuteIfBound();
}

void UScctHudWidget::SetChatText(const FString& Text)
{
	if (bChatOpen && ChatText.IsValid())
	{
		ChatText->SetText(FText::FromString(Text.Left(MaxChatChars)));
	}
}

void UScctHudWidget::SubmitChat()
{
	if (!bChatOpen || !ChatText.IsValid())
	{
		return;
	}
	const FString Line = ChatText->GetText().ToString().TrimStartAndEnd();
	const bool bTeamOnly = bChatTeam;
	CloseChat();
	if (!Line.IsEmpty())
	{
		OnChatSubmitted.ExecuteIfBound(Line, bTeamOnly);
	}
}
