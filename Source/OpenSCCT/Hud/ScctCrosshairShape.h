#pragma once

#include "CoreMinimal.h"

class FScctHudPainter;

/**
 * The merc's custom crosshair, a port of the Godot client's CrosshairSettings and CrosshairShape (an OpenSCCT addition,
 * modelled on CS2's crosshair settings): ten styles, length, gap, thickness, outline, centre dot, T style, colours and
 * the split style's values, with sizes in pixels at a reference screen height that scale with the screen. Off by
 * default: the original reticle (the four ticks of PaintCrosshair) draws. A CS2 share code (CS...) reads into it and
 * the current crosshair is kept as one.
 *
 * The options live in the settings registry on the Mercenary page, group Crosshair (Hud.Crosshair.*), with Reloaded's
 * classic_crosshair (Hud.ClassicCrosshair: the merc reticle 3 units left and 3 down of the centre on the original's
 * 640 x 480 canvas, for the original reticle and the custom one alike). The HUD reads them every frame, so a change on
 * the settings page shows on the live HUD at once; that HUD is the preview.
 *
 * Unattended runs:
 *   -ScctCrosshair=style,length,gap[,thickness[,outline[,dot[,r,g,b[,a]]]]]   the custom crosshair with these values
 *   -ScctCrosshair=original                                                   the original reticle
 *   -ScctCrosshair=CS...                                                      a CS2 share code
 * all forced for the run only (never saved), and the timed HUD commands Crosshair 0/1, CrosshairStyle N,
 * CrosshairGap N and ClassicCrosshair 0/1. Each time the drawn crosshair changes, a ScctCrosshair: line in the log
 * says what is drawn.
 */
namespace ScctCrosshair
{
	struct FSettings
	{
		bool bCustom = false;
		/** 0 to 9, the CS2 styles (StyleName). */
		int32 Style = 4;
		/** The crosshair follows the aim point (the weapon's kick) instead of staying at the fixed spot. */
		bool bFollowRecoil = false;
		bool bCenterDot = false;
		/** No top arm. */
		bool bTStyle = false;
		/** 0 none, 1 full, 2 half (a shadow to the bottom right). */
		int32 OutlineMode = 1;
		int32 Red = 50, Green = 250, Blue = 50, Alpha = 255;
		int32 OutlineRed = 0, OutlineGreen = 0, OutlineBlue = 0, OutlineAlpha = 255;
		int32 Gap = 3;
		int32 Length = 7;
		int32 Thickness = 2;
		int32 DynamicSpreadLimit = 181;
		int32 SplitDistance = 3;
		float InnerSplitAlpha = 1.0f;
		float OuterSplitAlpha = 0.5f;
		float SplitSizeRatio = 0.3f;
		/** The screen height the pixel sizes are for (CS2's cl_crosshair_screen_height). */
		int32 ScreenHeight = 1080;
		/** The original's red for 0.4 s after a hit, on the custom crosshair too. */
		bool bHitFlash = true;
		/** Reloaded's classic_crosshair: the reticle at the original off-centre spot. */
		bool bClassicPosition = false;

		/** Puts every value in the range CS2 allows. */
		void Clamp();
		bool IsDynamic() const { return Style == 0 || Style == 1 || Style == 2 || Style == 5 || Style == 7; }
	};

	/** One piece to paint: a solid box, or a ring (a full circle Outer - Inner thick). Outlines come first. */
	struct FPiece
	{
		bool bRing = false;
		FBox2f Box = FBox2f(FVector2f::ZeroVector, FVector2f::ZeroVector);
		FVector2f Centre = FVector2f::ZeroVector;
		float Inner = 0.0f;
		float Outer = 0.0f;
		FLinearColor Colour = FLinearColor::White;
	};

	const TCHAR* StyleName(int32 Style);

	/**
	 * The pieces to paint around Centre on a screen ScreenHeight tall. SpreadPx is how far the weapon's spread opens a
	 * dynamic style, in the same units; bHit draws the fill in the hit colour (when the settings keep the hit flash).
	 */
	TArray<FPiece> Build(const FSettings& Settings, const FVector2f& Centre, float ScreenHeight, float SpreadPx, bool bHit);

	/** Paints what Build gave. */
	void Paint(FScctHudPainter& Painter, const TArray<FPiece>& Pieces);

	/** The settings as they stand (stored, or forced for the run). */
	FSettings Read();

	/** Registers the options (once; later calls do nothing) and reads -ScctCrosshair. */
	void RegisterOptions();

	/** The timed HUD commands Crosshair, CrosshairStyle, CrosshairGap and ClassicCrosshair; false for any other. */
	bool RunCommand(const FString& Name, int32 Argument);

	/** The settings as a CS2 share code (CS and 44 characters). */
	FString ToShareCode(const FSettings& Settings);

	/** Reads a CS2 share code into Out (custom on; the hit flash and the classic position kept); false when it is not one. */
	bool FromShareCode(const FString& Code, FSettings& Out);

	/** One line for the log: what is drawn and with which values. */
	FString Describe(const FSettings& Settings);
}
