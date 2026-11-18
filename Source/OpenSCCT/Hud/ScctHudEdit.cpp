#include "Hud/ScctHudEdit.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Input/HittestGrid.h"
#include "Hud/ScctChatSubsystem.h"
#include "Hud/ScctHudLayout.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudPainter.h"
#include "Hud/ScctHudStyle.h"
#include "Hud/ScctHudSubsystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctGameMode.h"
#include "Play/ScctPlayerController.h"
#include "Settings/ScctSettings.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	/** Over the chat (20) and the quick chat (21), under the match menu (30). */
	constexpr int32 HudEditZOrder = 25;

	/** How near (safe-area units) an edge or the centre pulls a dragged group onto the safe area's edge or centre. */
	constexpr float HudEditSnapUnits = 10.0f;

	/** A group drawn within this long counts as on screen: it gets an outline. */
	constexpr double HudEditOnScreenSeconds = 1.5;

	FString HudEditPercent(float Value)
	{
		return FString::Printf(TEXT("%d%%"), FMath::RoundToInt32(Value * 100.0f));
	}
}

/**
 * The edit mode's layer over the whole viewport: it draws the groups' outlines and the help, and takes the mouse and
 * the keys while the layout is edited. Positions it keeps are in the safe-area canvas's units (FScctHudGroupLive).
 */
class SScctHudEditOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctHudEditOverlay) {}
		SLATE_ATTRIBUTE(int32, Side)
		SLATE_EVENT(FSimpleDelegate, OnExit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Side = InArgs._Side;
		OnExit = InArgs._OnExit;
		SetCanTick(false);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** The group's box on the canvas with its layout now (not last frame's), or invalid when it is not on screen. */
	static FBox2f PlacedNow(EScctHudGroup Group, int32 InSide)
	{
		const FScctHudGroupLive& Live = ScctHudLayout::Live(Group);
		// Within the last second and a half, or the last few frames when frames are slower than that.
		const bool bRecent = FPlatformTime::Seconds() - Live.PaintedAt <= HudEditOnScreenSeconds || GFrameCounter - Live.PaintedFrame <= 3;
		if (!Live.Raw.bIsValid || Live.Side != InSide || !bRecent)
		{
			return FBox2f(ForceInit);
		}
		FVector2f Offset;
		float Scale;
		ScctHudLayout::Placement(Group, ScctHudLayout::Get(InSide, Group), Live.CanvasSize, Live.Raw, Offset, Scale);
		return FBox2f(Offset + Live.Raw.Min * Scale, Offset + Live.Raw.Max * Scale);
	}

	// Both the paint and pointer paths use overlay-local coordinates. Slate pointer events arrive in
	// desktop space, while the geometry supplied to OnPaint starts at the game window's client area.
	struct FHitRegion
	{
		FBox2f Body = FBox2f(ForceInit);
		FBox2f Label = FBox2f(ForceInit);
	};
	mutable FHitRegion HitRegions[ScctHudLayout::NumGroups];

	bool PickAt(const FVector2f& LocalPoint, EScctHudGroup& OutGroup) const
	{
		float BestArea = TNumericLimits<float>::Max();
		bool bFound = false;
		for (int32 Index = ScctHudLayout::NumGroups - 1; Index >= 0; --Index)
		{
			const FHitRegion& Region = HitRegions[Index];
			if (Region.Label.bIsValid && Region.Label.IsInside(LocalPoint))
			{
				OutGroup = static_cast<EScctHudGroup>(Index);
				return true;
			}
			if (Region.Body.bIsValid && Region.Body.IsInside(LocalPoint) && Region.Body.GetArea() < BestArea)
			{
				BestArea = Region.Body.GetArea();
				OutGroup = static_cast<EScctHudGroup>(Index);
				bFound = true;
			}
		}
		return bFound;
	}

	static FVector2f CanvasToLocal(const FGeometry& PaintGeometry, const FScctHudGroupLive& Live, const FVector2f& Point)
	{
		return FVector2f(PaintGeometry.AbsoluteToLocal(Live.AbsoluteOrigin + Point * Live.AbsoluteScale));
	}

	bool BeginDrag(const FVector2f& LocalPoint)
	{
		if (!PickAt(LocalPoint, Selected))
		{
			return false;
		}
		bHasSelection = true;
		bDragging = true;
		DragStart = LocalPoint;
		DragFrom = ScctHudLayout::Get(Side.Get(0), Selected);
		const FScctHudGroupLive& Live = ScctHudLayout::Live(Selected);
		const FVector2f Size = Live.CanvasSize.ComponentMax(FVector2f(1.0f));
		FVector2f Offset;
		float Scale;
		ScctHudLayout::Placement(Selected, DragFrom, Size, Live.Raw, Offset, Scale);
		// If the old position was clamped after a resize, start at the box the player actually grabbed.
		const FVector2f Fraction = (Offset - ScctHudLayout::Anchor(Selected) * Size * (1.0f - Scale)) / Size;
		DragFrom.X = Fraction.X;
		DragFrom.Y = Fraction.Y;
		return true;
	}

	void MoveDrag(const FVector2f& LocalPoint)
	{
		if (!bDragging)
		{
			return;
		}
		const int32 InSide = Side.Get(0);
		const FScctHudGroupLive& Live = ScctHudLayout::Live(Selected);
		const FVector2f Size = Live.CanvasSize.ComponentMax(FVector2f(1.0f, 1.0f));
		FScctHudGroupLayout Layout = DragFrom;
		const float ToCanvasScale = GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale() / Live.AbsoluteScale;
		const FVector2f Delta = (LocalPoint - DragStart) * ToCanvasScale / Size;
		Layout.X += Delta.X;
		Layout.Y += Delta.Y;

		// Where the group would be, unclamped, then pulled onto the nearest edge or centre line in reach.
		const FVector2f Anchor = ScctHudLayout::Anchor(Selected) * Size;
		const FVector2f Offset = Anchor * (1.0f - Layout.Scale) + FVector2f(Layout.X, Layout.Y) * Size;
		const FBox2f Box(Offset + Live.Raw.Min * Layout.Scale, Offset + Live.Raw.Max * Layout.Scale);
		SnapX.Reset();
		SnapY.Reset();
		if (Live.Raw.bIsValid)
		{
			auto Snap = [](float Min, float Max, float Extent, TOptional<float>& OutLine) -> float
			{
				const float Candidates[3][2] = { { 0.0f - Min, 0.0f }, { Extent - Max, Extent }, { Extent * 0.5f - (Min + Max) * 0.5f, Extent * 0.5f } };
				float Best = HudEditSnapUnits + 1.0f;
				float Move = 0.0f;
				for (const auto& Candidate : Candidates)
				{
					if (FMath::Abs(Candidate[0]) <= HudEditSnapUnits && FMath::Abs(Candidate[0]) < Best)
					{
						Best = FMath::Abs(Candidate[0]);
						Move = Candidate[0];
						OutLine = Candidate[1];
					}
				}
				return Move;
			};
			Layout.X += Snap(Box.Min.X, Box.Max.X, Size.X, SnapX) / Size.X;
			Layout.Y += Snap(Box.Min.Y, Box.Max.Y, Size.Y, SnapY) / Size.Y;
		}
		ScctHudLayout::Set(InSide, Selected, Layout);
	}

	void EndDrag()
	{
		if (!bDragging)
		{
			return;
		}
		bDragging = false;
		SnapX.Reset();
		SnapY.Reset();
		const FScctHudGroupLayout L = ScctHudLayout::Get(Side.Get(0), Selected);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: moved %s.%s to x=%.3f y=%.3f"), ScctHudLayout::SideId(Side.Get(0)), ScctHudLayout::GroupId(Selected), L.X, L.Y);
	}

	/** One wheel step over the canvas point (or on the picked group): its size, or with bOpacity its opacity. */
	void Wheel(const FVector2f& LocalPoint, float Steps, bool bOpacity)
	{
		EScctHudGroup Group = EScctHudGroup::Status;
		if (PickAt(LocalPoint, Group))
		{
			Selected = Group;
			bHasSelection = true;
		}
		if (!bHasSelection)
		{
			return;
		}
		const int32 InSide = Side.Get(0);
		FScctHudGroupLayout L = ScctHudLayout::Get(InSide, Selected);
		float& Field = bOpacity ? L.Opacity : L.Scale;
		Field = FMath::RoundToFloat((Field + 0.05f * Steps) * 20.0f) / 20.0f;
		ScctHudLayout::Set(InSide, Selected, L);
		L = ScctHudLayout::Get(InSide, Selected);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: %s.%s %s %s"), ScctHudLayout::SideId(InSide), ScctHudLayout::GroupId(Selected), bOpacity ? TEXT("opacity") : TEXT("size"),
			*HudEditPercent(bOpacity ? L.Opacity : L.Scale));
	}

	// ---- Slate ----------------------------------------------------------------------------------------------------

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && BeginDrag(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()))))
		{
			return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse).CaptureMouse(SharedThis(this));
		}
		return FReply::Handled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const FVector2f Point = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		bHovering = PickAt(Point, Hovered);
		MoveDrag(Point);
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			EndDrag();
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Handled();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		EndDrag();
		SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		Wheel(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())), MouseEvent.GetWheelDelta() > 0.0f ? 1.0f : -1.0f, MouseEvent.IsShiftDown());
		return FReply::Handled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		return FCursorReply::Cursor(bDragging || bHovering ? EMouseCursor::CardinalCross : EMouseCursor::Default);
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override
	{
		const FKey Key = KeyEvent.GetKey();
		const int32 InSide = Side.Get(0);
		if (Key == EKeys::Escape || Key == EKeys::Enter)
		{
			OnExit.ExecuteIfBound();
			return FReply::Handled();
		}
		if (Key == EKeys::R && KeyEvent.IsControlDown())
		{
			ScctHudLayout::ResetSide(InSide);
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: every %s group back to the stock layout"), ScctHudLayout::SideId(InSide));
			return FReply::Handled();
		}
		if (!bHasSelection)
		{
			return FReply::Handled();
		}
		FScctHudGroupLayout L = ScctHudLayout::Get(InSide, Selected);
		const FVector2f Size = ScctHudLayout::Live(Selected).CanvasSize.ComponentMax(FVector2f(1.0f, 1.0f));
		const float Units = KeyEvent.IsShiftDown() ? 10.0f : 1.0f;
		if (Key == EKeys::R)
		{
			ScctHudLayout::ResetGroup(InSide, Selected);
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: %s.%s back to the stock layout"), ScctHudLayout::SideId(InSide), ScctHudLayout::GroupId(Selected));
		}
		else if (Key == EKeys::H)
		{
			L.bVisible = !L.bVisible;
			ScctHudLayout::Set(InSide, Selected, L);
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: %s.%s %s"), ScctHudLayout::SideId(InSide), ScctHudLayout::GroupId(Selected), L.bVisible ? TEXT("shown") : TEXT("hidden"));
		}
		else if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down)
		{
			L.X += (Key == EKeys::Left ? -Units : Key == EKeys::Right ? Units : 0.0f) / Size.X;
			L.Y += (Key == EKeys::Up ? -Units : Key == EKeys::Down ? Units : 0.0f) / Size.Y;
			ScctHudLayout::Set(InSide, Selected, L);
		}
		return FReply::Handled();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		using namespace ScctHudStyle;
		FScctHudPainter Painter(OutDrawElements, AllottedGeometry, LayerId);
		const int32 InSide = Side.Get(0);
		const FLinearColor Colour = Accent(InSide);
		const FSlateFontInfo LabelFont = Label(8.0f);

		TArray<FString> Offscreen;
		for (int32 Index = 0; Index < ScctHudLayout::NumGroups; ++Index)
		{
			const EScctHudGroup Group = static_cast<EScctHudGroup>(Index);
			FHitRegion& Region = HitRegions[Index];
			Region = FHitRegion{};
			const FBox2f Placed = PlacedNow(Group, InSide);
			if (!Placed.bIsValid)
			{
				Offscreen.Add(ScctHudLayout::GroupLabel(Group).ToString());
				continue;
			}
			const FScctHudGroupLayout L = ScctHudLayout::Get(InSide, Group);
			const FScctHudGroupLive& Live = ScctHudLayout::Live(Group);
			const FBox2f Box(CanvasToLocal(AllottedGeometry, Live, Placed.Min - FVector2f(3.0f)), CanvasToLocal(AllottedGeometry, Live, Placed.Max + FVector2f(3.0f)));
			Region.Body = Box;
			const bool bPicked = bHasSelection && Selected == Group;
			const bool bHot = bPicked || (bHovering && Hovered == Group);
			Painter.Fill(Box, Fade(Colour, bHot ? 0.12f : 0.05f), 2.0f, Fade(L.bVisible ? Colour : TextFaint(), bHot ? 0.95f : 0.55f), bPicked ? 2.0f : 1.0f);
			FString Caption = ScctHudLayout::GroupLabel(Group).ToString().ToUpper();
			Caption += FString::Printf(TEXT("  %s"), *HudEditPercent(L.Scale));
			if (L.Opacity < 0.999f)
			{
				Caption += FString::Printf(TEXT("  OPACITY %s"), *HudEditPercent(L.Opacity));
			}
			if (!L.bVisible)
			{
				Caption += TEXT("  HIDDEN");
			}
			const FVector2f TextSize = Painter.Measure(Caption, LabelFont);
			const float TagY = Box.Min.Y - TextSize.Y - 4.0f >= 0.0f ? Box.Min.Y - TextSize.Y - 4.0f : Box.Max.Y + 2.0f;
			Region.Label = FBox2f(FVector2f(Box.Min.X, TagY), FVector2f(Box.Min.X + TextSize.X + 10.0f, TagY + TextSize.Y + 2.0f));
			Painter.Fill(Region.Label, FLinearColor(0.0f, 0.0f, 0.0f, 0.7f), 2.0f);
			Painter.Text(FVector2f(Box.Min.X + 5.0f, TagY + 1.0f), Caption, LabelFont, bHot ? Colour : Text(), FScctHudPainter::EAlign::Left, false);
		}

		// The snap guides while dragging.
		const FScctHudGroupLive& Dragged = ScctHudLayout::Live(Selected);
		const FVector2f CanvasSize = Dragged.CanvasSize;
		if (bDragging && CanvasSize.X > 0.0f)
		{
			if (SnapX.IsSet())
			{
				Painter.Line(CanvasToLocal(AllottedGeometry, Dragged, FVector2f(SnapX.GetValue(), 0.0f)), CanvasToLocal(AllottedGeometry, Dragged, FVector2f(SnapX.GetValue(), CanvasSize.Y)), Fade(Colour, 0.8f), 1.0f);
			}
			if (SnapY.IsSet())
			{
				Painter.Line(CanvasToLocal(AllottedGeometry, Dragged, FVector2f(0.0f, SnapY.GetValue())), CanvasToLocal(AllottedGeometry, Dragged, FVector2f(CanvasSize.X, SnapY.GetValue())), Fade(Colour, 0.8f), 1.0f);
			}
		}

		// The help at the top centre, under the round panel's usual place.
		const FVector2f Size = Painter.Size();
		const FSlateFontInfo TitleFont = Label(10.0f);
		const FSlateFontInfo HelpFont = Font(EWeight::Regular, 9.0f);
		TArray<FString> Lines;
		Lines.Add(TEXT("Drag to move (edges and centre snap)   Wheel: size   Shift+wheel: opacity   Arrows: nudge"));
		Lines.Add(TEXT("H: hide or show   R: reset group   Ctrl+R: reset every group   Esc or Enter: save and leave"));
		if (Offscreen.Num() > 0)
		{
			Lines.Add(TEXT("Not on screen now: ") + FString::Join(Offscreen, TEXT(", ")));
		}
		float Width = Painter.Measure(TEXT("EDIT HUD LAYOUT"), TitleFont).X;
		for (const FString& Line : Lines)
		{
			Width = FMath::Max(Width, Painter.Measure(Line, HelpFont).X);
		}
		const float LineHeight = Painter.Measure(TEXT("Ag"), HelpFont).Y + 3.0f;
		const float PanelHeight = 30.0f + Lines.Num() * LineHeight + 8.0f;
		const FBox2f Panel(FVector2f((Size.X - Width) * 0.5f - 16.0f, Size.Y * 0.18f), FVector2f((Size.X + Width) * 0.5f + 16.0f, Size.Y * 0.18f + PanelHeight));
		Painter.Glass(Panel, 4.0f, 0.78f);
		Painter.Fill(FBox2f(Panel.Min, FVector2f(Panel.Max.X, Panel.Min.Y + 2.0f)), Colour);
		Painter.Text(FVector2f(Size.X * 0.5f, Panel.Min.Y + 9.0f), FString::Printf(TEXT("EDIT HUD LAYOUT: %s"), InSide == 1 ? TEXT("MERCENARY") : TEXT("SPY")), TitleFont, Colour, FScctHudPainter::EAlign::Centre);
		float Y = Panel.Min.Y + 30.0f;
		for (const FString& Line : Lines)
		{
			Painter.Text(FVector2f(Size.X * 0.5f, Y), Line, HelpFont, TextDim(), FScctHudPainter::EAlign::Centre);
			Y += LineHeight;
		}
		return Painter.GetLayer();
	}

