#include "Menu/SScctStrategicMapPage.h"
#include "Menu/ScctMenuNav.h"
#include "Menu/SScctStrategicMap3D.h"
#include "Menu/ScctMenuKit.h"
#include "Engine/World.h"
#include "Play/ScctPlayerController.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Menu/ScctMenuStyle.h"
#include "OpenSCCT.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	const FName StrategicMapPageType(TEXT("SScctStrategicMapPage"));

	/** GUI.StrategicMap: the half-second timer of the blink and the positions. */
	constexpr float StrategicMapTimerSeconds = 0.5f;

	/** The picture's side on screen, and the original's units: the picture 256, the "you are here" marker 20, a mate 32. */
	constexpr float StrategicMapSide = 620.0f;
	constexpr float StrategicMapPictureUnits = 256.0f;
	constexpr float StrategicMapYouUnits = 20.0f;
	constexpr float StrategicMapMateUnits = 32.0f;
	constexpr float StrategicMapLegendWidth = 300.0f;
	constexpr float StrategicMapFloorsWidth = 128.0f;

	/** GUI.StrategicMap defaults ShowSpotColor, HideSpotColor, ShowTeamMateColor, HideTeamMateColor. */
	FLinearColor StrategicMapSpot(bool bBright)
	{
		return bBright ? ScctHudStyle::Srgb(255, 255, 255) : ScctHudStyle::Srgb(49, 63, 49);
	}

	FLinearColor StrategicMapMate(bool bBright)
	{
		return bBright ? ScctHudStyle::Srgb(254, 183, 75) : ScctHudStyle::Srgb(99, 60, 1);
	}

	FString StrategicMapError(const FScctMapApi* Api)
	{
		TCHAR Buffer[1024];
		Buffer[0] = 0;
		Api->LastError(Buffer, UE_ARRAY_COUNT(Buffer));
		FString Error(Buffer);
		int32 LineEnd = INDEX_NONE;
		if (Error.FindChar(TEXT('\n'), LineEnd))
		{
			Error.LeftInline(LineEnd);
		}
		return Error.TrimStartAndEnd();
	}

	/**
	 * A floor picture as a brush, decoded from its PNG the first time it is shown and kept for the run (never released:
	 * Slate may be gone by the time statics are destroyed). Null when it cannot be read.
	 */
	const FSlateBrush* StrategicMapPicture(const FString& File)
	{
		static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>>& Pictures = *new TMap<FString, TSharedPtr<FSlateDynamicImageBrush>>();
		if (File.IsEmpty())
		{
			return nullptr;
		}
		if (const TSharedPtr<FSlateDynamicImageBrush>* Found = Pictures.Find(File))
		{
			return Found->Get();
		}

		TSharedPtr<FSlateDynamicImageBrush> Made;
		FImage Loaded;
		if (FImageUtils::LoadImage(*File, Loaded))
		{
			// Slate takes 8-bit BGRA; the converted pictures decode to that, to grey or to 16 bits a channel, which
			// are turned into it here (the game module does not link the image library's converter).
			const int64 Pixels = static_cast<int64>(Loaded.SizeX) * Loaded.SizeY;
			TArray<uint8> Bytes;
			if (Loaded.Format == ERawImageFormat::BGRA8)
			{
				Bytes = TArray<uint8>(Loaded.RawData.GetData(), static_cast<int32>(Loaded.RawData.Num()));
			}
			else if (Loaded.Format == ERawImageFormat::G8 || Loaded.Format == ERawImageFormat::RGBA16)
			{
				Bytes.SetNumUninitialized(static_cast<int32>(Pixels * 4));
				const uint16* Wide = reinterpret_cast<const uint16*>(Loaded.RawData.GetData());
				for (int64 Index = 0; Index < Pixels; ++Index)
				{
					uint8* Out = &Bytes[static_cast<int32>(Index * 4)];
					if (Loaded.Format == ERawImageFormat::G8)
					{
						Out[0] = Out[1] = Out[2] = Loaded.RawData[Index];
						Out[3] = 255;
					}
					else
					{
						Out[0] = static_cast<uint8>(Wide[Index * 4 + 2] >> 8);
						Out[1] = static_cast<uint8>(Wide[Index * 4 + 1] >> 8);
						Out[2] = static_cast<uint8>(Wide[Index * 4 + 0] >> 8);
						Out[3] = static_cast<uint8>(Wide[Index * 4 + 3] >> 8);
					}
				}
			}
			else
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctStrategicMap: the picture %s has an unexpected pixel format (%d)"), *File, static_cast<int32>(Loaded.Format));
			}
			if (Pixels > 0 && Bytes.Num() == Pixels * 4)
			{
				Made = FSlateDynamicImageBrush::CreateWithImageData(FName(*(TEXT("ScctStrategicMap:") + File)), FVector2D(Loaded.SizeX, Loaded.SizeY), Bytes);
			}
		}
		if (!Made.IsValid())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctStrategicMap: the picture %s could not be read"), *File);
		}
		Pictures.Add(File, Made);
		return Made.Get();
	}
}

