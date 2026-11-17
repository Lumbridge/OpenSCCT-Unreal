#include "Social/SScctSocialToast.h"

#include "Hud/ScctHudStyle.h"
#include "Menu/ScctMenuStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The last second fades the notice out. */
	constexpr float FadeSeconds = 1.0f;
}

void SScctSocialToast::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::Collapsed);
	ChildSlot
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Top)
	.Padding(24.0f, 16.0f)
	[
		ScctMenuStyle::Glass(
			SNew(STextBlock)
			.Text_Lambda([this] { return Message; })
			.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 15.0f))
			.ColorAndOpacity(ScctMenuStyle::Accent())
			.Justification(ETextJustify::Center)
			.AutoWrapText(true),
			FMargin(20.0f, 12.0f), 0.82f)
	];
}

void SScctSocialToast::Show(const FString& Text, float Seconds)
{
	Message = FText::FromString(Text);
	Remaining = FMath::Max(Seconds, FadeSeconds);
	SetRenderOpacity(1.0f);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SScctSocialToast::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Remaining <= 0.0f)
	{
		return;
	}

	Remaining -= InDeltaTime;
	if (Remaining <= 0.0f)
	{
		SetVisibility(EVisibility::Collapsed);
		return;
	}
	SetRenderOpacity(FMath::Clamp(Remaining / FadeSeconds, 0.0f, 1.0f));
}