private:
	TAttribute<int32> Side;
	FSimpleDelegate OnExit;

	EScctHudGroup Selected = EScctHudGroup::Status;
	bool bHasSelection = false;
	EScctHudGroup Hovered = EScctHudGroup::Status;
	bool bHovering = false;

	bool bDragging = false;
	FVector2f DragStart = FVector2f::ZeroVector;
	FScctHudGroupLayout DragFrom;
	TOptional<float> SnapX;
	TOptional<float> SnapY;
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs HudEditConsoleCommand(
		TEXT("scct.HudEdit"),
		TEXT("scct.HudEdit [on|off]: opens (or closes, saving) the HUD layout's edit mode in a match; no argument toggles it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UScctHudEditSubsystem* Edit = World != nullptr ? World->GetSubsystem<UScctHudEditSubsystem>() : nullptr;
			if (Edit == nullptr)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("scct.HudEdit: only in a match"));
				return;
			}
			const bool bOn = Args.Num() > 0 ? (Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase) || Args[0] == TEXT("1")) : !Edit->IsEditing();
			if (bOn && !Edit->IsEditing() && !Edit->Start())
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("scct.HudEdit: the edit mode cannot open now (no match HUD, or a menu or the chat has the keys)"));
			}
			else if (!bOn && Edit->IsEditing())
			{
				Edit->Stop();
			}
		}));
}

