#pragma once

#include "CoreMinimal.h"

class SWidget;
struct FSlateBrush;

/**
 * The rank pictures (the Godot client's RankIcons, spec/additions/progression.md): a level insignia per five levels
 * (its shape and colour by tier, one to five chevrons inside), an emblem per prestige (a star with more points and a
 * richer colour each prestige, round the trifocal goggles) and the master prestige emblem. They are OpenSCCT's own
 * drawings and are drawn here, by the same shapes, into a texture the first time one is asked for: no art file is
 * read and none is shipped. A picture is kept per size for the run. Game thread only.
 */
namespace ScctRankIcons
{
	/** OpenSCCT's ProgressionK.MaxLevel and MaxPrestige. */
	constexpr int32 MaxLevel = 55;
	constexpr int32 MaxPrestige = 10;

	/** A rank's picture at Size slate units: the prestige emblem once prestiged (the master one at the end), else the level insignia. Null without a rank (Level 0). */
	const FSlateBrush* For(int32 Prestige, int32 Level, float Size);

	/** The same for a packed rank (FScctOnlinePlayer::Rank: the level in the low byte, the prestige in the next). Null for none. */
	const FSlateBrush* ForPacked(int32 PackedRank, float Size);

	/** A prestige's emblem (1 to MaxPrestige). */
	const FSlateBrush* Prestige(int32 Prestige, float Size);

	/** The master prestige emblem. */
	const FSlateBrush* Master(float Size);

	/** The picture as a widget, Size square; takes no room when Brush is null. */
	TSharedRef<SWidget> Icon(const FSlateBrush* Brush, float Size);
}