/**
 * The picture of the shown floor with its places and markers, drawn as the Godot StrategicMapView draws its modern
 * look: the "you are here" marker a dot with a view cone opening the way the player faces, a mate a dot in a ring,
 * in the original's colours, bright or dim with the blink.
 */
class SScctStrategicMapView : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctStrategicMapView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FScctMapState* InState, const TArray<FScctMapPlace>* InPlaces)
	{
		State = InState;
		Places = InPlaces;
		SetCanTick(false);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(StrategicMapSide, StrategicMapSide);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Out,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		static const FSlateRoundedBoxBrush Frame(FLinearColor(0.02f, 0.025f, 0.03f, 0.9f), 4.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.22f), 1.0f);
		const FVector2f Size = Geometry.GetLocalSize();
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(), &Frame);

		// The picture fills the frame but for its border; markers are placed on it by U and V.
		const FVector2f InnerAt(2.0f, 2.0f);
		const FVector2f Inner = Size - FVector2f(4.0f, 4.0f);
		const FScctMapState& S = *State;
		const FSlateBrush* Picture = S.HasPicture != 0 ? StrategicMapPicture(FString(S.Picture)) : nullptr;
		if (Picture != nullptr)
		{
			FSlateDrawElement::MakeBox(Out, LayerId + 1, Geometry.ToPaintGeometry(Inner, FSlateLayoutTransform(InnerAt)), Picture);
		}
		else
		{
			const FSlateFontInfo Font = ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 14.0f);
			const FString Text = TEXT("No map picture");
			const FVector2f Extent = Measure(Text, Font);
			FSlateDrawElement::MakeText(Out, LayerId + 1, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform((Size - Extent) * 0.5f)), Text, Font,
				ESlateDrawEffect::None, ScctHudStyle::TextDim());
		}

		const float Scale = Inner.X / StrategicMapPictureUnits;
		const auto At = [&](float U, float V) { return InnerAt + FVector2f(U * Inner.X, V * Inner.Y); };

		// The map's numbered places on the shown floor.
		const FSlateFontInfo PlaceFont = ScctHudStyle::Font(ScctHudStyle::EWeight::Bold, FMath::Max(10.0f, 14.0f * Scale));
		for (const FScctMapPlace& Place : *Places)
		{
			if (Place.Floor != S.Floor || Place.U < 0.0f || Place.U > 1.0f || Place.V < 0.0f || Place.V > 1.0f)
			{
				continue;
			}
			const FString Text = FString::FromInt(Place.Number);
			const FVector2f Extent = Measure(Text, PlaceFont);
			const FVector2f Corner = At(Place.U, Place.V) - Extent * 0.5f;
			FSlateDrawElement::MakeText(Out, LayerId + 2, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Corner + FVector2f(1.5f, 1.5f))), Text,
				PlaceFont, ESlateDrawEffect::None, FLinearColor::Black);
			FSlateDrawElement::MakeText(Out, LayerId + 3, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Corner)), Text, PlaceFont,
				ESlateDrawEffect::None, StrategicMapSpot(true));
		}

		const bool bBright = S.Bright != 0;
		for (int32 Index = 0; Index < FScctMapState::MaxMates; ++Index)
		{
			if (S.MateShown[Index] != 0)
			{
				DrawMate(Geometry, Out, LayerId + 4, At(S.MateU[Index], S.MateV[Index]), StrategicMapMateUnits * Scale, StrategicMapMate(bBright));
			}
		}
		if (S.YouShown != 0)
		{
			DrawYou(Geometry, Out, LayerId + 5, At(S.YouU, S.YouV), S.YouAngle, StrategicMapYouUnits * Scale, StrategicMapSpot(bBright));
		}
		return LayerId + 6;
	}