bool UScctHudEditSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScctHudEditSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctHudEditSubsystem, STATGROUP_Tickables);
}

void UScctHudEditSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UScctHudSubsystem>();
	Collection.InitializeDependency<UScctChatSubsystem>();

	bDebugOpen = FParse::Param(FCommandLine::Get(), TEXT("ScctHudEdit"));
	FParse::Value(FCommandLine::Get(), TEXT("ScctHudEditAt="), DebugAt);
	FParse::Value(FCommandLine::Get(), TEXT("ScctHudEditExitAt="), DebugExitAt);
	FString Drag;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctHudEditDrag="), Drag, /*bShouldStopOnSeparator*/ false))
	{
		FString Numbers;
		TArray<FString> Parts;
		if (Drag.Split(TEXT(":"), &DebugDragGroup, &Numbers) && Numbers.ParseIntoArray(Parts, TEXT(",")) >= 2)
		{
			DebugDrag = FVector2f(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
			DebugWheel = Parts.Num() > 2 ? FCString::Atoi(*Parts[2]) : 0;
		}
		else
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctHudEditDrag: '%s' is not <group>:DX,DY[,W]"), *Drag);
			DebugDragGroup.Reset();
		}
	}
}

void UScctHudEditSubsystem::Deinitialize()
{
	if (bEditing)
	{
		Stop();
	}
	RemoveOverlay();
	Super::Deinitialize();
}

