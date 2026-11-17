#include "Voice/SScctVoiceLobbyControls.h"

#include "Brushes/SlateColorBrush.h"
#include "HAL/PlatformTime.h"
#include "Hud/ScctHudStyle.h"
#include "Voice/ScctVoiceSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ScctVoiceLobby"

namespace
{
	/** Seconds a first click on Voice Ban waits for the second that confirms it (Godot asks in a dialog). */
	constexpr double ConfirmSeconds = 4.0;

	DECLARE_DELEGATE_OneParam(FScctVoiceWheel, int32 /*Steps*/);

	/**
	 * A small piece of text that is a control: a click runs OnClick, the wheel OnWheel, and it lights up under the
	 * cursor. The lobby's columns are narrow, so the voice controls are words rather than buttons.
	 */
	class SScctVoiceLobbyText : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctVoiceLobbyText) {}
			SLATE_ATTRIBUTE(FText, Text)
			SLATE_ATTRIBUTE(FSlateColor, Colour)
			SLATE_ARGUMENT(FText, ToolTip)
			SLATE_EVENT(FSimpleDelegate, OnClick)
			SLATE_EVENT(FScctVoiceWheel, OnWheel)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnClick = InArgs._OnClick;
			OnWheel = InArgs._OnWheel;
			static const FSlateColorBrush Brush(FLinearColor::White);
			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(&Brush)
				.Padding(FMargin(4.0f, 1.0f))
				.ToolTipText(InArgs._ToolTip)
				.BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this]
				{
					return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, IsHovered() ? 0.14f : 0.0f));
				}))
				[
					SNew(STextBlock)
					.Text(InArgs._Text)
					.Font(ScctHudStyle::Label(8.5f))
					.ColorAndOpacity(InArgs._Colour)
				]
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !OnClick.IsBound())
			{
				return FReply::Unhandled();
			}
			OnClick.Execute();
			return FReply::Handled();
		}

		virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!OnWheel.IsBound() || FMath::IsNearlyZero(Event.GetWheelDelta()))
			{
				return FReply::Unhandled();
			}
			OnWheel.Execute(Event.GetWheelDelta() > 0.0f ? 1 : -1);
			return FReply::Handled();
		}

		virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}

	private:
		FSimpleDelegate OnClick;
		FScctVoiceWheel OnWheel;
	};
}

