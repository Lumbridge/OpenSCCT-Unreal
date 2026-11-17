#include "Hud/SScctHudCanvas.h"

#include "Hud/ScctHudElements.h"

void SScctHudCanvas::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	Layer = InArgs._Layer;
	SetVisibility(EVisibility::HitTestInvisible);
	// The HUD moves every frame (eased values, fades), so there is nothing to cache between paints.
	SetCanTick(false);
}

FVector2D SScctHudCanvas::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// It fills whatever its slot gives it.
	return FVector2D::ZeroVector;
}

int32 SScctHudCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!Model.IsValid())
	{
		return LayerId;
	}

	using namespace ScctHudElements;
	FScctHudPainter Painter(OutDrawElements, AllottedGeometry, LayerId);
	if (Layer == ELayer::Blinding)
	{
		PaintFlashWhiteOut(Painter, *Model);
	}
	else if (Layer == ELayer::FullScreen)
	{
		PaintScreenEffects(Painter, *Model);
		PaintMarkers(Painter, *Model);
		PaintTeammateNames(Painter, *Model);
		PaintScope(Painter, *Model);
		PaintSpyBinoculars(Painter, *Model);
		PaintCameraPages(Painter, *Model);
		PaintMotionTracking(Painter, *Model);
		PaintCrosshair(Painter, *Model);
		PaintTeamPings(Painter, *Model);
	}
	else
	{
		PaintRadar(Painter, *Model);
		PaintObjectives(Painter, *Model);
		PaintRound(Painter, *Model);
		PaintFeed(Painter, *Model);
		PaintHeadlines(Painter, *Model);
		PaintTutorial(Painter, *Model);
		PaintLog(Painter, *Model);
		PaintStatus(Painter, *Model);
		PaintEquipment(Painter, *Model);
		PaintBackpack(Painter, *Model);
		PaintPrompt(Painter, *Model);
		PaintGadgetMenu(Painter, *Model);
		PaintCoopIcon(Painter, *Model);
		PaintSpectator(Painter, *Model);
		PaintRespawnWeapon(Painter, *Model);
		PaintBanner(Painter, *Model);
		PaintScoreboard(Painter, *Model);
		PaintTelemetry(Painter, *Model);
	}
	return Painter.GetLayer();
}