private:
	static FVector2f Measure(const FString& Text, const FSlateFontInfo& Font)
	{
		return FSlateApplication::IsInitialized()
			? FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font))
			: FVector2f(Text.Len() * Font.Size * 0.6f, Font.Size * 1.3f);
	}

	static void Dot(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FVector2f& Centre, float Radius, const FLinearColor& Colour)
	{
		const FSlateRoundedBoxBrush Round(FLinearColor::White, Radius);
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Radius * 2.0f, Radius * 2.0f), FSlateLayoutTransform(Centre - FVector2f(Radius, Radius))),
			&Round, ESlateDrawEffect::None, Colour);
	}

	/** A dot with a view cone opening the way the player faces (0: right, turning clockwise). */
	static void DrawYou(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FVector2f& Centre, float Angle, float Size, const FLinearColor& Colour)
	{
		const float R = Size * 0.5f;
		const FVector2f Forward(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2f Side(-Forward.Y, Forward.X);
		const FVector2f Left = Centre + Forward * (R * 1.6f) - Side * (R * 0.9f);
		const FVector2f Right = Centre + Forward * (R * 1.6f) + Side * (R * 0.9f);

		// The cone's inside as a fan of lines, then its edge.
		FLinearColor Fill = Colour;
		Fill.A *= 0.45f;
		constexpr int32 Rays = 14;
		for (int32 Ray = 0; Ray <= Rays; ++Ray)
		{
			const FVector2f End = FMath::Lerp(Left, Right, static_cast<float>(Ray) / Rays);
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), TArray<FVector2f>{ Centre, End }, ESlateDrawEffect::None, Fill, true, 2.5f);
		}
		FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), TArray<FVector2f>{ Centre, Left, Right, Centre }, ESlateDrawEffect::None, Colour, true, 1.5f);

		// A dark rim keeps the dot readable on the white of the pictures.
		Dot(Geometry, Out, Layer + 1, Centre, R * 0.45f + 1.5f, FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		Dot(Geometry, Out, Layer + 2, Centre, R * 0.45f, Colour);
	}

	static void DrawMate(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const FVector2f& Centre, float Size, const FLinearColor& Colour)
	{
		Dot(Geometry, Out, Layer, Centre, Size * 0.16f, Colour);
		TArray<FVector2f> Ring;
		constexpr int32 Segments = 24;
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float A = UE_TWO_PI * Index / Segments;
			Ring.Add(Centre + FVector2f(FMath::Cos(A), FMath::Sin(A)) * (Size * 0.26f));
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Ring, ESlateDrawEffect::None, Colour, true, 1.5f);
	}

	const FScctMapState* State = nullptr;
	const TArray<FScctMapPlace>* Places = nullptr;
};

