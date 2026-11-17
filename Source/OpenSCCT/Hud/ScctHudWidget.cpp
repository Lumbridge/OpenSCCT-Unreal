#include "Hud/ScctHudWidget.h"

#include "Hud/SScctHudCanvas.h"
#include "Hud/ScctHudModel.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/SOverlay.h"

TSharedRef<SWidget> UScctHudWidget::RebuildWidget()
{
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
			SNew(SScctHudCanvas)
			.Model(Model)
			.Layer(SScctHudCanvas::ELayer::Blinding)
		];
}