int32 UScctHudEditSubsystem::GetSide() const
{
	const UScctHudSubsystem* Hud = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UScctHudSubsystem>() : nullptr;
	return Hud != nullptr && Hud->GetModel().LocalTeam == 1 ? 1 : 0;
}

namespace
{
	// RequestStart: how long a match's world keeps trying, when it began to, and when the request lapses.
	float HudEditRequestSeconds = 0.0f;
	double HudEditRequestTryUntil = 0.0;
	double HudEditRequestLapses = 0.0;

	/** A fresh match's HUD needs a moment before its groups have their places. */
	constexpr double HudEditRequestWorldAge = 1.5;
}

void UScctHudEditSubsystem::RequestStart(float Seconds)
{
	HudEditRequestSeconds = FMath::Max(Seconds, 0.1f);
	HudEditRequestTryUntil = 0.0;
	HudEditRequestLapses = FPlatformTime::Seconds() + 180.0;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: asked to open as soon as it can (tried for %.0f s in a match)"), HudEditRequestSeconds);
}

bool UScctHudEditSubsystem::Start()
{
	if (bEditing)
	{
		return true;
	}
	UWorld* World = GetWorld();
	AScctPlayerController* Player = World != nullptr ? Cast<AScctPlayerController>(World->GetFirstPlayerController()) : nullptr;
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	const UScctChatSubsystem* Chat = World != nullptr ? World->GetSubsystem<UScctChatSubsystem>() : nullptr;
	if (World == nullptr || Cast<AScctGameMode>(World->GetAuthGameMode()) == nullptr || Player == nullptr || !Player->IsLocalController() || Viewport == nullptr
		|| !FSlateApplication::IsInitialized() || Player->IsMenuOpen() || (Chat != nullptr && Chat->IsOpen()))
	{
		return false;
	}

	TWeakObjectPtr<UScctHudEditSubsystem> WeakThis(this);
	SAssignNew(Overlay, SScctHudEditOverlay)
		.Side_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->GetSide() : 0; })
		.OnExit_Lambda([WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Stop(); } });
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), HudEditZOrder);

	// The keys and the mouse leave the simulation (everything held is let go) and the overlay takes them.
	bEditing = true;
	ScctHudLayout::SetEditing(true);
	InputHolder = Player;
	Player->SetMenuOpen(true);
	if (!FApp::IsUnattended())
	{
		Player->SetShowMouseCursor(true);
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Overlay);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Player->SetInputMode(Mode);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: the edit mode opened for the %s layout (%s)"), GetSide() == 1 ? TEXT("merc") : TEXT("spy"), *ScctHudLayout::Describe());
	return true;
}

