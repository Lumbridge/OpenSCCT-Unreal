#include "Demo/SScctDemoHud.h"

#include "Brushes/SlateColorBrush.h"
#include "Demo/ScctDemoPlayback.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/App.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace DemoHudUi = ScctMenuStyle;

namespace
{
	constexpr float DemoHudHideAfter = 4.0f;
	const TCHAR* const DemoHudDefaultPreview = TEXT("Click or drag to seek · select a marker to watch the event");
	const float DemoHudSpeeds[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };

	FLinearColor DemoHudMarkerColour(int32 Kind)
	{
		switch (static_cast<EScctDemoMarkerKind>(Kind))
		{
		case EScctDemoMarkerKind::Kill: return FLinearColor(1.0f, 0.42f, 0.38f);
		case EScctDemoMarkerKind::RoundStart:
		case EScctDemoMarkerKind::RoundEnd: return FLinearColor(1.0f, 0.8f, 0.32f);
		default: return FLinearColor(0.48f, 0.74f, 1.0f);
		}
	}

	const TCHAR* DemoHudMarkerName(int32 Kind)
	{
		switch (static_cast<EScctDemoMarkerKind>(Kind))
		{
		case EScctDemoMarkerKind::RoundStart: return TEXT("Round start");
		case EScctDemoMarkerKind::RoundEnd: return TEXT("Round end");
		case EScctDemoMarkerKind::Kill: return TEXT("Death");
		case EScctDemoMarkerKind::Objective: return TEXT("Objective");
		case EScctDemoMarkerKind::MatchEnd: return TEXT("Match end");
		default: return TEXT("Event");
		}
	}

	TSharedRef<SWidget> DemoHudText(const TAttribute<FText>& Text, float Size, const FLinearColor& Colour = ScctHudStyle::Text(), ScctHudStyle::EWeight Weight = ScctHudStyle::EWeight::Regular)
	{
		return DemoHudUi::Text(Text, Size, FSlateColor(Colour), Weight);
	}

