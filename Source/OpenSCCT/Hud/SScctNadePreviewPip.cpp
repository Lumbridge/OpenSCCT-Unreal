#include "Hud/SScctNadePreviewPip.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** The picture's share of the viewport's width, and the least and most it is wide (Slate units). */
	constexpr float NadePipWidthShare = 0.22f;
	constexpr float NadePipMinWidth = 240.0f;
	constexpr float NadePipMaxWidth = 460.0f;
	constexpr float NadePipAspect = 4.0f / 3.0f;
	constexpr float NadePipEdge = 28.0f;
	constexpr float NadePipBorder = 2.0f;
	constexpr float NadePipCaptionHeight = 22.0f;

	void NadePipBox(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FVector2f& TopLeft, const FVector2f& Size, const FLinearColor& Colour)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Colour);
	}

	FVector2f NadePipMeasure(const FString& Text, const FSlateFontInfo& Font)
	{
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
	}
}

void SScctNadePreviewPip::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	Picture.DrawAs = ESlateBrushDrawType::Image;
	Picture.Tiling = ESlateBrushTileType::NoTile;
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
}

void SScctNadePreviewPip::SetTarget(UTextureRenderTarget2D* Target)
{
	Picture.SetResourceObject(Target);
	bHasTarget = Target != nullptr;
	if (Target != nullptr)
	{
		Picture.ImageSize = FVector2D(Target->SizeX, Target->SizeY);
	}
}

FVector2D SScctNadePreviewPip::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// It fills the viewport it is put in.
	return FVector2D::ZeroVector;
}

int32 SScctNadePreviewPip::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!Model.IsValid())
	{
		return LayerId;
	}

	const FVector2f Area(AllottedGeometry.GetLocalSize());
	if (Model->bPicture && bHasTarget)
	{
		const float Width = FMath::Clamp(Area.X * NadePipWidthShare, NadePipMinWidth, NadePipMaxWidth);
		const FVector2f Size(Width, Width / NadePipAspect);
		const FVector2f TopLeft(Area.X - NadePipEdge - Size.X, (Area.Y - Size.Y - NadePipCaptionHeight) * 0.5f);
		const FVector2f Border(NadePipBorder, NadePipBorder);

		// The frame, the picture in it, and the caption on a dark strip under it.
		NadePipBox(OutDrawElements, ++LayerId, AllottedGeometry, TopLeft - Border, Size + Border * 2.0f + FVector2f(0.0f, NadePipCaptionHeight), FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		NadePipBox(OutDrawElements, ++LayerId, AllottedGeometry, TopLeft - Border, FVector2f(Size.X + Border.X * 2.0f, Border.Y), Model->Frame);
		NadePipBox(OutDrawElements, ++LayerId, AllottedGeometry, TopLeft + FVector2f(-Border.X, Size.Y), FVector2f(Size.X + Border.X * 2.0f, Border.Y), Model->Frame);
		NadePipBox(OutDrawElements, LayerId, AllottedGeometry, TopLeft - Border, FVector2f(Border.X, Size.Y + Border.Y * 2.0f), Model->Frame);
		NadePipBox(OutDrawElements, LayerId, AllottedGeometry, TopLeft + FVector2f(Size.X, -Border.Y), FVector2f(Border.X, Size.Y + Border.Y * 2.0f), Model->Frame);
		FSlateDrawElement::MakeBox(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			&Picture, ESlateDrawEffect::IgnoreTextureAlpha, FLinearColor::White);

		if (!Model->Caption.IsEmpty())
		{
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 10);
			const FVector2f TextSize = NadePipMeasure(Model->Caption, Font);
			const FVector2f At(TopLeft.X + 6.0f, TopLeft.Y + Size.Y + NadePipBorder + (NadePipCaptionHeight - NadePipBorder - TextSize.Y) * 0.5f);
			FSlateDrawElement::MakeText(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(At)),
				Model->Caption, Font, ESlateDrawEffect::None, FLinearColor::White);
		}
	}

	if (!Model->Notice.IsEmpty() && FPlatformTime::Seconds() < Model->NoticeUntil)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 14);
		const FVector2f TextSize = NadePipMeasure(Model->Notice, Font);
		const FVector2f At((Area.X - TextSize.X) * 0.5f, Area.Y * 0.3f);
		NadePipBox(OutDrawElements, ++LayerId, AllottedGeometry, At - FVector2f(10.0f, 6.0f), TextSize + FVector2f(20.0f, 12.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
		FSlateDrawElement::MakeText(OutDrawElements, ++LayerId, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(At)),
			Model->Notice, Font, ESlateDrawEffect::None, FLinearColor::White);
	}
	return LayerId;
}