void UScctHudEditSubsystem::Stop()
{
	if (!bEditing)
	{
		return;
	}
	bEditing = false;
	ScctHudLayout::SetEditing(false);
	FScctSettings::Save();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: the edit mode closed and saved the layout: %s"), *ScctHudLayout::Describe());
	RemoveOverlay();

	AScctPlayerController* Player = InputHolder.Get();
	InputHolder.Reset();
	if (Player == nullptr)
	{
		return;
	}
	Player->SetMenuOpen(false);
	if (!FApp::IsUnattended())
	{
		// As the game mode gives the match its input: game only, no cursor.
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetShowMouseCursor(false);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UScctHudEditSubsystem::RemoveOverlay()
{
	if (Overlay.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
			}
		}
	}
	Overlay.Reset();
}

void UScctHudEditSubsystem::Tick(float DeltaTime)
{
	Clock += DeltaTime;
	if (bEditing)
	{
		// A menu taking the keys (the match ending, the game mode's own menu) closes the edit mode, saving it.
		const AScctPlayerController* Player = InputHolder.Get();
		if (Player == nullptr || !Player->IsMenuOpen())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHudEdit: something else took the keys"));
			Stop();
		}
		else if (Overlay.IsValid() && !FApp::IsUnattended() && FSlateApplication::IsInitialized() && !Overlay->HasKeyboardFocus() && !Overlay->HasMouseCapture())
		{
			// The pause menu this was opened from hands the keys and the mouse back to the game as it closes, after the
			// edit mode took them: the overlay then got no click and no Escape. Take them again whenever they are lost.
			if (AScctPlayerController* Holder = InputHolder.Get())
			{
				Holder->SetShowMouseCursor(true);
				FInputModeUIOnly Mode;
				Mode.SetWidgetToFocus(Overlay);
				Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
				Holder->SetInputMode(Mode);
				FSlateApplication::Get().SetKeyboardFocus(Overlay, EFocusCause::SetDirectly);
			}
		}
	}
	else if (HudEditRequestSeconds > 0.0f)
	{
		const double Now = FPlatformTime::Seconds();
		const UWorld* World = GetWorld();
		if (Now > HudEditRequestLapses)
		{
			HudEditRequestSeconds = 0.0f;
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHudEdit: the request to open lapsed before a match was up"));
		}
		else if (World != nullptr && Cast<AScctGameMode>(World->GetAuthGameMode()) != nullptr && Clock >= HudEditRequestWorldAge)
		{
			if (HudEditRequestTryUntil <= 0.0)
			{
				HudEditRequestTryUntil = Now + HudEditRequestSeconds;
			}
			if (Start())
			{
				HudEditRequestSeconds = 0.0f;
			}
			else if (Now > HudEditRequestTryUntil)
			{
				HudEditRequestSeconds = 0.0f;
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHudEdit: the edit mode could not open (a menu or the chat kept the keys)"));
			}
		}
	}
	RunUnattended();
}