TSharedPtr<SScctStrategicMapPage> SScctStrategicMapPage::Open(const TSharedRef<SOverlay>& Host, int32 Context, const FString& Map, bool bOwnKeys,
	FSimpleDelegate OnClosed, TWeakObjectPtr<UWorld> MatchWorld)
{
	if (const TSharedPtr<SScctStrategicMapPage> Already = FindOpen(Host))
	{
		return Already;
	}
	const FScctMapApi* Api = ScctFeature<FScctMapApi>();
	if (Api == nullptr || Api->Open(Context, *Map) == 0)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctStrategicMap: the strategic map cannot open: %s"),
			Api == nullptr ? TEXT("the bridge has no map table") : *StrategicMapError(Api));
		return nullptr;
	}

	TSharedRef<SScctStrategicMapPage> Page = SNew(SScctStrategicMapPage)
		.bOwnKeys(bOwnKeys)
		.MatchWorld(MatchWorld)
		.OnClosed(OnClosed);
	Page->Host = Host;
	if (FSlateApplication::IsInitialized())
	{
		Page->FocusBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}
	Host->AddSlot()[ Page ];
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: opened %s (%s): %d floor(s), picture %s, %d place(s); %s"), Page->State.Map,
		Context == EScctMapContext::Match ? TEXT("in a round") : TEXT("lobby overview"), Page->State.FloorCount, Page->State.Picture,
		Page->State.PlaceCount, *Page->Describe());
	return Page;
}

TSharedPtr<SScctStrategicMapPage> SScctStrategicMapPage::FindOpen(const TSharedRef<SOverlay>& Host)
{
	FChildren* Children = Host->GetChildren();
	for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
	{
		const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
		if (Child->GetType() == StrategicMapPageType)
		{
			return StaticCastSharedRef<SScctStrategicMapPage>(Child);
		}
	}
	return nullptr;
}

