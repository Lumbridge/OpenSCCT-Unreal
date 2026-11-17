#include "Hud/ScctHudWidget.h"

#include "Hud/SScctHudCanvas.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/SOverlay.h"

TSharedRef<SWidget> UScctHudWidget::RebuildWidget()
{
	return SNew(SOverlay)
		.Visibility(EVisibility::HitTestInvisible)
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