void UScctHudEditSubsystem::RunUnattended()
{
	if (!bDebugOpen)
	{
		return;
	}
	if (DebugStep == 0 && Clock >= DebugAt)
	{
		// The HUD may not be up yet: tried again every frame until it opens.
		if (Start())
		{
			DebugStep = 1;
		}
	}
	else if (DebugStep == 1 && Clock >= DebugAt + 1.0f)
	{
		DebugStep = 2;
		EScctHudGroup Group = EScctHudGroup::Status;
		if (!DebugDragGroup.IsEmpty() && Overlay.IsValid())
		{
			if (!ScctHudLayout::ParseGroup(DebugDragGroup, Group))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctHudEditDrag: no group '%s'"), *DebugDragGroup);
			}
			else
			{
				const FBox2f Box = SScctHudEditOverlay::PlacedNow(Group, GetSide());
				if (!Box.bIsValid)
				{
					UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctHudEditDrag: %s is not on screen"), ScctHudLayout::GroupId(Group));
				}
				else
				{
					// Route a desktop-coordinate drag through the window's real hit-test grid. Calling the
					// gesture helpers directly used to pass even when the match menu ate every mouse click.
					FSlateApplication& Slate = FSlateApplication::Get();
					const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Overlay.ToSharedRef());
					if (Window.IsValid())
					{
						const FScctHudGroupLive& Live = ScctHudLayout::Live(Group);
						auto ScreenPoint = [&](const FVector2f& CanvasPoint)
						{
							return FVector2D(Overlay->GetTickSpaceGeometry().LocalToAbsolute(
								SScctHudEditOverlay::CanvasToLocal(Overlay->GetPaintSpaceGeometry(), Live, CanvasPoint)));
						};
						const FVector2D Start = ScreenPoint(Box.GetCenter());
						const FVector2D End = ScreenPoint(Box.GetCenter() + DebugDrag);
						const FWidgetPath Path(Window->GetHittestGrid().GetBubblePath(Start, Slate.GetCursorRadius(), false, 0));
						if (!Path.ContainsWidget(Overlay.Get()))
						{
							UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHudEdit: drag blocked by %s"),
								Path.IsValid() ? *Path.GetLastWidget()->ToString() : TEXT("empty hit path"));
							return;
						}
						const FModifierKeysState Mods;
						const TSet<FKey> Held{EKeys::LeftMouseButton};
						const TSet<FKey> Up;
						const uint32 Pointer = Slate.CursorPointerIndex;
						Slate.RoutePointerDownEvent(Path, FPointerEvent(Pointer, Start, Start, Held, EKeys::LeftMouseButton, 0, Mods));
						const FWidgetPath EndPath(Window->GetHittestGrid().GetBubblePath(End, Slate.GetCursorRadius(), false, 0));
						Slate.RoutePointerMoveEvent(EndPath, FPointerEvent(Pointer, End, Start, Held, EKeys::Invalid, 0, Mods), false);
						Slate.RoutePointerUpEvent(EndPath, FPointerEvent(Pointer, End, End, Up, EKeys::LeftMouseButton, 0, Mods));
						for (int32 Step = 0; Step < FMath::Abs(DebugWheel); ++Step)
						{
							Slate.RouteMouseWheelOrGestureEvent(EndPath, FPointerEvent(Pointer, End, End, Up, EKeys::Invalid, FMath::Sign(DebugWheel), Mods));
						}
					}
				}
			}
		}
	}
	if (bEditing && DebugExitAt >= 0.0f && Clock >= DebugExitAt)
	{
		DebugExitAt = -1.0f;
		Stop();
	}
}
