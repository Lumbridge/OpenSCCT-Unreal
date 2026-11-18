#pragma once

#include "CoreMinimal.h"

class FScctHudPainter;

/**
 * The player's HUD layout (Godot's HudEditorSettings and HudEditorTransform): each group of HUD elements, per side
 * (spy, merc), has an offset, a scale, an opacity and a shown flag, which the canvas applies around the group's
 * painters (FScctHudPainter::BeginGroup). The stock layout is every group at offset 0, scale 1, opacity 1, shown.
 *
 * THE GROUPS and the point of the safe area each scales about (the corner or edge it hangs from):
 *   Status      bottom left: health, lives, stance, gauges, vision mode, the coop icon          (0, 1)
 *   Equipment   bottom right: weapon, ammunition, gadgets, the backpack, the respawn weapon     (1, 1)
 *   Radar       top left: the radar and its place name                                         (0, 0)
 *   Objectives  top left under the radar: the objectives panel                                  (0, 0)
 *   Feed        top right: the kill feed                                                        (1, 0)
 *   Log         lower left above the status block: state lines, zones, console lines            (0, 1)
 *   Round       top centre: clock, teams, objectives done, countdowns, the headlines under it   (0.5, 0)
 *   Telemetry   the frame-rate and network readouts                                             (1, 0)
 *   Prompt      bottom centre: what Use does now                                                (0.5, 1)
 *   Chat        the passive chat lines (Hud/SScctChatPanel.h; the open composer is not moved)   (0, 1)
 * A point P of a group lands at A + D + S * (P - A), A the anchor, D the offset (X and Y as fractions of the safe
 * area's width and height, -1..1) and S the scale (0.25..3); a moved or scaled group is pushed back inside the safe
 * area by the box it drew last frame, as Godot clamps an edited element to the screen. The opacity (0.1..1) fades the
 * whole group on top of its own fades.
 *
 * THE OPTIONS are in the settings registry (Settings/ScctSettings.h), page Hud:
 *   Hud.Layout.<Spy|Merc>.<Group>.Scale and .Opacity  listed under "Layout: Spy" and "Layout: Mercenary"
 *   Hud.Layout.<Spy|Merc>.<Group>.X, .Y and .Visible  not listed (the edit mode moves and hides groups)
 * The Hud tab's reset puts them back with the rest of the page; the edit mode resets one group or a side.
 *
 * Unattended switch: -ScctHudLayout=<side>.<group>:X,Y,Scale,Opacity[,Visible] (several joined by '+', side spy or
 * merc, e.g. merc.radar:0.2,0.1,1.3,0.7) sets those values and saves them, as the edit mode would; an entry 'reset'
 * (alone, or first: reset+merc.radar:...) puts every group of both sides back. The log names every group that is not at
 * the stock layout when the HUD starts ("ScctHudLayout: loaded layout ...").
 */
enum class EScctHudGroup : uint8
{
	Status,
	Equipment,
	Radar,
	Objectives,
	Feed,
	Log,
	Round,
	Telemetry,
	Prompt,
	Chat,
	Num,
};

struct FScctHudGroupLayout
{
	float X = 0.0f;
	float Y = 0.0f;
	float Scale = 1.0f;
	float Opacity = 1.0f;
	bool bVisible = true;

	bool IsStock() const;
	/** Within Godot's ranges (HudElementSettings.Clamp). */
	void Clamp();
};

/** Where a group was drawn by the last paint: for the edit mode's outlines, its picking and the clamp. */
struct FScctHudGroupLive
{
	/** The box the group drew in, in the canvas's units before its transform; invalid when it drew nothing. */
	FBox2f Raw = FBox2f(ForceInit);
	/** Raw after the group's transform: where it is on the canvas. */
	FBox2f Placed = FBox2f(ForceInit);
	/** The canvas's size in its own units, and how its units map to the window (absolute = Origin + local * Scale). */
	FVector2f CanvasSize = FVector2f::ZeroVector;
	FVector2f AbsoluteOrigin = FVector2f::ZeroVector;
	float AbsoluteScale = 1.0f;
	/** The side the group was drawn for (0 spy, 1 merc) and when (FPlatformTime::Seconds). */
	int32 Side = 0;
	double PaintedAt = 0.0;
	/** The frame (GFrameCounter) it last drew something in. */
	uint64 PaintedFrame = 0;
};

namespace ScctHudLayout
{
	constexpr int32 NumGroups = static_cast<int32>(EScctHudGroup::Num);

	/** Registers the options and reads -ScctHudLayout; once per run (the HUD subsystem calls it). */
	void RegisterOptions();

	/** "Radar", "Status"... as in the option keys. */
	const TCHAR* GroupId(EScctHudGroup Group);
	/** "Radar", "Status block"... for people. */
	FText GroupLabel(EScctHudGroup Group);
	bool ParseGroup(const FString& Text, EScctHudGroup& OutGroup);
	const TCHAR* SideId(int32 Side);

	/** The point of the safe area the group scales about, as fractions of its size. */
	FVector2f Anchor(EScctHudGroup Group);

	/** The stored layout of a group for a side (0 spy, 1 merc). */
	FScctHudGroupLayout Get(int32 Side, EScctHudGroup Group);
	/** Stores it (clamped); FScctSettings::Save writes it to the user settings file. */
	void Set(int32 Side, EScctHudGroup Group, const FScctHudGroupLayout& Layout);
	void ResetGroup(int32 Side, EScctHudGroup Group);
	void ResetSide(int32 Side);

	/** Every group not at the stock layout ("merc.Radar x=0.20 y=0.10 scale=1.30 opacity=0.70"), or "stock". */
	FString Describe();

	/**
	 * Where a group with this layout goes on a canvas of this size: the offset and scale for BeginGroup. LastRaw (the
	 * box it drew last frame, may be invalid) keeps a moved or scaled group inside the canvas.
	 */
	void Placement(EScctHudGroup Group, const FScctHudGroupLayout& Layout, const FVector2f& CanvasSize, const FBox2f& LastRaw, FVector2f& OutOffset, float& OutScale);

	/** The edit mode (Hud/ScctHudEdit.h) is on: hidden groups are drawn faint so they can be found and shown again. */
	bool IsEditing();
	void SetEditing(bool bEditing);

	/** What the last paint of a group left. */
	const FScctHudGroupLive& Live(EScctHudGroup Group);

	/**
	 * The scope a canvas paints one group in: applies the side's layout to the painter until it ends, and records
	 * where the group drew. Draws() is false for a hidden group outside the edit mode: paint nothing then.
	 */
	class FScope
	{
	public:
		FScope(FScctHudPainter& InPainter, EScctHudGroup InGroup, int32 InSide);
		~FScope();
		bool Draws() const { return bDraws; }

	private:
		FScctHudPainter& Painter;
		EScctHudGroup Group;
		int32 Side;
		FVector2f Offset = FVector2f::ZeroVector;
		float Scale = 1.0f;
		bool bDraws = false;
	};
}
