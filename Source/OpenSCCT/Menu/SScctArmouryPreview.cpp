#include "Menu/SScctArmouryPreview.h"

#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Menu/ScctArmouryPreviewStage.h"
#include "Rendering/DrawElements.h"
#include "Runtime/ScctFeatures.h"
#include "Social/ScctArmourySubsystem.h"

namespace
{
	/** Seconds after a drag before the view starts turning by itself again. */
	constexpr float ArmouryPreviewDragRest = 2.5f;
	/** Degrees of turn per pixel dragged. */
	constexpr float ArmouryPreviewDragTurn = 0.69f;
}

void SScctArmouryPreview::Construct(const FArguments&)
{
	SetCanTick(true);
	Stage();
}

SScctArmouryPreview::~SScctArmouryPreview()
{
	if (AScctArmouryPreviewStage* Old = StagePtr.Get())
	{
		Old->Destroy();
	}
}

AScctArmouryPreviewStage* SScctArmouryPreview::Stage()
{
	if (AScctArmouryPreviewStage* Live = StagePtr.Get())
	{
		return Live;
	}
	const UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent();
	const UGameInstance* Instance = Armoury != nullptr ? Armoury->GetGameInstance() : nullptr;
	UWorld* World = Instance != nullptr ? Instance->GetWorld() : nullptr;
	AScctArmouryPreviewStage* Made = World != nullptr && !World->bIsTearingDown ? AScctArmouryPreviewStage::Make(World) : nullptr;
	StagePtr = Made;
	Brush = FSlateBrush();
	if (Made != nullptr && Made->GetTarget() != nullptr)
	{
		Brush.SetResourceObject(Made->GetTarget());
		Brush.ImageSize = FVector2D(AScctArmouryPreviewStage::TargetWidth, AScctArmouryPreviewStage::TargetHeight);
		Brush.DrawAs = ESlateBrushDrawType::Image;
	}
	return Made;
}

void SScctArmouryPreview::Show(int32 Side, int32 Slot, const FString& PatternItem, const FString& LightItem)
{
	if (AScctArmouryPreviewStage* Live = Stage())
	{
		Live->Show(Side, Slot, PatternItem, LightItem);
	}
}

bool SScctArmouryPreview::HasModel() const
{
	const AScctArmouryPreviewStage* Live = StagePtr.Get();
	return Live != nullptr && Live->HasModel();
}

void SScctArmouryPreview::Tick(const FGeometry&, const double, const float InDeltaTime)
{
	if (AScctArmouryPreviewStage* Live = StagePtr.Get())
	{
		Rest = bDragging ? ArmouryPreviewDragRest : FMath::Max(Rest - InDeltaTime, 0.0f);
		Live->Advance(InDeltaTime, bDragging || Rest > 0.0f);
	}
}

int32 SScctArmouryPreview::OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool) const
{
	if (!HasModel() || Brush.GetResourceObject() == nullptr)
	{
		return LayerId;
	}
	// The picture whole, as large as fits, in the middle. The capture's alpha is not a coverage: it is left out.
	const FVector2D Local = AllottedGeometry.GetLocalSize();
	const float Scale = FMath::Min(Local.X / AScctArmouryPreviewStage::TargetWidth, Local.Y / AScctArmouryPreviewStage::TargetHeight);
	const FVector2D Size(AScctArmouryPreviewStage::TargetWidth * Scale, AScctArmouryPreviewStage::TargetHeight * Scale);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform((Local - Size) * 0.5)),
		&Brush, ESlateDrawEffect::IgnoreTextureAlpha, InWidgetStyle.GetColorAndOpacityTint());
	return LayerId;
}

FVector2D SScctArmouryPreview::ComputeDesiredSize(float) const
{
	return FVector2D(396.0, 330.0);
}

FReply SScctArmouryPreview::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !HasModel())
	{
		return FReply::Unhandled();
	}
	bDragging = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SScctArmouryPreview::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bDragging)
	{
		return FReply::Unhandled();
	}
	bDragging = false;
	Rest = ArmouryPreviewDragRest;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SScctArmouryPreview::OnMouseMove(const FGeometry&, const FPointerEvent& MouseEvent)
{
	AScctArmouryPreviewStage* Live = StagePtr.Get();
	if (!bDragging || Live == nullptr)
	{
		return FReply::Unhandled();
	}
	Live->Turn(MouseEvent.GetCursorDelta().X * ArmouryPreviewDragTurn);
	return FReply::Handled();
}

FCursorReply SScctArmouryPreview::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return HasModel() ? FCursorReply::Cursor(bDragging ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand) : FCursorReply::Unhandled();
}

const FSlateBrush* ScctArmouryThumbs::For(const FString& ItemId)
{
	static TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(ItemId))
	{
		return Found->Get();
	}
	const FScctArmouryPreviewApi* Api = ScctFeature<FScctArmouryPreviewApi>();
	if (Api == nullptr || ItemId.IsEmpty())
	{
		return nullptr;
	}

	FScctArmouryPreviewItem Item = {};
	TCHAR Pattern[128] = {};
	TCHAR Thumb[1024] = {};
	TSharedPtr<FSlateBrush> Made;
	if (Api->GetItem(*ItemId, &Item, Pattern, UE_ARRAY_COUNT(Pattern), Thumb, UE_ARRAY_COUNT(Thumb)) != 0 && Thumb[0] != 0)
	{
		// The picture is kept by the stage's picture store for the run.
		if (UTexture2D* Texture = AScctArmouryPreviewStage::LoadPicture(Thumb))
		{
			Made = MakeShared<FSlateBrush>();
			Made->SetResourceObject(Texture);
			Made->ImageSize = FVector2D(40.0, 40.0);
			Made->DrawAs = ESlateBrushDrawType::Image;
		}
	}
	Brushes.Add(ItemId, Made);
	return Made.Get();
}
