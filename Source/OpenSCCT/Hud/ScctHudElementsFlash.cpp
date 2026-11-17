#include "Hud/ScctHudElements.h"

#include "Hud/ScctScreenEffectsSubsystem.h"

// The flash-bang white-out: above the whole HUD, as the merc page's Flash area covers the page and its feed.

void ScctHudElements::PaintFlashWhiteOut(FScctHudPainter& Painter, const FScctHudModel& Model)
{
	if (!Model.bHasState)
	{
		return;
	}
	const float White = UScctScreenEffectsSubsystem::FlashWhiteOut(Model.State);
	if (White > 0.0f)
	{
		Painter.Fill(FBox2f(FVector2f::ZeroVector, Painter.Size()), FLinearColor(1.0f, 1.0f, 1.0f, White));
	}
}
