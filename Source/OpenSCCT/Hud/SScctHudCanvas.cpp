#include "Hud/SScctHudCanvas.h"

#include "Hud/ScctHudElements.h"
#include "Hud/ScctHudLayout.h"

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
		// Each group of the player's HUD layout (Hud/ScctHudLayout.h) is painted moved, scaled and faded as the side's
		// layout says; the pages and banners over the HUD stay where they are.
		const FScctHudModel& M = *Model;
		const int32 Side = M.LocalTeam;
		auto Group = [&Painter, Side](EScctHudGroup Id, TFunctionRef<void()> Paint)
		{
			const ScctHudLayout::FScope Scope(Painter, Id, Side);
			if (Scope.Draws())
			{
				Paint();
			}
		};
		Group(EScctHudGroup::Radar, [&] { PaintRadar(Painter, M); });
		Group(EScctHudGroup::Objectives, [&] { PaintObjectives(Painter, M); });
		Group(EScctHudGroup::Round, [&] { PaintRound(Painter, M); PaintHeadlines(Painter, M); });
		Group(EScctHudGroup::Feed, [&] { PaintFeed(Painter, M); });
		PaintTutorial(Painter, M);
		Group(EScctHudGroup::Log, [&] { PaintLog(Painter, M); });
		Group(EScctHudGroup::Status, [&] { PaintStatus(Painter, M); PaintCoopIcon(Painter, M); });
		Group(EScctHudGroup::Equipment, [&] { PaintEquipment(Painter, M); PaintBackpack(Painter, M); PaintRespawnWeapon(Painter, M); });
		Group(EScctHudGroup::Prompt, [&] { PaintPrompt(Painter, M); });
		PaintGadgetMenu(Painter, M);
		PaintSpectator(Painter, M);
		PaintBanner(Painter, M);
		PaintScoreboard(Painter, M);
		Group(EScctHudGroup::Telemetry, [&] { PaintTelemetry(Painter, M); });
	}
	return Painter.GetLayer();
}