	/**
	 * The timeline (DemoSeekBar): the played part, the markers, a seek's target, the playhead; click or drag to seek,
	 * a marker within six pixels seeks three seconds before it.
	 */
	class SScctDemoTimeline : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctDemoTimeline) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TWeakPtr<SScctDemoHud>& InHud) { Hud = InHud; }

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(200.0f, 26.0f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			static const FSlateColorBrush White(FLinearColor::White);
			const TSharedPtr<SScctDemoHud> Owner = Hud.Pin();
			if (!Owner.IsValid())
			{
				return Layer;
			}
			const FScctDemoPlayInfo& Info = Owner->GetInfo();
			const FVector2D Size = Geometry.GetLocalSize();
			const float Middle = Size.Y * 0.5f;
			const auto Box = [&](float X, float Y, float W, float H, const FLinearColor& Colour, int32 At)
			{
				FSlateDrawElement::MakeBox(Out, At, Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))), &White, ESlateDrawEffect::None, Colour);
			};
			Box(0.0f, Middle - 3.0f, Size.X, 6.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), Layer);
			Box(0.0f, Middle - 3.0f, XOf(Info.CurrentTick, Size.X), 6.0f, DemoHudUi::Accent(), Layer + 1);
			for (const FScctDemoMarker& Marker : Owner->GetMarkers())
			{
				if (Marker.Tick < Info.FirstTick || Marker.Tick > Info.LastTick)
				{
					continue;
				}
				Box(XOf(Marker.Tick, Size.X) - 1.5f, Middle - 10.0f, 3.0f, 20.0f, DemoHudMarkerColour(Marker.Kind), Layer + 2);
			}
			if ((Info.Flags & EScctDemoPlayFlags::Seeking) != 0 && Info.SeekTarget > 0)
			{
				Box(XOf(Info.SeekTarget, Size.X) - 1.0f, 1.0f, 2.0f, Size.Y - 2.0f, FLinearColor::White, Layer + 3);
			}
			Box(XOf(Info.CurrentTick, Size.X) - 5.0f, Middle - 5.0f, 10.0f, 10.0f, DemoHudUi::Accent(), Layer + 4);
			if (bHovering || bDragging)
			{
				Box(HoverX - 0.5f, 1.0f, 1.0f, Size.Y - 2.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), Layer + 5);
			}
			return Layer + 6;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			bDragging = true;
			Preview(Geometry, Event);
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !bDragging)
			{
				return FReply::Unhandled();
			}
			bDragging = false;
			Preview(Geometry, Event);
			if (const TSharedPtr<SScctDemoHud> Owner = Hud.Pin())
			{
				int32 Marker = INDEX_NONE;
				const int32 Tick = TickAt(HoverX, Geometry.GetLocalSize().X, Marker);
				if (Marker != INDEX_NONE)
				{
					Owner->Command(EScctDemoCommand::SeekMarker, Marker);
				}
				else
				{
					Owner->Command(EScctDemoCommand::Seek, Tick);
				}
			}
			return FReply::Handled().ReleaseMouseCapture();
		}

		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			Preview(Geometry, Event);
			return FReply::Handled();
		}

		virtual void OnMouseLeave(const FPointerEvent& Event) override
		{
			bHovering = false;
			if (!bDragging)
			{
				if (const TSharedPtr<SScctDemoHud> Owner = Hud.Pin())
				{
					Owner->SetPreview(DemoHudDefaultPreview);
				}
			}
		}

		virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}

	private:
		float XOf(int32 Tick, float Width) const
		{
			const TSharedPtr<SScctDemoHud> Owner = Hud.Pin();
			const FScctDemoPlayInfo& Info = Owner->GetInfo();
			const float Span = FMath::Max(1.0f, static_cast<float>(Info.LastTick - Info.FirstTick));
			return Width * static_cast<float>(FMath::Clamp(Tick, Info.FirstTick, Info.LastTick) - Info.FirstTick) / Span;
		}

		int32 TickAt(float X, float Width, int32& OutMarker) const
		{
			OutMarker = INDEX_NONE;
			const TSharedPtr<SScctDemoHud> Owner = Hud.Pin();
			const FScctDemoPlayInfo& Info = Owner->GetInfo();
			float Closest = 6.0f;
			const TArray<FScctDemoMarker>& All = Owner->GetMarkers();
			for (int32 Index = 0; Index < All.Num(); ++Index)
			{
				if (All[Index].Tick < Info.FirstTick || All[Index].Tick > Info.LastTick)
				{
					continue;
				}
				const float Distance = FMath::Abs(XOf(All[Index].Tick, Width) - X);
				if (Distance <= Closest)
				{
					Closest = Distance;
					OutMarker = Index;
				}
			}
			return Info.FirstTick + FMath::RoundToInt((Info.LastTick - Info.FirstTick) * FMath::Clamp(X / FMath::Max(Width, 1.0f), 0.0f, 1.0f));
		}

		void Preview(const FGeometry& Geometry, const FPointerEvent& Event)
		{
			const TSharedPtr<SScctDemoHud> Owner = Hud.Pin();
			if (!Owner.IsValid())
			{
				return;
			}
			bHovering = true;
			const float Width = Geometry.GetLocalSize().X;
			HoverX = FMath::Clamp(static_cast<float>(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).X), 0.0f, Width);
			int32 Marker = INDEX_NONE;
			const int32 Tick = TickAt(HoverX, Width, Marker);
			const FScctDemoPlayInfo& Info = Owner->GetInfo();
			if (Marker != INDEX_NONE)
			{
				const FScctDemoMarker& Found = Owner->GetMarkers()[Marker];
				const FString Text(Found.Text);
				Owner->SetPreview(FString::Printf(TEXT("%s · %s%s · 3 s lead-in"), *Owner->Clock(Found.Tick - Info.FirstTick),
					DemoHudMarkerName(Found.Kind), Text.TrimStartAndEnd().IsEmpty() ? TEXT("") : *(TEXT(": ") + Text)));
			}
			else
			{
				Owner->SetPreview(TEXT("Seek to ") + Owner->Clock(Tick - Info.FirstTick));
			}
		}

		TWeakPtr<SScctDemoHud> Hud;
		float HoverX = 0.0f;
		bool bHovering = false;
		bool bDragging = false;
	};
}