void SScctStrategicMapPage::Construct(const FArguments& InArgs)
{
	OnClosed = InArgs._OnClosed;
	bOwnKeys = InArgs._bOwnKeys;
	if (const FScctMapApi* Api = ScctFeature<FScctMapApi>())
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(Api->GetLegend(nullptr, 0) + 1);
		Api->GetLegend(Buffer.GetData(), Buffer.Num());
		Legend = FString(Buffer.GetData());
		Places.SetNumZeroed(Api->ListPlaces(nullptr, 0));
		Api->ListPlaces(Places.GetData(), Places.Num());
	}
	SAssignNew(Iso, SScctStrategicMap3D, &State, &Places, InArgs._MatchWorld);
	if (InArgs._MatchWorld.IsValid())
	{
		if (const AScctPlayerController* PC = Cast<AScctPlayerController>(InArgs._MatchWorld->GetFirstPlayerController()))
		{
			MapKeys = PC->FindKeysBoundTo(TEXT("DisplayStrategicMap"));
		}
	}
	Refresh();

	const auto FloorButton = [this](const TCHAR* Caption, bool bUp)
	{
		return Ui::Button(Ui::ToText(Caption), FOnClicked::CreateLambda([this, bUp] { bUp ? FloorUp() : FloorDown(); return FReply::Handled(); }),
			false, Ui::Accent(), StrategicMapFloorsWidth);
	};

	static const FSlateColorBrush Dim(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		// What is behind is dimmed and, being covered, takes no clicks while the page is open.
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(&Dim)
			.ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
		]
		+ SOverlay::Slot().Padding(20.0f)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				Ui::Glass(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 24.0f, 0.0f)
						[
							Ui::Title(Ui::ToText(TEXT("STRATEGIC MAP")), 26.0f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(ScctHudStyle::Label(10.0f))
							.ColorAndOpacity(ScctHudStyle::TextDim())
							.Text(Ui::ToText(FString(State.Map).ToUpper()))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 16.0f)[ Ui::Rule() ]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 20, 0)
						[
							SNew(SWidgetSwitcher).WidgetIndex_Lambda([this] { return Shows3D() ? 0 : 1; })
							+ SWidgetSwitcher::Slot()[ Iso.ToSharedRef() ]
							+ SWidgetSwitcher::Slot()[ SNew(SScctStrategicMapView, &State, &Places) ]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
						[
							SNew(SBox).WidthOverride(StrategicMapLegendWidth).MaxDesiredHeight(StrategicMapSide)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
								[
									Ui::Text(TAttribute<FText>::CreateLambda([this]
									{
										return Ui::ToText(State.FloorCount > 0 ? FString::Printf(TEXT("Floor %d / %d"), State.Floor + 1, State.FloorCount) : FString(State.Map));
									}), 18)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
								[
									SNew(SHorizontalBox).Visibility(State.FloorCount > 1 ? EVisibility::Visible : EVisibility::Collapsed)
									+ SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[ FloorButton(TEXT("DOWN"), false) ]
									+ SHorizontalBox::Slot().AutoWidth()[ FloorButton(TEXT("UP"), true) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
								[
									SNew(SBox).Visibility_Lambda([this] { return Iso->HasGeometry() && State.HasPicture ? EVisibility::Visible : EVisibility::Collapsed; })
									[
										Ui::Button(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(Shows3D() ? TEXT("FLOOR PLAN") : TEXT("3D MAP")); }),
											FOnClicked::CreateSP(this, &SScctStrategicMapPage::ToggleView))
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
								[
									SNew(SBox).Visibility_Lambda([this] { return Shows3D() ? EVisibility::Visible : EVisibility::Collapsed; })
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
										[
											Ui::Text(TAttribute<FText>::CreateLambda([]
											{
												return Ui::ToText(ScctUi::IsPadActive()
													? TEXT("D-pad to pan, bumpers to orbit\nTriggers to zoom, Y / X for floors\nPress left stick for floor plan, right to reset")
													: TEXT("Drag to orbit, Shift-drag to pan\nScroll to zoom, PgUp / PgDn for floors"));
											}), 12, FSlateColor(ScctHudStyle::TextDim()))
										]
										+ SVerticalBox::Slot().AutoHeight()
										[
											Ui::Button(Ui::ToText(TEXT("RESET VIEW")), FOnClicked::CreateLambda([this]{ Iso->ResetCamera(); return FReply::Handled(); }))
										]
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[ Ui::Rule() ]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ Ui::Label(TEXT("LEGEND")) ]
								+ SVerticalBox::Slot().FillHeight(1)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										Ui::Text(Ui::ToText(Legend.IsEmpty() ? FString(TEXT("This map has no legend.")) : Legend), 12.5f,
											FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
									]
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 12.0f)[ Ui::Rule() ]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Ui::Button(Ui::ToText(TEXT("BACK")), FOnClicked::CreateLambda([this] { Close(); return FReply::Handled(); }), false, Ui::Accent(), 150.0f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
						[
							Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(Shows3D() ? TEXT("White arrow  �  You       Orange ring  �  Teammate       Diamond  �  Objective")
								: (State.Live ? TEXT("White  �  You       Orange  �  Teammate") : TEXT("Lobby overview"))); }),
								11.5f, FSlateColor(ScctHudStyle::TextDim()))
						]
					], FMargin(30.0f, 26.0f), 0.82f)
			]
		]
	];
}

void SScctStrategicMapPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// The keyboard can only be given to a widget that is on screen, which the page is by its first tick.
	if (bOwnKeys && !bFocusTaken && FSlateApplication::IsInitialized())
	{
		bFocusTaken = true;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}

	// GUI.StrategicMap.Timer: the markers blink and are placed again from fresh positions.
	Timer += InDeltaTime;
	if (Timer < StrategicMapTimerSeconds)
	{
		return;
	}
	Timer = FMath::Fmod(Timer, StrategicMapTimerSeconds);
	if (const FScctMapApi* Api = ScctFeature<FScctMapApi>(); Api != nullptr && !bClosed)
	{
		Api->Tick();
	}
	Refresh();
}

bool SScctStrategicMapPage::Shows3D() const
{
	return Iso.IsValid() && Iso->HasGeometry() && (!bFloorPlan || !State.HasPicture);
}