void SScctVoiceLobbyControls::Construct(const FArguments& InArgs)
{
	Voice = InArgs._Voice;
	PlayerName = InArgs._PlayerName;
	PlayerId = InArgs._PlayerId;
	bLocal = InArgs._Local;
	const int32 Team = InArgs._Team;
	const bool bBanned = InArgs._VoiceBanned;

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
	ChildSlot
	[
		Box
	];
	if (InArgs._Bot)
	{
		// Bots never talk: nothing to show or set.
		return;
	}

	const TWeakObjectPtr<UScctVoiceSubsystem> WeakVoice = Voice;
	const FString Name = PlayerName;
	auto Read = [WeakVoice, Name]
	{
		const UScctVoiceSubsystem* Subsystem = WeakVoice.Get();
		if (Subsystem == nullptr)
		{
			FScctVoicePlayer None = {};
			None.Volume = 1.0f;
			return None;
		}
		return Subsystem->GetPlayerVoice(Name);
	};

	// The speaking mark: the side's colour while heard (the local player's while he transmits), a faint dot otherwise.
	Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT("●")))
		.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 10.0f))
		.ToolTipText(LOCTEXT("SpeakingTip", "Lit while this player is heard"))
		.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([Read, Team]
		{
			return FSlateColor(Read().Speaking != 0 ? ScctHudStyle::Accent(Team) : ScctHudStyle::Fade(ScctHudStyle::TextFaint(), 0.5f));
		}))
	];
	if (bLocal)
	{
		return;
	}

	// "Voice volume" and "Mute" in one: the percentage (or MUTED); the wheel sets the volume, a click mutes.
	const TWeakPtr<SScctVoiceLobbyControls> Weak = SharedThis(this);
	Box->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(SScctVoiceLobbyText)
		.ToolTip(LOCTEXT("VolumeTip", "Voice volume of this player: scroll to change it, click to mute or unmute"))
		.Text(TAttribute<FText>::CreateLambda([Read]
		{
			const FScctVoicePlayer Player = Read();
			return Player.Muted != 0 ? LOCTEXT("Muted", "MUTED")
				: FText::Format(LOCTEXT("Volume", "{0}%"), FText::AsNumber(FMath::RoundToInt32(Player.Volume * 100.0f)));
		}))
		.Colour(TAttribute<FSlateColor>::CreateLambda([Read]
		{
			return FSlateColor(Read().Muted != 0 ? ScctHudStyle::Danger() : ScctHudStyle::TextDim());
		}))
		.OnClick(FSimpleDelegate::CreateLambda([WeakVoice, Name, Read]
		{
			if (UScctVoiceSubsystem* Subsystem = WeakVoice.Get())
			{
				Subsystem->SetPlayerMuted(Name, Read().Muted == 0);
			}
		}))
		.OnWheel(FScctVoiceWheel::CreateLambda([Weak](int32 Steps)
		{
			if (const TSharedPtr<SScctVoiceLobbyControls> Pinned = Weak.Pin())
			{
				Pinned->Step(Steps);
			}
		}))
	];

	if (InArgs._CanBan && PlayerId > 0)
	{
		// Two clicks: the first asks, the second (within a few seconds) sends.
		TSharedRef<double> ArmedAt = MakeShared<double>(-1.0);
		const int32 Id = PlayerId;
		auto Armed = [ArmedAt] { return *ArmedAt >= 0.0 && FPlatformTime::Seconds() - *ArmedAt < ConfirmSeconds; };
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SScctVoiceLobbyText)
			.ToolTip(bBanned ? LOCTEXT("UnbanTip", "Let this player use voice chat again") : LOCTEXT("BanTip", "Ban this player from voice chat"))
			.Text(TAttribute<FText>::CreateLambda([Armed, bBanned]
			{
				return Armed() ? LOCTEXT("BanSure", "SURE?") : bBanned ? LOCTEXT("VoiceUnban", "VOICE UNBAN") : LOCTEXT("VoiceBan", "VOICE BAN");
			}))
			.Colour(TAttribute<FSlateColor>::CreateLambda([Armed]
			{
				return FSlateColor(Armed() ? ScctHudStyle::Danger() : ScctHudStyle::TextFaint());
			}))
			.OnClick(FSimpleDelegate::CreateLambda([WeakVoice, Id, bBanned, ArmedAt, Armed]
			{
				if (!Armed())
				{
					*ArmedAt = FPlatformTime::Seconds();
					return;
				}
				*ArmedAt = -1.0;
				if (UScctVoiceSubsystem* Subsystem = WeakVoice.Get())
				{
					Subsystem->SetVoiceBanned(Id, !bBanned);
				}
			}))
		];
	}
}

FReply SScctVoiceLobbyControls::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bLocal || FMath::IsNearlyZero(Event.GetWheelDelta()))
	{
		return FReply::Unhandled();
	}
	Step(Event.GetWheelDelta() > 0.0f ? 1 : -1);
	return FReply::Handled();
}

void SScctVoiceLobbyControls::Step(int32 Steps) const
{
	UScctVoiceSubsystem* Subsystem = Voice.Get();
	if (Subsystem == nullptr)
	{
		return;
	}
	const float Volume = Subsystem->GetPlayerVoice(PlayerName).Volume;
	Subsystem->SetPlayerVolume(PlayerName, FMath::Clamp(FMath::RoundToFloat((Volume + Steps * 0.1f) * 10.0f) / 10.0f, 0.0f, 2.0f));
}

#undef LOCTEXT_NAMESPACE