void SScctDemoHud::Construct(const FArguments& InArgs)
{
	OnLeave = InArgs._OnLeave;
	Preview = DemoHudDefaultPreview;
	// Nobody moves the mouse in an unattended run: the bar stays up for its screenshots.
	bAlwaysShown = FApp::IsUnattended();
	if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
	{
		Api->GetPlayInfo(&Info);
	}

	const TWeakPtr<SScctDemoHud> Weak = SharedThis(this);
	ChildSlot
	[
		SNew(SOverlay)
		// "Seeking to 1:20..." over the picture while a seek runs.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 300.0f, 0.0f, 0.0f)
		[
			SNew(SBox).Visibility_Lambda([this] { return (Info.Flags & EScctDemoPlayFlags::Seeking) != 0 && !bConfirmingLeave ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				DemoHudText(TAttribute<FText>::CreateLambda([this]
				{
					const int32 Target = Info.SeekTarget > 0 ? Info.SeekTarget : Info.CurrentTick;
					return DemoHudUi::ToText(FString::Printf(TEXT("Seeking to %s…"), *Clock(FMath::Clamp(Target, Info.FirstTick, Info.LastTick) - Info.FirstTick)));
				}), 26.0f, ScctHudStyle::Text(), ScctHudStyle::EWeight::Light)
			]
		]
		// Why the demo could not be played.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(620.0f).Visibility_Lambda([this] { return Info.State == static_cast<int32>(EScctDemoPlayState::Playing) ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				DemoHudUi::Glass(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ DemoHudUi::Label(TEXT("Demo")) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
					[
						DemoHudUi::Text(TAttribute<FText>::CreateLambda([]
						{
							const FString Why = UScctDemoPlayback::PlayText(EScctDemoPlayText::Error);
							return DemoHudUi::ToText(Why.IsEmpty() ? TEXT("Loading the demo…") : TEXT("This demo cannot be played: ") + Why);
						}), 15.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						DemoHudUi::Button(DemoHudUi::ToText(TEXT("BACK TO DEMOS")), FOnClicked::CreateLambda([this] { OnLeave.ExecuteIfBound(); return FReply::Handled(); }))
					])
			]
		]
		// The playback bar.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(16.0f)
		[
			SNew(SBox).WidthOverride(1280.0f)
			.Visibility_Lambda([this] { return IsBarShown() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				BuildBar()
			]
		]
		// Show controls, while the bar is hidden.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(16.0f)
		[
			SNew(SBox).Visibility_Lambda([this] { return Info.State == static_cast<int32>(EScctDemoPlayState::Playing) && !IsBarShown() && !bConfirmingLeave ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				DemoHudUi::Button(DemoHudUi::ToText(TEXT("SHOW CONTROLS · H")), FOnClicked::CreateLambda([this] { bHidden = false; Idle = 0.0f; return FReply::Handled(); }))
			]
		]
		// Back to demos? (DemoHud.ConfirmLeave)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(560.0f).Visibility_Lambda([this] { return bConfirmingLeave ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				DemoHudUi::Glass(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
					[
						DemoHudUi::Text(DemoHudUi::ToText(TEXT("Stop watching this demo and go back to the Demos page?")), 16.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							DemoHudUi::Button(DemoHudUi::ToText(TEXT("KEEP WATCHING")), FOnClicked::CreateLambda([this] { AnswerLeave(false); return FReply::Handled(); }))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							DemoHudUi::Button(DemoHudUi::ToText(TEXT("BACK TO DEMOS")), FOnClicked::CreateLambda([this] { AnswerLeave(true); return FReply::Handled(); }), true)
						]
					])
			]
		]
	];
}