FReply SScctStrategicMapPage::ToggleView()
{
	if (State.HasPicture && Iso->HasGeometry()) bFloorPlan = !bFloorPlan;
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

FReply SScctStrategicMapPage::OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	ScctUi::NoteInput(Key);
	if (MapKeys.Contains(Key))
	{
		if (!Event.IsRepeat()) Close();
		return FReply::Handled();
	}
	if (Key == EKeys::PageUp || Key == EKeys::Gamepad_FaceButton_Top) { FloorUp(); return FReply::Handled(); }
	if (Key == EKeys::PageDown || Key == EKeys::Gamepad_FaceButton_Left) { FloorDown(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_LeftThumbstick && State.Live) return ToggleView();
	if (Shows3D() && Iso->Navigate(Key)) return FReply::Handled();
	return FReply::Unhandled();
}

FReply SScctStrategicMapPage::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bOwnKeys && ScctMenuNav::IsBackKey(InKeyEvent.GetKey()))
	{
		Close();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SScctStrategicMapPage::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bOwnKeys && MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Close();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

bool SScctStrategicMapPage::FloorUp()
{
	const FScctMapApi* Api = ScctFeature<FScctMapApi>();
	const bool bMoved = Api != nullptr && Api->Up() != 0;
	Refresh();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: UP %s; %s"), bMoved ? TEXT("shows the floor above") : TEXT("(no floor above)"), *Describe());
	return bMoved;
}

bool SScctStrategicMapPage::FloorDown()
{
	const FScctMapApi* Api = ScctFeature<FScctMapApi>();
	const bool bMoved = Api != nullptr && Api->Down() != 0;
	Refresh();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: DOWN %s; %s"), bMoved ? TEXT("shows the floor below") : TEXT("(no floor below)"), *Describe());
	return bMoved;
}

void SScctStrategicMapPage::Close()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	if (const FScctMapApi* Api = ScctFeature<FScctMapApi>())
	{
		Api->Close();
	}
	const TSharedRef<SScctStrategicMapPage> Self = SharedThis(this);
	if (const TSharedPtr<SOverlay> Overlay = Host.Pin())
	{
		Overlay->RemoveSlot(Self);
	}
	if (bOwnKeys && FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<SWidget> Before = FocusBefore.Pin())
		{
			FSlateApplication::Get().SetKeyboardFocus(Before, EFocusCause::SetDirectly);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctStrategicMap: closed"));
	OnClosed.ExecuteIfBound();
}

FString SScctStrategicMapPage::Describe() const
{
	FString Line = FString::Printf(TEXT("floor %d of %d"), State.Floor + 1, State.FloorCount);
	if (State.Live == 0)
	{
		return Line + TEXT(", no markers (lobby)");
	}
	Line += State.YouShown != 0
		? FString::Printf(TEXT(", you-are-here marker at U %.3f V %.3f facing %.0f deg"), State.YouU, State.YouV, FMath::RadiansToDegrees(State.YouAngle))
		: FString(TEXT(", no you-are-here marker on this floor"));
	if (State.HasYou != 0)
	{
		Line += FString::Printf(TEXT(", player at sim (%.0f, %.0f, %.0f) yaw %d on floor %d"), State.YouX, State.YouY, State.YouZ, State.YouYaw, State.YouFloor + 1);
	}
	Line += FString::Printf(TEXT(", %d team mate(s), %d on this floor"), State.MateCount, State.MatesShown);
	for (int32 Index = 0; Index < FScctMapState::MaxMates; ++Index)
	{
		if (State.MateShown[Index] != 0)
		{
			Line += FString::Printf(TEXT(" [U %.3f V %.3f]"), State.MateU[Index], State.MateV[Index]);
		}
	}
	return Line;
}

void SScctStrategicMapPage::Refresh()
{
	if (const FScctMapApi* Api = ScctFeature<FScctMapApi>())
	{
		Api->GetState(&State);
		Iso->ReadFloor(*Api);
		Iso->Invalidate(EInvalidateWidgetReason::Paint);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}
