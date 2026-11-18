#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

/**
 * Which rank picture to draw (the Godot client's RankIcons.For): the master prestige emblem, else the prestige emblem
 * once prestiged, else the level insignia (its shape and colour by tier of five levels, one to five chevrons inside).
 */
struct FScctRankEmblem
{
	int32 Level = 0; // 0: no rank (a guest, a bot); nothing is drawn
	int32 Prestige = 0;
	bool bMaster = false;

	bool IsNone() const { return Level <= 0 && Prestige <= 0 && !bMaster; }

	/** A packed lobby rank (FScctOnlinePlayer::Rank), unpacked by the bridge's account table; none without it. */
	static FScctRankEmblem OfPacked(int32 Packed);

	/** The emblem of a prestige (1 to 10), whatever the level. */
	static FScctRankEmblem OfPrestige(int32 Prestige) { return FScctRankEmblem{ 1, Prestige, false }; }

	static FScctRankEmblem Master() { return FScctRankEmblem{ 55, 10, true }; }
};

/**
 * A rank's picture, drawn with Slate's own lines and triangles from the shapes of the Godot client's RankIcons (which
 * draws them from SVG made in code), so it is sharp at any size and needs no content. Collapses to nothing for no rank.
 */
class SScctRankIcon : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SScctRankIcon) : _Size(24.0f), _Fade(1.0f) {}
		SLATE_ARGUMENT(float, Size)
		/** The picture's opacity (an emblem not reached yet is drawn faint). */
		SLATE_ARGUMENT(float, Fade)
		SLATE_ATTRIBUTE(FScctRankEmblem, Rank)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float Size = 24.0f;
	float Fade = 1.0f;
	TAttribute<FScctRankEmblem> Rank;
};