TSharedRef<SWidget> SScctDemoHud::BuildBar()
{
	const auto Chip = [this](const FString& Label, TFunction<void()> OnPressed, TFunction<bool()> IsSelected, float MinWidth = 0.0f)
	{
		return DemoHudUi::Button(DemoHudUi::ToText(Label), FOnClicked::CreateLambda([OnPressed] { OnPressed(); return FReply::Handled(); }),
			TAttribute<bool>::CreateLambda([IsSelected] { return IsSelected ? IsSelected() : false; }), DemoHudUi::Accent(), MinWidth);
	};
	const auto Row = [](const TSharedRef<SHorizontalBox>& Box, const TSharedRef<SWidget>& Widget)
	{
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ Widget ];
	};

	// The heading: the demo, its state and who is watched.
	const FString Title = FString::Printf(TEXT("DEMO  /  %s  /  %s"), *UScctDemoPlayback::PlayText(EScctDemoPlayText::Map), *UScctDemoPlayback::PlayText(EScctDemoPlayText::Mode));
	const TSharedRef<SHorizontalBox> Heading = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ DemoHudText(DemoHudUi::ToText(Title), 15.0f) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.0f, 0.0f)
		[
			DemoHudText(TAttribute<FText>::CreateLambda([] { return DemoHudUi::ToText(TEXT("WATCHING  ") + UScctDemoPlayback::PlayText(EScctDemoPlayText::Watching)); }), 13.0f, ScctHudStyle::TextDim())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			DemoHudText(TAttribute<FText>::CreateLambda([] { return DemoHudUi::ToText(UScctDemoPlayback::PlayText(EScctDemoPlayText::State)); }), 13.0f, DemoHudUi::Accent(), ScctHudStyle::EWeight::Bold)
		];

	// The time and the timeline's hover text.
	const TSharedRef<SHorizontalBox> Times = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			DemoHudText(TAttribute<FText>::CreateLambda([this]
			{
				return DemoHudUi::ToText(Clock(FMath::Clamp(Info.CurrentTick, Info.FirstTick, Info.LastTick) - Info.FirstTick) + TEXT(" / ") + Clock(Info.LastTick - Info.FirstTick));
			}), 14.0f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
		[
			DemoHudText(TAttribute<FText>::CreateLambda([this] { return DemoHudUi::ToText(Preview); }), 12.5f, ScctHudStyle::TextDim())
		];

	// Restart, -5 s, Pause, +5 s, the speeds, Controls, Hide, Back to demos.
	const TSharedRef<SHorizontalBox> Transport = SNew(SHorizontalBox);
	Row(Transport, Chip(TEXT("RESTART"), [this] { Command(EScctDemoCommand::Restart); }, nullptr));
	Row(Transport, Chip(TEXT("-5 S"), [this] { Command(EScctDemoCommand::Skip, -5); }, nullptr));
	Row(Transport, DemoHudUi::Button(TAttribute<FText>::CreateLambda([this]
	{
		return DemoHudUi::ToText((Info.Flags & EScctDemoPlayFlags::AtEnd) != 0 ? TEXT("REPLAY") : (Info.Flags & EScctDemoPlayFlags::Paused) != 0 ? TEXT("PLAY") : TEXT("PAUSE"));
	}), FOnClicked::CreateLambda([this] { Command(EScctDemoCommand::TogglePause); return FReply::Handled(); }), true, DemoHudUi::Accent(), 86.0f));
	Row(Transport, Chip(TEXT("+5 S"), [this] { Command(EScctDemoCommand::Skip, 5); }, nullptr));
	Transport->AddSlot().AutoWidth().Padding(8.0f, 0.0f, 6.0f, 0.0f).VAlign(VAlign_Center)[ DemoHudUi::Label(TEXT("Speed")) ];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(DemoHudSpeeds); ++Index)
	{
		const FString Label = FString::Printf(TEXT("%s×"), *FString::SanitizeFloat(DemoHudSpeeds[Index], 0));
		Row(Transport, Chip(Label, [this, Index] { Command(EScctDemoCommand::SetSpeed, Index); }, [this, Index] { return Info.SpeedIndex == Index; }));
	}
	Transport->AddSlot().FillWidth(1.0f)[ SNullWidget::NullWidget ];
	Row(Transport, Chip(TEXT("CONTROLS"), [this] { ToggleHelp(); }, [this] { return bHelp; }));
	Row(Transport, Chip(TEXT("HIDE"), [this] { bHidden = true; }, nullptr));
	Transport->AddSlot().AutoWidth()[ Chip(TEXT("BACK TO DEMOS"), [this] { AskLeave(); }, nullptr) ];

	// The players, then the cameras.
	const TSharedRef<SHorizontalBox> Players = SNew(SHorizontalBox);
	Players->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ SNew(SBox).WidthOverride(58.0f)[ DemoHudUi::Label(TEXT("Player")) ] ];
	Row(Players, Chip(TEXT("<"), [this] { Command(EScctDemoCommand::CycleTarget, -1); }, nullptr, 34.0f));
	Players->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
	[
		SAssignNew(PlayerBox, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f))
	];
	Row(Players, Chip(TEXT(">"), [this] { Command(EScctDemoCommand::CycleTarget, 1); }, nullptr, 34.0f));

	const TSharedRef<SHorizontalBox> View = SNew(SHorizontalBox);
	View->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ SNew(SBox).WidthOverride(58.0f)[ DemoHudUi::Label(TEXT("Camera")) ] ];
	static const TCHAR* const Cameras[] = { TEXT("FIRST PERSON"), TEXT("THIRD PERSON"), TEXT("FREE CAMERA"), TEXT("AS PLAYED") };
	for (int32 Camera = 0; Camera < UE_ARRAY_COUNT(Cameras); ++Camera)
	{
		Row(View, Chip(Cameras[Camera], [this, Camera] { Command(EScctDemoCommand::SetCamera, Camera); }, [this, Camera] { return Info.Camera == Camera; }));
	}

	// The legend of the markers.
	const TSharedRef<SHorizontalBox> Legend = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)[ DemoHudText(DemoHudUi::ToText(TEXT("• Round")), 12.0f, DemoHudMarkerColour(0)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)[ DemoHudText(DemoHudUi::ToText(TEXT("• Death")), 12.0f, DemoHudMarkerColour(2)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 18.0f, 0.0f)[ DemoHudText(DemoHudUi::ToText(TEXT("• Event")), 12.0f, DemoHudMarkerColour(3)) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			DemoHudText(TAttribute<FText>::CreateLambda([] { return DemoHudUi::ToText(UScctDemoPlayback::PlayText(EScctDemoPlayText::Hint)); }), 12.0f, ScctHudStyle::TextDim())
		];

	const FString Help =
		TEXT("Space  Play / pause     Left / Right  Seek 5 s (Shift: 30 s)     Up / Down  Speed     Home  Restart     End  The end\n")
		TEXT("C  Camera     [ / ] or wheel  Change player     1-9  Select player     H  Hide / show controls\n")
		TEXT("Third person follows the player. Hold right mouse to orbit; release to return behind the player.\n")
		TEXT("As played follows the recorded camera, including aiming and gadget views.\n")
		TEXT("Free camera: hold right mouse to look, W A S D move, Q / E down / up, Shift move faster.\n")
		TEXT("Click or drag the timeline to seek; a marker seeks three seconds before its event. Rewind starts from the nearest checkpoint when the demo carries checkpoints.\n")
		TEXT("Esc  Close help / confirm returning to demos");

	return DemoHudUi::Glass(SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Heading ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)[ SNew(SScctDemoTimeline, SharedThis(this)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ Times ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Transport ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Players ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ View ]
		+ SVerticalBox::Slot().AutoHeight()[ Legend ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SBox).Visibility_Lambda([this] { return bHelp ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				DemoHudUi::Text(DemoHudUi::ToText(Help), 12.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
			]
		], FMargin(16.0f, 12.0f), 0.82f);
}

void SScctDemoHud::RebuildPlayers()
{
	FString Signature;
	for (const FScctDemoTarget& Target : Targets)
	{
		Signature += FString::Printf(TEXT("%d:%s:%d\n"), Target.PlayerId, Target.Name, Target.Team);
	}
	if (Signature == RosterSignature && PlayerBox.IsValid() && PlayerBox->GetChildren()->Num() > 0)
	{
		return;
	}
	RosterSignature = Signature;
	if (!PlayerBox.IsValid())
	{
		return;
	}
	PlayerBox->ClearChildren();
	if (Targets.Num() == 0)
	{
		PlayerBox->AddSlot()[ DemoHudText(DemoHudUi::ToText(TEXT("No players available")), 12.5f, ScctHudStyle::TextDim()) ];
		return;
	}
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		const FScctDemoTarget& Target = Targets[Index];
		const int32 PlayerId = Target.PlayerId;
		const FLinearColor Side = ScctHudStyle::Accent(Target.Team == 1 ? 1 : 0);
		PlayerBox->AddSlot()
		[
			DemoHudUi::Button(DemoHudUi::ToText(FString::Printf(TEXT("%d  %s · %s"), Index + 1, Target.Name, Target.Team == 1 ? TEXT("Merc") : TEXT("Spy"))),
				FOnClicked::CreateLambda([this, PlayerId] { Command(EScctDemoCommand::WatchPlayer, PlayerId); return FReply::Handled(); }),
				TAttribute<bool>::CreateLambda([this, Index] { return Info.TargetIndex == Index; }), Side)
		];
	}
}

void SScctDemoHud::DriveFrame(float DeltaSeconds)
{
	const FScctDemoPlayApi* Api = UScctDemoPlayback::Api();
	if (Api == nullptr)
	{
		return;
	}

	const int32 OldRevision = Info.Revision;
	const int32 OldMarkers = Markers.Num();
	Api->GetPlayInfo(&Info);
	Idle += DeltaSeconds;

	if (Info.MarkerCount != OldMarkers)
	{
		Markers.SetNumZeroed(Info.MarkerCount);
		Markers.SetNum(FMath::Max(Api->GetMarkers(Markers.GetData(), Markers.Num()), 0));
	}
	Targets.SetNumZeroed(16);
	Targets.SetNum(FMath::Clamp(Api->GetTargets(Targets.GetData(), Targets.Num()), 0, 16));
	RebuildPlayers();

	// A camera that no longer takes the mouse lets it go (the player cycled the camera while looking).
	if (bLooking && (Info.Flags & EScctDemoPlayFlags::CanLook) == 0)
	{
		SetLooking(false);
	}

	// The free camera flies on the held keys, in real time (paused or not).
	if (Info.Camera == static_cast<int32>(EScctDemoCamera::Free) && !bConfirmingLeave)
	{
		const auto Axis = [this](const FKey& Positive, const FKey& Negative)
		{
			return (Held.Contains(Positive) ? 1.0f : 0.0f) - (Held.Contains(Negative) ? 1.0f : 0.0f);
		};
		const float Forward = Axis(EKeys::W, EKeys::S);
		const float Right = Axis(EKeys::D, EKeys::A);
		const float Up = Axis(EKeys::E, EKeys::Q);
		if (Forward != 0.0f || Right != 0.0f || Up != 0.0f)
		{
			Api->Fly(Forward, Right, Up, Held.Contains(EKeys::LeftShift) || Held.Contains(EKeys::RightShift) ? 1 : 0, DeltaSeconds);
		}
	}
	(void)OldRevision;
}

void SScctDemoHud::Command(EScctDemoCommand Which, int32 Arg)
{
	Idle = 0.0f;
	if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
	{
		if (Which == EScctDemoCommand::CycleCamera || Which == EScctDemoCommand::SetCamera || Which == EScctDemoCommand::CycleTarget
			|| Which == EScctDemoCommand::WatchIndex || Which == EScctDemoCommand::WatchPlayer)
		{
			SetLooking(false);
		}
		Api->Control(static_cast<int32>(Which), Arg);
		Api->GetPlayInfo(&Info);
	}
}

void SScctDemoHud::SetPreview(const FString& Text)
{
	Preview = Text;
	Idle = 0.0f;
}

FString SScctDemoHud::Clock(int32 Ticks) const
{
	const int32 Seconds = FMath::Max(Ticks, 0) / FMath::Max(Info.TickRate, 1);
	return Seconds >= 3600
		? FString::Printf(TEXT("%d:%02d:%02d"), Seconds / 3600, Seconds / 60 % 60, Seconds % 60)
		: FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
}

bool SScctDemoHud::IsBarShown() const
{
	if (Info.State != static_cast<int32>(EScctDemoPlayState::Playing) || bConfirmingLeave || bHidden)
	{
		return false;
	}
	return bAlwaysShown || Idle < DemoHudHideAfter || bHelp || (Info.Flags & (EScctDemoPlayFlags::Paused | EScctDemoPlayFlags::Seeking)) != 0;
}

void SScctDemoHud::SetLooking(bool bInLooking)
{
	if (bLooking == bInLooking)
	{
		return;
	}
	bLooking = bInLooking;
	if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
	{
		Api->Control(static_cast<int32>(EScctDemoCommand::SetLooking), bLooking ? 1 : 0);
	}
}

void SScctDemoHud::ToggleHelp()
{
	bHelp = !bHelp;
	bHidden = false;
	Idle = 0.0f;
}

void SScctDemoHud::AskLeave()
{
	if (bConfirmingLeave)
	{
		return;
	}
	SetLooking(false);
	bConfirmingLeave = true;
	bWasPausedBeforeConfirm = (Info.Flags & EScctDemoPlayFlags::Paused) != 0;
	Command(EScctDemoCommand::SetPaused, 1);
}

void SScctDemoHud::AnswerLeave(bool bLeave)
{
	bConfirmingLeave = false;
	Idle = 0.0f;
	if (bLeave)
	{
		OnLeave.ExecuteIfBound();
		return;
	}
	Command(EScctDemoCommand::SetPaused, bWasPausedBeforeConfirm ? 1 : 0);
}

FReply SScctDemoHud::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	Idle = 0.0f;
	const FKey Key = InKeyEvent.GetKey();
	Held.Add(Key);
	if (InKeyEvent.IsRepeat())
	{
		return FReply::Unhandled();
	}
	if (bConfirmingLeave)
	{
		if (Key == EKeys::Escape)
		{
			AnswerLeave(false);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (Info.State != static_cast<int32>(EScctDemoPlayState::Playing))
	{
		if (Key == EKeys::Escape)
		{
			OnLeave.ExecuteIfBound();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	const bool bShift = InKeyEvent.IsShiftDown();
	if (Key == EKeys::SpaceBar) { Command(EScctDemoCommand::TogglePause); return FReply::Handled(); }
	if (Key == EKeys::Up) { Command(EScctDemoCommand::StepSpeed, 1); return FReply::Handled(); }
	if (Key == EKeys::Down) { Command(EScctDemoCommand::StepSpeed, -1); return FReply::Handled(); }
	if (Key == EKeys::Left) { Command(EScctDemoCommand::Skip, bShift ? -30 : -5); return FReply::Handled(); }
	if (Key == EKeys::Right) { Command(EScctDemoCommand::Skip, bShift ? 30 : 5); return FReply::Handled(); }
	if (Key == EKeys::Home) { Command(EScctDemoCommand::Restart); return FReply::Handled(); }
	if (Key == EKeys::End) { Command(EScctDemoCommand::SeekEnd); return FReply::Handled(); }
	if (Key == EKeys::C) { Command(EScctDemoCommand::CycleCamera); return FReply::Handled(); }
	if (Key == EKeys::LeftBracket) { Command(EScctDemoCommand::CycleTarget, -1); return FReply::Handled(); }
	if (Key == EKeys::RightBracket) { Command(EScctDemoCommand::CycleTarget, 1); return FReply::Handled(); }
	if (Key == EKeys::H) { bHidden = !bHidden; return FReply::Handled(); }
	if (Key == EKeys::Slash && bShift) { ToggleHelp(); return FReply::Handled(); }
	if (Key == EKeys::Escape)
	{
		if (bHelp)
		{
			ToggleHelp();
		}
		else
		{
			AskLeave();
		}
		return FReply::Handled();
	}
	static const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Numbers); ++Index)
	{
		if (Key == Numbers[Index])
		{
			Command(EScctDemoCommand::WatchIndex, Index);
			return FReply::Handled();
		}
	}
	// The free camera's keys are held, not pressed: they are read each frame.
	if (Key == EKeys::W || Key == EKeys::A || Key == EKeys::S || Key == EKeys::D || Key == EKeys::Q || Key == EKeys::E)
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SScctDemoHud::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	Held.Remove(InKeyEvent.GetKey());
	return FReply::Unhandled();
}

FReply SScctDemoHud::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Idle = 0.0f;
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && (Info.Flags & EScctDemoPlayFlags::CanLook) != 0 && !bConfirmingLeave)
	{
		SetLooking(true);
		// The cursor hides and the raw movement turns the camera while the button is held.
		return FReply::Handled().CaptureMouse(SharedThis(this)).UseHighPrecisionMouseMovement(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SScctDemoHud::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bLooking)
	{
		SetLooking(false);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SScctDemoHud::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Idle = 0.0f;
	if (bLooking && HasMouseCapture())
	{
		const FVector2D Delta = MouseEvent.GetCursorDelta();
		if (const FScctDemoPlayApi* Api = UScctDemoPlayback::Api())
		{
			Api->Turn(static_cast<float>(Delta.X), static_cast<float>(Delta.Y));
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SScctDemoHud::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Idle = 0.0f;
	if (bConfirmingLeave)
	{
		return FReply::Unhandled();
	}
	Command(EScctDemoCommand::CycleTarget, MouseEvent.GetWheelDelta() > 0.0f ? -1 : 1);
	return FReply::Handled();
}

void SScctDemoHud::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	// The window lost focus (or a click moved it): nothing stays held.
	if (InFocusEvent.GetCause() == EFocusCause::WindowActivate)
	{
		Held.Reset();
		SetLooking(false);
	}
}
